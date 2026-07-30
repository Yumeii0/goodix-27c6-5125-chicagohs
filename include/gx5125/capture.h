#ifndef GX5125_CAPTURE_H
#define GX5125_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "gx5125/sensor.h"
#include "gx5125/session.h"
#include "gx5125/tls.h"

typedef struct gx_capture_result {
    bool encrypted_ack_received;
    uint8_t encrypted_ack_flags;
    uint8_t response_cmd0;
    uint8_t response_cmd1;
    uint8_t metadata[5];
    size_t plaintext_bytes;
    unsigned int requests_sent;
} gx_capture_result;

int gx_capture_image(gx_session *session,
                     gx_tls_server *tls_server,
                     uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
                     unsigned int timeout_ms,
                     unsigned int max_requests,
                     gx_capture_result *result);

int gx_capture_image_cancelable(
    gx_session *session,
    gx_tls_server *tls_server,
    uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
    unsigned int timeout_ms,
    unsigned int max_requests,
    const atomic_bool *cancel_requested,
    gx_capture_result *result);

#endif
