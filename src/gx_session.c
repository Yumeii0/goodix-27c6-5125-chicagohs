#include "gx5125/session.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libusb-1.0/libusb.h>

#include "gx5125/protocol.h"
#include "gx5125/transport.h"

#define GX_RX_TRANSFER_CAPACITY 4096u
#define GX_RX_TIMEOUT_MS 100u
#define GX_TX_TIMEOUT_MS 1000u

static void gx_deadline_after_ms(struct timespec *deadline,
                                 unsigned int timeout_ms)
{
    uint64_t nanoseconds;

    (void)clock_gettime(CLOCK_REALTIME, deadline);
    nanoseconds = (uint64_t)deadline->tv_nsec +
                  (uint64_t)(timeout_ms % 1000u) * UINT64_C(1000000);
    deadline->tv_sec += (time_t)(timeout_ms / 1000u) +
                        (time_t)(nanoseconds / UINT64_C(1000000000));
    deadline->tv_nsec = (long)(nanoseconds % UINT64_C(1000000000));
}

static void gx_stream_discard_prefix(gx_session *session, size_t count)
{
    if (count >= session->stream_length) {
        session->stream_length = 0u;
        return;
    }

    memmove(session->stream,
            session->stream + count,
            session->stream_length - count);
    session->stream_length -= count;
}

static void gx_store_ack(gx_session *session, const gx_inner_view *inner)
{
    const uint8_t acked = (uint8_t)(inner->data[0] & UINT8_C(0xfe));
    const uint8_t cmd0 = (uint8_t)(acked >> 4);
    const uint8_t cmd1 = (uint8_t)((acked & UINT8_C(0x0f)) >> 1);
    gx_ack_state *state = &session->acknowledgements[cmd0][cmd1];
    size_t copy_length = inner->data_length;

    if (copy_length > sizeof(state->data)) {
        copy_length = sizeof(state->data);
    }

    state->sequence += 1u;
    state->acked_command = acked;
    state->flags = inner->data_length >= 2u ? inner->data[1] : 0u;
    state->data_length = copy_length;
    if (copy_length != 0u) {
        memcpy(state->data, inner->data, copy_length);
    }
}

static void gx_store_response(gx_session *session, const gx_inner_view *inner)
{
    gx_response_state *state = &session->responses[inner->cmd0][inner->cmd1];
    size_t copy_length = inner->data_length;

    if (copy_length > sizeof(state->data)) {
        copy_length = sizeof(state->data);
    }

    state->sequence += 1u;
    state->command_byte = inner->command_byte;
    state->data_length = copy_length;
    if (copy_length != 0u) {
        memcpy(state->data, inner->data, copy_length);
    }
}

static int gx_dispatch_outer(gx_session *session,
                             const uint8_t *frame,
                             size_t frame_length)
{
    gx_outer_view outer;
    gx_inner_view inner;
    int rc;

    rc = gx_outer_decode(frame, frame_length, &outer);
    if (rc < 0) {
        session->framing_errors += 1u;
        return rc;
    }

    session->outer_frames += 1u;
    if (outer.family == GX5125_OUTER_TLS) {
        int tls_rc = LIBUSB_SUCCESS;

        (void)pthread_mutex_lock(&session->mutex);
        session->tls_frames += 1u;
        if ((size_t)outer.payload_length >
            sizeof(session->tls_fifo) - session->tls_fifo_length) {
            session->tls_fifo_overflows += 1u;
            tls_rc = LIBUSB_ERROR_OVERFLOW;
        } else {
            if (outer.payload_length != 0u) {
                memcpy(session->tls_fifo + session->tls_fifo_length,
                       outer.payload,
                       outer.payload_length);
                session->tls_fifo_length += outer.payload_length;
                session->tls_bytes_received += outer.payload_length;
            }
            (void)pthread_cond_broadcast(&session->condition);
        }
        (void)pthread_mutex_unlock(&session->mutex);
        return tls_rc;
    }
    if (outer.family == GX5125_OUTER_NOTICE) {
        session->notice_frames += 1u;
    }

    rc = gx_inner_decode(outer.payload, outer.payload_length, &inner);
    if (rc < 0) {
        session->framing_errors += 1u;
        return rc;
    }
    if (!inner.checksum_ok) {
        session->checksum_errors += 1u;
        return LIBUSB_ERROR_IO;
    }

    session->inner_frames += 1u;
    (void)pthread_mutex_lock(&session->mutex);
    if (inner.cmd0 == UINT8_C(0x0b) && inner.cmd1 == 0u &&
        inner.data_length >= 1u) {
        gx_store_ack(session, &inner);
    } else {
        gx_store_response(session, &inner);
    }
    (void)pthread_cond_broadcast(&session->condition);
    (void)pthread_mutex_unlock(&session->mutex);
    return LIBUSB_SUCCESS;
}

static int gx_stream_parse(gx_session *session)
{
    while (session->stream_length >= GX5125_OUTER_HEADER_SIZE) {
        size_t start = 0u;
        bool found = false;
        uint16_t payload_length = 0u;
        size_t total_length;

        while (start + GX5125_OUTER_HEADER_SIZE <= session->stream_length) {
            const uint8_t *candidate = session->stream + start;

            if (gx_outer_family_supported(candidate[0])) {
                payload_length = (uint16_t)candidate[1] |
                                 (uint16_t)((uint16_t)candidate[2] << 8);
                if (payload_length <= GX5125_OUTER_MAX_PAYLOAD &&
                    candidate[3] == gx_outer_checksum(candidate[0],
                                                      payload_length)) {
                    found = true;
                    break;
                }
            }
            start += 1u;
        }

        if (!found) {
            if (session->stream_length > GX5125_OUTER_HEADER_SIZE - 1u) {
                gx_stream_discard_prefix(
                    session,
                    session->stream_length - (GX5125_OUTER_HEADER_SIZE - 1u));
            }
            return LIBUSB_SUCCESS;
        }

        if (start != 0u) {
            gx_stream_discard_prefix(session, start);
        }

        total_length = GX5125_OUTER_HEADER_SIZE + (size_t)payload_length;
        if (session->stream_length < total_length) {
            return LIBUSB_SUCCESS;
        }

        (void)gx_dispatch_outer(session, session->stream, total_length);
        gx_stream_discard_prefix(session, total_length);
    }

    return LIBUSB_SUCCESS;
}

static int gx_stream_feed(gx_session *session,
                          const uint8_t *bytes,
                          size_t length)
{
    if (session == NULL || (bytes == NULL && length != 0u)) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    if (length > sizeof(session->stream) - session->stream_length) {
        session->framing_errors += 1u;
        session->stream_length = 0u;
        if (length > sizeof(session->stream)) {
            return LIBUSB_ERROR_OVERFLOW;
        }
    }

    if (length != 0u) {
        memcpy(session->stream + session->stream_length, bytes, length);
        session->stream_length += length;
    }
    return gx_stream_parse(session);
}

static void gx_set_receiver_error(gx_session *session, int error)
{
    (void)pthread_mutex_lock(&session->mutex);
    if (session->receiver_error == LIBUSB_SUCCESS) {
        session->receiver_error = error;
    }
    (void)pthread_cond_broadcast(&session->condition);
    (void)pthread_mutex_unlock(&session->mutex);
}

static void *gx_receiver_main(void *argument)
{
    gx_session *session = argument;
    uint8_t bytes[GX_RX_TRANSFER_CAPACITY];

    while (!atomic_load_explicit(&session->stop_requested,
                                 memory_order_relaxed)) {
        size_t received = 0u;
        int rc = gx_usb_bulk_read(session->usb,
                                  bytes,
                                  sizeof(bytes),
                                  &received,
                                  GX_RX_TIMEOUT_MS);

        if (rc == LIBUSB_ERROR_TIMEOUT || rc == LIBUSB_ERROR_INTERRUPTED) {
            continue;
        }
        if (rc < 0) {
            gx_set_receiver_error(session, rc);
            break;
        }
        if (received != 0u) {
            rc = gx_stream_feed(session, bytes, received);
            if (rc < 0) {
                gx_set_receiver_error(session, rc);
                break;
            }
        }
    }
    return NULL;
}

int gx_session_start(gx_session *session, gx_usb_device *usb)
{
    int rc;

    if (session == NULL || usb == NULL || usb->handle == NULL ||
        !usb->claimed_data) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    memset(session, 0, sizeof(*session));
    session->usb = usb;
    session->receiver_error = LIBUSB_SUCCESS;
    atomic_init(&session->stop_requested, false);

    rc = pthread_mutex_init(&session->mutex, NULL);
    if (rc != 0) {
        return LIBUSB_ERROR_OTHER;
    }
    rc = pthread_cond_init(&session->condition, NULL);
    if (rc != 0) {
        (void)pthread_mutex_destroy(&session->mutex);
        return LIBUSB_ERROR_OTHER;
    }

    rc = pthread_create(&session->receiver_thread,
                        NULL,
                        gx_receiver_main,
                        session);
    if (rc != 0) {
        (void)pthread_cond_destroy(&session->condition);
        (void)pthread_mutex_destroy(&session->mutex);
        return LIBUSB_ERROR_OTHER;
    }

    session->receiver_started = true;
    return LIBUSB_SUCCESS;
}

void gx_session_stop(gx_session *session)
{
    if (session == NULL) {
        return;
    }

    if (session->receiver_started) {
        atomic_store_explicit(&session->stop_requested,
                              true,
                              memory_order_relaxed);
        (void)pthread_join(session->receiver_thread, NULL);
        session->receiver_started = false;
    }
    (void)pthread_cond_destroy(&session->condition);
    (void)pthread_mutex_destroy(&session->mutex);
}

static int gx_wait_for_ack(gx_session *session,
                           uint8_t cmd0,
                           uint8_t cmd1,
                           uint64_t baseline,
                           unsigned int timeout_ms,
                           gx_command_result *result)
{
    struct timespec deadline;
    gx_ack_state *state = &session->acknowledgements[cmd0][cmd1];
    int rc = 0;

    gx_deadline_after_ms(&deadline, timeout_ms);
    (void)pthread_mutex_lock(&session->mutex);
    while (state->sequence == baseline &&
           session->receiver_error == LIBUSB_SUCCESS) {
        rc = pthread_cond_timedwait(&session->condition,
                                    &session->mutex,
                                    &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
        if (rc != 0) {
            (void)pthread_mutex_unlock(&session->mutex);
            return LIBUSB_ERROR_OTHER;
        }
    }

    if (state->sequence != baseline) {
        result->ack_received = true;
        result->ack_flags = state->flags;
        (void)pthread_mutex_unlock(&session->mutex);
        return LIBUSB_SUCCESS;
    }
    if (session->receiver_error != LIBUSB_SUCCESS) {
        rc = session->receiver_error;
        (void)pthread_mutex_unlock(&session->mutex);
        return rc;
    }

    (void)pthread_mutex_unlock(&session->mutex);
    return LIBUSB_ERROR_TIMEOUT;
}

static int gx_wait_for_response(gx_session *session,
                                uint8_t cmd0,
                                uint8_t cmd1,
                                uint64_t baseline,
                                unsigned int timeout_ms,
                                gx_command_result *result)
{
    struct timespec deadline;
    gx_response_state *state = &session->responses[cmd0][cmd1];
    int rc = 0;

    gx_deadline_after_ms(&deadline, timeout_ms);
    (void)pthread_mutex_lock(&session->mutex);
    while (state->sequence == baseline &&
           session->receiver_error == LIBUSB_SUCCESS) {
        rc = pthread_cond_timedwait(&session->condition,
                                    &session->mutex,
                                    &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
        if (rc != 0) {
            (void)pthread_mutex_unlock(&session->mutex);
            return LIBUSB_ERROR_OTHER;
        }
    }

    if (state->sequence != baseline) {
        result->response_received = true;
        result->response_length = state->data_length;
        if (state->data_length != 0u) {
            memcpy(result->response, state->data, state->data_length);
        }
        (void)pthread_mutex_unlock(&session->mutex);
        return LIBUSB_SUCCESS;
    }
    if (session->receiver_error != LIBUSB_SUCCESS) {
        rc = session->receiver_error;
        (void)pthread_mutex_unlock(&session->mutex);
        return rc;
    }

    (void)pthread_mutex_unlock(&session->mutex);
    return LIBUSB_ERROR_TIMEOUT;
}

int gx_session_command(gx_session *session,
                       uint8_t cmd0,
                       uint8_t cmd1,
                       const uint8_t *payload,
                       size_t payload_length,
                       unsigned int ack_timeout_ms,
                       bool expect_response,
                       unsigned int response_timeout_ms,
                       unsigned int max_attempts,
                       gx_command_result *result)
{
    uint8_t inner[GX5125_OUTER_MAX_PAYLOAD];
    size_t inner_length = 0u;
    uint64_t ack_baseline;
    uint64_t response_baseline;
    bool wait_for_ack;
    unsigned int attempt;
    int last_error = LIBUSB_ERROR_OTHER;
    int rc;

    if (session == NULL || result == NULL || cmd0 > UINT8_C(0x0f) ||
        cmd1 > UINT8_C(0x07) || max_attempts == 0u ||
        (payload == NULL && payload_length != 0u)) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    memset(result, 0, sizeof(*result));
    wait_for_ack = ack_timeout_ms != 0u;
    rc = gx_inner_build(cmd0,
                        cmd1,
                        wait_for_ack,
                        payload,
                        payload_length,
                        inner,
                        sizeof(inner),
                        &inner_length);
    if (rc < 0) {
        return rc;
    }

    (void)pthread_mutex_lock(&session->mutex);
    ack_baseline = session->acknowledgements[cmd0][cmd1].sequence;
    response_baseline = session->responses[cmd0][cmd1].sequence;
    (void)pthread_mutex_unlock(&session->mutex);

    for (attempt = 1u; attempt <= max_attempts; ++attempt) {
        result->attempts_used = attempt;
        rc = gx_transport_send_outer(session->usb,
                                     GX5125_OUTER_COMMAND,
                                     inner,
                                     inner_length,
                                     GX_TX_TIMEOUT_MS);
        if (rc < 0) {
            last_error = rc;
            continue;
        }

        if (wait_for_ack) {
            rc = gx_wait_for_ack(session,
                                 cmd0,
                                 cmd1,
                                 ack_baseline,
                                 ack_timeout_ms,
                                 result);
            if (rc < 0) {
                last_error = rc;
                if (expect_response) {
                    rc = gx_wait_for_response(session,
                                              cmd0,
                                              cmd1,
                                              response_baseline,
                                              1u,
                                              result);
                    if (rc == LIBUSB_SUCCESS) {
                        return LIBUSB_SUCCESS;
                    }
                }
                continue;
            }
        }

        if (expect_response) {
            rc = gx_wait_for_response(session,
                                      cmd0,
                                      cmd1,
                                      response_baseline,
                                      response_timeout_ms,
                                      result);
            if (rc < 0) {
                last_error = rc;
                continue;
            }
        }

        return LIBUSB_SUCCESS;
    }

    return last_error;
}


void gx_session_tls_clear(gx_session *session)
{
    if (session == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&session->mutex);
    if (session->tls_fifo_length != 0u) {
        memset(session->tls_fifo, 0, session->tls_fifo_length);
    }
    session->tls_fifo_length = 0u;
    (void)pthread_mutex_unlock(&session->mutex);
}

int gx_session_tls_read(gx_session *session,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length,
                        unsigned int timeout_ms)
{
    struct timespec deadline;
    size_t copy_length;
    int rc = 0;

    if (output_length != NULL) {
        *output_length = 0u;
    }
    if (session == NULL || output == NULL || output_capacity == 0u ||
        output_length == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    gx_deadline_after_ms(&deadline, timeout_ms);
    (void)pthread_mutex_lock(&session->mutex);
    while (session->tls_fifo_length == 0u &&
           session->receiver_error == LIBUSB_SUCCESS &&
           !atomic_load_explicit(&session->stop_requested,
                                 memory_order_relaxed)) {
        rc = pthread_cond_timedwait(&session->condition,
                                    &session->mutex,
                                    &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
        if (rc != 0) {
            (void)pthread_mutex_unlock(&session->mutex);
            return LIBUSB_ERROR_OTHER;
        }
    }

    if (session->tls_fifo_length != 0u) {
        copy_length = session->tls_fifo_length;
        if (copy_length > output_capacity) {
            copy_length = output_capacity;
        }
        memcpy(output, session->tls_fifo, copy_length);
        if (copy_length < session->tls_fifo_length) {
            memmove(session->tls_fifo,
                    session->tls_fifo + copy_length,
                    session->tls_fifo_length - copy_length);
        }
        session->tls_fifo_length -= copy_length;
        *output_length = copy_length;
        (void)pthread_mutex_unlock(&session->mutex);
        return LIBUSB_SUCCESS;
    }

    if (session->receiver_error != LIBUSB_SUCCESS) {
        rc = session->receiver_error;
        (void)pthread_mutex_unlock(&session->mutex);
        return rc;
    }

    (void)pthread_mutex_unlock(&session->mutex);
    return LIBUSB_ERROR_TIMEOUT;
}

void gx_session_print_statistics(gx_session *session)
{
    if (session == NULL) {
        return;
    }

    printf("GOODIX_RX_STATS=outer:%llu inner:%llu tls:%llu tls_bytes:%llu tls_overflows:%llu notice:%llu framing_errors:%llu checksum_errors:%llu\n",
           (unsigned long long)session->outer_frames,
           (unsigned long long)session->inner_frames,
           (unsigned long long)session->tls_frames,
           (unsigned long long)session->tls_bytes_received,
           (unsigned long long)session->tls_fifo_overflows,
           (unsigned long long)session->notice_frames,
           (unsigned long long)session->framing_errors,
           (unsigned long long)session->checksum_errors);
}

int gx_session_test_feed(gx_session *session,
                         const uint8_t *bytes,
                         size_t length)
{
    return gx_stream_feed(session, bytes, length);
}
