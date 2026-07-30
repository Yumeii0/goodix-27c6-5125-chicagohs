#include "gx5125/protocol.h"

#include <limits.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

#include "gx5125/transport.h"

uint8_t gx_inner_command_byte(uint8_t cmd0, uint8_t cmd1, bool wait_for_ack)
{
    uint8_t value = (uint8_t)((cmd0 << 4) | (cmd1 << 1));

    if (!wait_for_ack) {
        value = (uint8_t)(value | UINT8_C(0x01));
    }
    return value;
}

uint8_t gx_inner_checksum(uint8_t command_byte,
                          uint16_t inner_length,
                          const uint8_t *data,
                          size_t data_length)
{
    uint8_t sum = (uint8_t)(command_byte & UINT8_C(0xfe));
    size_t index;

    sum = (uint8_t)(sum + (uint8_t)(inner_length & UINT16_C(0x00ff)));
    sum = (uint8_t)(sum + (uint8_t)(inner_length >> 8));
    for (index = 0u; index < data_length; ++index) {
        sum = (uint8_t)(sum + data[index]);
    }

    return (uint8_t)(GX5125_INNER_CHECKSUM_TARGET - sum);
}

int gx_inner_build(uint8_t cmd0,
                   uint8_t cmd1,
                   bool wait_for_ack,
                   const uint8_t *data,
                   size_t data_length,
                   uint8_t *output,
                   size_t output_capacity,
                   size_t *output_length)
{
    uint16_t inner_length;
    size_t total_length;
    uint8_t command_byte;

    if (cmd0 > UINT8_C(0x0f) || cmd1 > UINT8_C(0x07) ||
        (data == NULL && data_length != 0u) || output == NULL ||
        output_length == NULL || data_length > (size_t)UINT16_MAX - 1u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    inner_length = (uint16_t)(data_length + 1u);
    total_length = 3u + (size_t)inner_length;
    if (output_capacity < total_length) {
        return LIBUSB_ERROR_OVERFLOW;
    }

    command_byte = gx_inner_command_byte(cmd0, cmd1, wait_for_ack);
    output[0] = command_byte;
    output[1] = (uint8_t)(inner_length & UINT16_C(0x00ff));
    output[2] = (uint8_t)(inner_length >> 8);
    if (data_length != 0u) {
        memcpy(output + 3u, data, data_length);
    }
    output[3u + data_length] = gx_inner_checksum(command_byte,
                                                 inner_length,
                                                 data,
                                                 data_length);
    *output_length = total_length;
    return LIBUSB_SUCCESS;
}

int gx_inner_decode(const uint8_t *frame,
                    size_t frame_length,
                    gx_inner_view *view)
{
    uint16_t inner_length;
    size_t required_length;
    size_t data_length;
    uint8_t checksum;
    uint8_t expected;

    if (frame == NULL || view == NULL || frame_length < 4u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    inner_length = (uint16_t)frame[1] |
                   (uint16_t)((uint16_t)frame[2] << 8);
    if (inner_length < 1u) {
        return LIBUSB_ERROR_IO;
    }

    required_length = 3u + (size_t)inner_length;
    if (required_length > frame_length) {
        return LIBUSB_ERROR_IO;
    }

    data_length = (size_t)inner_length - 1u;
    checksum = frame[3u + data_length];
    expected = gx_inner_checksum(frame[0],
                                 inner_length,
                                 frame + 3u,
                                 data_length);

    view->command_byte = frame[0];
    view->cmd0 = (uint8_t)(frame[0] >> 4);
    view->cmd1 = (uint8_t)((frame[0] & UINT8_C(0x0f)) >> 1);
    view->no_ack = (frame[0] & UINT8_C(0x01)) != 0u;
    view->inner_length = inner_length;
    view->data = frame + 3u;
    view->data_length = data_length;
    view->checksum = checksum;
    view->checksum_ok = checksum == GX5125_INNER_CHECKSUM_BYPASS ||
                        checksum == expected;
    return LIBUSB_SUCCESS;
}

int gx_command_wire_build(uint8_t cmd0,
                          uint8_t cmd1,
                          bool wait_for_ack,
                          const uint8_t *data,
                          size_t data_length,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *logical_length,
                          size_t *wire_length)
{
    uint8_t inner[GX5125_OUTER_MAX_PAYLOAD];
    size_t inner_length = 0u;
    int rc;

    if (output == NULL || logical_length == NULL || wire_length == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    rc = gx_inner_build(cmd0,
                        cmd1,
                        wait_for_ack,
                        data,
                        data_length,
                        inner,
                        sizeof(inner),
                        &inner_length);
    if (rc < 0) {
        return rc;
    }

    return gx_outer_build(GX5125_OUTER_COMMAND,
                          inner,
                          inner_length,
                          output,
                          output_capacity,
                          logical_length,
                          wire_length);
}
