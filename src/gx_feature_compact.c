#include "gx5125/feature_compact.h"

#include <limits.h>
#include <string.h>

#define GX_FEATURE_COMPACT_BUILD_BYTES 200U
#define GX_FEATURE_COMPACT_MAX_CELLS 4096U

static size_t gx_ceil_div4_i32(int32_t value) {
    return ((size_t)value + 3U) / 4U;
}

size_t gx_feature_compact_linear_bytes(size_t value_count) {
    if (value_count > SIZE_MAX - 7U) return 0U;
    return (value_count + 7U) / 8U;
}

int gx_feature_compact_pack_linear(const uint8_t *values,
                                   size_t value_count,
                                   uint8_t *packed,
                                   size_t packed_bytes) {
    size_t required;
    size_t full_bytes;
    size_t byte_index;

    required = gx_feature_compact_linear_bytes(value_count);
    if (value_count == 0U) return 0;
    if (values == NULL || packed == NULL || required == 0U ||
        packed_bytes < required) return -1;

    full_bytes = value_count / 8U;
    for (byte_index = 0U; byte_index < full_bytes; ++byte_index) {
        size_t bit;
        uint8_t value = 0U;
        for (bit = 0U; bit < 8U; ++bit) {
            value |= (uint8_t)(values[byte_index * 8U + bit] << bit);
        }
        packed[byte_index] = value;
    }

    if ((value_count & 7U) != 0U) {
        const size_t base = full_bytes * 8U;
        const size_t remaining = value_count - base;
        size_t bit;
        uint8_t value = 0U;
        for (bit = 0U; bit < remaining; ++bit) {
            value |= (uint8_t)(values[base + bit] << bit);
        }
        packed[full_bytes] = value;
    }
    return 0;
}

int gx_feature_compact_build_quarter(const uint8_t *pixels,
                                     int32_t width,
                                     int32_t height,
                                     int32_t row_stride,
                                     uint8_t output[200]) {
    size_t compact_width;
    size_t compact_height;
    size_t compact_cells;
    size_t packed_bytes;
    uint8_t cells[GX_FEATURE_COMPACT_MAX_CELLS];
    size_t compact_y;

    if (pixels == NULL || output == NULL || width <= 0 || height <= 0 ||
        row_stride < width) return -1;

    compact_width = gx_ceil_div4_i32(width);
    compact_height = gx_ceil_div4_i32(height);
    if (compact_width != 0U && compact_height > SIZE_MAX / compact_width) return -1;
    compact_cells = compact_width * compact_height;
    packed_bytes = gx_feature_compact_linear_bytes(compact_cells);
    if (compact_cells == 0U || compact_cells > sizeof(cells) ||
        packed_bytes == 0U || packed_bytes > GX_FEATURE_COMPACT_BUILD_BYTES) return -1;

    for (compact_y = 0U; compact_y < compact_height; ++compact_y) {
        const size_t y0 = compact_y * 4U;
        size_t y1 = y0 + 4U;
        size_t compact_x;
        if (y1 > (size_t)height) y1 = (size_t)height;

        for (compact_x = 0U; compact_x < compact_width; ++compact_x) {
            const size_t x0 = compact_x * 4U;
            size_t x1 = x0 + 4U;
            uint32_t sum = 0U;
            uint32_t count = 0U;
            size_t y;
            if (x1 > (size_t)width) x1 = (size_t)width;

            for (y = y0; y < y1; ++y) {
                size_t x;
                const uint8_t *row = pixels + y * (size_t)row_stride;
                for (x = x0; x < x1; ++x) {
                    sum += row[x];
                    ++count;
                }
            }
            cells[compact_y * compact_width + compact_x] =
                (uint8_t)(count <= sum * 2U ? 1U : 0U);
        }
    }

    memset(output, 0xff, GX_FEATURE_COMPACT_BUILD_BYTES);
    return gx_feature_compact_pack_linear(cells, compact_cells, output,
                                          GX_FEATURE_COMPACT_BUILD_BYTES);
}

int gx_feature_compact_expand(const uint8_t *packed,
                              size_t packed_bytes,
                              int32_t mode,
                              int32_t full_width,
                              int32_t full_height,
                              uint8_t *output,
                              size_t output_bytes,
                              int32_t *output_width,
                              int32_t *output_height) {
    size_t compact_width;
    size_t compact_height;
    size_t compact_cells;
    size_t required_packed;
    int32_t width;
    int32_t height;
    int32_t factor;
    size_t required_output;
    int32_t y;

    if (output_width == NULL || output_height == NULL) return -1;
    *output_width = 0;
    *output_height = 0;
    if (full_width <= 0 || full_height <= 0) return -1;

    compact_width = gx_ceil_div4_i32(full_width);
    compact_height = gx_ceil_div4_i32(full_height);
    if (compact_width != 0U && compact_height > SIZE_MAX / compact_width) return -1;
    compact_cells = compact_width * compact_height;
    required_packed = gx_feature_compact_linear_bytes(compact_cells);
    if (packed == NULL || required_packed == 0U || packed_bytes < required_packed) return -1;

    if (mode == 1) {
        factor = 2;
        width = full_width >> 1;
        height = full_height >> 1;
    } else {
        factor = 4;
        width = full_width;
        height = full_height;
    }
    if (width <= 0 || height <= 0) return -1;
    if ((size_t)height > SIZE_MAX / (size_t)width) return -1;
    required_output = (size_t)width * (size_t)height;
    if (output == NULL || output_bytes < required_output) return -1;

    for (y = 0; y < height; ++y) {
        const size_t compact_y = (size_t)(y / factor);
        int32_t x;
        for (x = 0; x < width; ++x) {
            const size_t compact_x = (size_t)(x / factor);
            const size_t bit_index = compact_y * compact_width + compact_x;
            output[(size_t)y * (size_t)width + (size_t)x] =
                (uint8_t)((packed[bit_index >> 3] >> (bit_index & 7U)) & 1U);
        }
    }

    *output_width = width;
    *output_height = height;
    return 0;
}
