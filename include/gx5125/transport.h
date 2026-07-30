#ifndef GX5125_TRANSPORT_H
#define GX5125_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/usb.h"

#define GX5125_OUTER_HEADER_SIZE 4u
#define GX5125_OUTER_MAX_PAYLOAD 0x6bffu
#define GX5125_OUTER_COMMAND UINT8_C(0xa0)
#define GX5125_OUTER_TLS     UINT8_C(0xb0)
#define GX5125_OUTER_NOTICE  UINT8_C(0xc0)

typedef struct gx_outer_view {
    uint8_t type_flags;
    uint8_t family;
    uint16_t payload_length;
    const uint8_t *payload;
} gx_outer_view;

bool gx_outer_family_supported(uint8_t type_flags);
uint8_t gx_outer_checksum(uint8_t type_flags, uint16_t payload_length);
size_t gx_outer_wire_size(size_t payload_length);

int gx_outer_build(uint8_t type_flags,
                   const uint8_t *payload,
                   size_t payload_length,
                   uint8_t *output,
                   size_t output_capacity,
                   size_t *logical_length,
                   size_t *wire_length);

int gx_outer_decode(const uint8_t *frame,
                    size_t frame_length,
                    gx_outer_view *view);

int gx_transport_send_outer(gx_usb_device *device,
                            uint8_t type_flags,
                            const uint8_t *payload,
                            size_t payload_length,
                            unsigned int timeout_ms);

#endif
