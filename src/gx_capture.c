#include "gx5125/capture.h"

#include <string.h>
#include <time.h>

#include <libusb-1.0/libusb.h>

#include "gx5125/protocol.h"
#include "gx5125/transport.h"

#define GX_TLS_PLAINTEXT_STREAM_CAPACITY 32768u
#define GX_TLS_READ_CHUNK 16384u

static uint64_t gx_capture_monotonic_ms(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static int gx_capture_send_image_request(gx_session *session)
{
    static const uint8_t payload[2] = {0x01, 0x00};
    uint8_t inner[32];
    size_t inner_length = 0u;
    int rc;

    rc = gx_inner_build(0x02, 0x00,
                        true,
                        payload, sizeof(payload),
                        inner, sizeof(inner),
                        &inner_length);
    if (rc < 0) {
        return rc;
    }
    return gx_transport_send_outer(session->usb,
                                   GX5125_OUTER_COMMAND,
                                   inner,
                                   inner_length,
                                   1000u);
}

static void gx_capture_discard_prefix(uint8_t *stream,
                                      size_t *stream_length,
                                      size_t count)
{
    if (count >= *stream_length) {
        *stream_length = 0u;
        return;
    }
    memmove(stream, stream + count, *stream_length - count);
    *stream_length -= count;
}

static int gx_capture_parse_stream(uint8_t *stream,
                                   size_t *stream_length,
                                   uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
                                   gx_capture_result *result,
                                   bool *complete)
{
    while (*stream_length >= 4u) {
        uint16_t inner_length;
        size_t total_length;
        gx_inner_view inner;
        int rc;

        inner_length = (uint16_t)stream[1] |
                       (uint16_t)((uint16_t)stream[2] << 8);
        if (inner_length < 1u || inner_length > GX5125_OUTER_MAX_PAYLOAD) {
            gx_capture_discard_prefix(stream, stream_length, 1u);
            continue;
        }
        total_length = 3u + (size_t)inner_length;
        if (*stream_length < total_length) {
            return LIBUSB_SUCCESS;
        }

        rc = gx_inner_decode(stream, total_length, &inner);
        if (rc < 0 || !inner.checksum_ok) {
            gx_capture_discard_prefix(stream, stream_length, 1u);
            continue;
        }

        if (inner.cmd0 == 0x0bu && inner.cmd1 == 0u && inner.data_length >= 1u) {
            if ((inner.data[0] & UINT8_C(0xfe)) == UINT8_C(0x20)) {
                result->encrypted_ack_received = true;
                result->encrypted_ack_flags = inner.data_length >= 2u
                                                   ? inner.data[1]
                                                   : 0u;
            }
        } else if (inner.cmd0 == 0x02u && inner.cmd1 == 0x00u &&
                   inner.data_length >= 5u + GX5125_IMAGE_RAW_FRAME_SIZE) {
            result->response_cmd0 = inner.cmd0;
            result->response_cmd1 = inner.cmd1;
            memcpy(result->metadata, inner.data, sizeof(result->metadata));
            memcpy(raw_frame,
                   inner.data + 5u,
                   GX5125_IMAGE_RAW_FRAME_SIZE);
            *complete = true;
            return LIBUSB_SUCCESS;
        }

        gx_capture_discard_prefix(stream, stream_length, total_length);
    }
    return LIBUSB_SUCCESS;
}

int gx_capture_image_cancelable(
    gx_session *session,
    gx_tls_server *tls_server,
    uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
    unsigned int timeout_ms,
    unsigned int max_requests,
    const atomic_bool *cancel_requested,
    gx_capture_result *result)
{
    uint8_t stream[GX_TLS_PLAINTEXT_STREAM_CAPACITY];
    uint8_t chunk[GX_TLS_READ_CHUNK];
    size_t stream_length = 0u;
    unsigned int request;

    if (session == NULL || tls_server == NULL || raw_frame == NULL ||
        result == NULL || timeout_ms == 0u || max_requests == 0u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    memset(result, 0, sizeof(*result));
    memset(raw_frame, 0, GX5125_IMAGE_RAW_FRAME_SIZE);
    memset(stream, 0, sizeof(stream));
    memset(chunk, 0, sizeof(chunk));

    for (request = 1u; request <= max_requests; ++request) {
        if (cancel_requested != NULL &&
            atomic_load_explicit(cancel_requested, memory_order_relaxed)) {
            memset(stream, 0, sizeof(stream));
            memset(chunk, 0, sizeof(chunk));
            return LIBUSB_ERROR_INTERRUPTED;
        }
        const uint64_t deadline = gx_capture_monotonic_ms() + timeout_ms;
        bool complete = false;
        int rc;

        rc = gx_capture_send_image_request(session);
        if (rc < 0) {
            memset(stream, 0, sizeof(stream));
            memset(chunk, 0, sizeof(chunk));
            return rc;
        }
        result->requests_sent = request;

        while (gx_capture_monotonic_ms() < deadline) {
            if (cancel_requested != NULL &&
                atomic_load_explicit(cancel_requested, memory_order_relaxed)) {
                memset(stream, 0, sizeof(stream));
                memset(chunk, 0, sizeof(chunk));
                return LIBUSB_ERROR_INTERRUPTED;
            }
            const uint64_t now = gx_capture_monotonic_ms();
            unsigned int wait_ms;
            size_t chunk_length = 0u;

            if (now >= deadline) {
                break;
            }
            wait_ms = (unsigned int)(deadline - now);
            if (wait_ms > 1000u) {
                wait_ms = 1000u;
            }
            rc = gx_tls_server_read_application(tls_server,
                                                session,
                                                chunk,
                                                sizeof(chunk),
                                                &chunk_length,
                                                wait_ms);
            if (rc == LIBUSB_ERROR_TIMEOUT) {
                continue;
            }
            if (rc < 0) {
                memset(stream, 0, sizeof(stream));
                memset(chunk, 0, sizeof(chunk));
                return rc;
            }
            if (chunk_length > sizeof(stream) - stream_length) {
                memset(stream, 0, sizeof(stream));
                memset(chunk, 0, sizeof(chunk));
                return LIBUSB_ERROR_OVERFLOW;
            }
            memcpy(stream + stream_length, chunk, chunk_length);
            stream_length += chunk_length;
            result->plaintext_bytes += chunk_length;
            memset(chunk, 0, chunk_length);

            rc = gx_capture_parse_stream(stream,
                                         &stream_length,
                                         raw_frame,
                                         result,
                                         &complete);
            if (rc < 0) {
                memset(stream, 0, sizeof(stream));
                memset(chunk, 0, sizeof(chunk));
                return rc;
            }
            if (complete) {
                memset(stream, 0, sizeof(stream));
                memset(chunk, 0, sizeof(chunk));
                return LIBUSB_SUCCESS;
            }
        }
    }

    memset(stream, 0, sizeof(stream));
    memset(chunk, 0, sizeof(chunk));
    return LIBUSB_ERROR_TIMEOUT;
}


int gx_capture_image(gx_session *session,
                     gx_tls_server *tls_server,
                     uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
                     unsigned int timeout_ms,
                     unsigned int max_requests,
                     gx_capture_result *result)
{
    return gx_capture_image_cancelable(session, tls_server, raw_frame,
                                       timeout_ms, max_requests, NULL, result);
}
