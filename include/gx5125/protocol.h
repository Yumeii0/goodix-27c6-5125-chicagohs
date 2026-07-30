#ifndef GX5125_PROTOCOL_H
#define GX5125_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GX5125_INNER_CHECKSUM_TARGET UINT8_C(0xaa)
#define GX5125_INNER_CHECKSUM_BYPASS UINT8_C(0x88)

typedef struct gx_inner_view {
    uint8_t command_byte;
    uint8_t cmd0;
    uint8_t cmd1;
    bool no_ack;
    uint16_t inner_length;
    const uint8_t *data;
    size_t data_length;
    uint8_t checksum;
    bool checksum_ok;
} gx_inner_view;

uint8_t gx_inner_command_byte(uint8_t cmd0, uint8_t cmd1, bool wait_for_ack);
uint8_t gx_inner_checksum(uint8_t command_byte,
                          uint16_t inner_length,
                          const uint8_t *data,
                          size_t data_length);

int gx_inner_build(uint8_t cmd0,
                   uint8_t cmd1,
                   bool wait_for_ack,
                   const uint8_t *data,
                   size_t data_length,
                   uint8_t *output,
                   size_t output_capacity,
                   size_t *output_length);

int gx_inner_decode(const uint8_t *frame,
                    size_t frame_length,
                    gx_inner_view *view);

int gx_command_wire_build(uint8_t cmd0,
                          uint8_t cmd1,
                          bool wait_for_ack,
                          const uint8_t *data,
                          size_t data_length,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *logical_length,
                          size_t *wire_length);

#endif
