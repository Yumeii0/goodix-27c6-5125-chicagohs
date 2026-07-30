#include "gx5125/transport.h"

#include <stdlib.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

bool gx_outer_family_supported(uint8_t type_flags)
{
    const uint8_t family = (uint8_t)(type_flags & UINT8_C(0xf0));

    return family == GX5125_OUTER_COMMAND ||
           family == GX5125_OUTER_TLS ||
           family == GX5125_OUTER_NOTICE;
}

uint8_t gx_outer_checksum(uint8_t type_flags, uint16_t payload_length)
{
    return (uint8_t)(type_flags +
                     (uint8_t)(payload_length & UINT16_C(0x00ff)) +
                     (uint8_t)(payload_length >> 8));
}

size_t gx_outer_wire_size(size_t payload_length)
{
    size_t logical;
    size_t remainder;

    if (payload_length > GX5125_OUTER_MAX_PAYLOAD) {
        return 0u;
    }

    logical = GX5125_OUTER_HEADER_SIZE + payload_length;
    remainder = logical % GX5125_USB_BLOCK_SIZE;
    return remainder == 0u ? logical : logical + GX5125_USB_BLOCK_SIZE - remainder;
}

int gx_outer_build(uint8_t type_flags,
                   const uint8_t *payload,
                   size_t payload_length,
                   uint8_t *output,
                   size_t output_capacity,
                   size_t *logical_length,
                   size_t *wire_length)
{
    size_t wire;
    uint16_t length16;

    if (!gx_outer_family_supported(type_flags) ||
        (payload == NULL && payload_length != 0u) || output == NULL ||
        logical_length == NULL || wire_length == NULL ||
        payload_length > GX5125_OUTER_MAX_PAYLOAD) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    wire = gx_outer_wire_size(payload_length);
    if (wire == 0u || output_capacity < wire) {
        return LIBUSB_ERROR_OVERFLOW;
    }

    memset(output, 0, wire);
    length16 = (uint16_t)payload_length;
    output[0] = type_flags;
    output[1] = (uint8_t)(length16 & UINT16_C(0x00ff));
    output[2] = (uint8_t)(length16 >> 8);
    output[3] = gx_outer_checksum(type_flags, length16);
    if (payload_length != 0u) {
        memcpy(output + GX5125_OUTER_HEADER_SIZE, payload, payload_length);
    }

    *logical_length = GX5125_OUTER_HEADER_SIZE + payload_length;
    *wire_length = wire;
    return LIBUSB_SUCCESS;
}

int gx_outer_decode(const uint8_t *frame,
                    size_t frame_length,
                    gx_outer_view *view)
{
    uint16_t payload_length;
    size_t required;

    if (frame == NULL || view == NULL || frame_length < GX5125_OUTER_HEADER_SIZE) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    if (!gx_outer_family_supported(frame[0])) {
        return LIBUSB_ERROR_IO;
    }

    payload_length = (uint16_t)frame[1] |
                     (uint16_t)((uint16_t)frame[2] << 8);
    if (payload_length > GX5125_OUTER_MAX_PAYLOAD ||
        frame[3] != gx_outer_checksum(frame[0], payload_length)) {
        return LIBUSB_ERROR_IO;
    }

    required = GX5125_OUTER_HEADER_SIZE + (size_t)payload_length;
    if (required > frame_length) {
        return LIBUSB_ERROR_IO;
    }

    view->type_flags = frame[0];
    view->family = (uint8_t)(frame[0] & UINT8_C(0xf0));
    view->payload_length = payload_length;
    view->payload = frame + GX5125_OUTER_HEADER_SIZE;
    return LIBUSB_SUCCESS;
}

int gx_transport_send_outer(gx_usb_device *device,
                            uint8_t type_flags,
                            const uint8_t *payload,
                            size_t payload_length,
                            unsigned int timeout_ms)
{
    uint8_t *wire;
    size_t wire_capacity;
    size_t logical_length = 0u;
    size_t wire_length = 0u;
    int rc;

    wire_capacity = gx_outer_wire_size(payload_length);
    if (wire_capacity == 0u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    wire = calloc(1u, wire_capacity);
    if (wire == NULL) {
        return LIBUSB_ERROR_NO_MEM;
    }

    rc = gx_outer_build(type_flags,
                        payload,
                        payload_length,
                        wire,
                        wire_capacity,
                        &logical_length,
                        &wire_length);
    (void)logical_length;
    if (rc == LIBUSB_SUCCESS) {
        rc = gx_usb_bulk_write_64(device, wire, wire_length, timeout_ms);
    }

    memset(wire, 0, wire_capacity);
    free(wire);
    return rc;
}
