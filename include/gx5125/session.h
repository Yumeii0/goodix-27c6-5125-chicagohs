#ifndef GX5125_SESSION_H
#define GX5125_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>
#include <stdatomic.h>

#include "gx5125/usb.h"

#define GX5125_RESPONSE_CAPACITY 1024u
#define GX5125_STREAM_CAPACITY 65536u
#define GX5125_TLS_FIFO_CAPACITY 131072u

typedef struct gx_ack_state {
    uint64_t sequence;
    uint8_t acked_command;
    uint8_t flags;
    size_t data_length;
    uint8_t data[32];
} gx_ack_state;

typedef struct gx_response_state {
    uint64_t sequence;
    uint8_t command_byte;
    size_t data_length;
    uint8_t data[GX5125_RESPONSE_CAPACITY];
} gx_response_state;

typedef struct gx_command_result {
    bool ack_received;
    uint8_t ack_flags;
    bool response_received;
    size_t response_length;
    uint8_t response[GX5125_RESPONSE_CAPACITY];
    unsigned int attempts_used;
} gx_command_result;

typedef struct gx_session {
    gx_usb_device *usb;
    pthread_t receiver_thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    atomic_bool stop_requested;
    bool receiver_started;
    int receiver_error;

    uint8_t stream[GX5125_STREAM_CAPACITY];
    size_t stream_length;

    uint8_t tls_fifo[GX5125_TLS_FIFO_CAPACITY];
    size_t tls_fifo_length;
    uint64_t tls_bytes_received;
    uint64_t tls_fifo_overflows;

    gx_ack_state acknowledgements[16][8];
    gx_response_state responses[16][8];

    uint64_t outer_frames;
    uint64_t inner_frames;
    uint64_t tls_frames;
    uint64_t notice_frames;
    uint64_t framing_errors;
    uint64_t checksum_errors;
} gx_session;

int gx_session_start(gx_session *session, gx_usb_device *usb);
void gx_session_stop(gx_session *session);

int gx_session_command(gx_session *session,
                       uint8_t cmd0,
                       uint8_t cmd1,
                       const uint8_t *payload,
                       size_t payload_length,
                       unsigned int ack_timeout_ms,
                       bool expect_response,
                       unsigned int response_timeout_ms,
                       unsigned int max_attempts,
                       gx_command_result *result);

void gx_session_tls_clear(gx_session *session);
int gx_session_tls_read(gx_session *session,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length,
                        unsigned int timeout_ms);

void gx_session_print_statistics(gx_session *session);

/* Offline parser test entry point; no USB access. */
int gx_session_test_feed(gx_session *session,
                         const uint8_t *bytes,
                         size_t length);

#endif
