#include "gx5125/feature_descriptor_sample.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gx5125/feature_orientation.h"
#include "gx5125/feature_primitives.h"

#define GX_ANGLE_PERIOD INT32_C(0x6488)
#define GX_ANGLE_HALF INT32_C(0x3244)

static int32_t arithmetic_shift_right_i32(int32_t value, unsigned shift) {
    if (shift == 0U) return value;
    if (value >= 0) return value >> shift;
    return (int32_t)~((~(uint32_t)value) >> shift);
}

static int64_t arithmetic_shift_right_i64(int64_t value, unsigned shift) {
    if (shift == 0U) return value;
    if (value >= 0) return value >> shift;
    return (int64_t)~((~(uint64_t)value) >> shift);
}

static int32_t wrap_mul_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left * (uint32_t)right);
}

static int32_t round_signed_q32(int64_t value) {
    int64_t shifted = arithmetic_shift_right_i64(value, 16U);
    uint64_t magnitude;
    int32_t rounded;

    if (shifted < 0) {
        magnitude = (uint64_t)(-shifted);
        rounded = (int32_t)((magnitude >> 16U) +
                            (((magnitude & UINT64_C(0x8000)) != 0U) ? 1U : 0U));
        return -rounded;
    }
    magnitude = (uint64_t)shifted;
    return (int32_t)((magnitude >> 16U) +
                     (((magnitude & UINT64_C(0x8000)) != 0U) ? 1U : 0U));
}

uint32_t gx_feature_isqrt_u32(uint32_t value) {
    uint32_t result = 0U;
    uint32_t bit = UINT32_C(0x8000);
    uint8_t shift = 15U;

    if (value < 2U) return value;
    do {
        const uint32_t trial = (bit + result * 2U) << shift;
        --shift;
        if (trial <= value) {
            result += bit;
            value -= trial;
        }
        bit >>= 1U;
    } while (bit != 0U);
    return result;
}

uint32_t gx_feature_isqrt_u64(uint64_t value) {
    uint32_t result = 0U;
    uint64_t bit = UINT64_C(0x80000000);
    uint8_t shift = 31U;

    if (value < 2U) return (uint32_t)value;
    do {
        /* AlgoChicago uses LEA into EDX here.  The doubled partial root
         * therefore wraps at 32 bits before it is zero-extended to 64 bits. */
        const uint32_t doubled = result * UINT32_C(2);
        const uint64_t trial = ((uint64_t)doubled + bit) << shift;
        --shift;
        if (trial <= value) {
            result += (uint32_t)bit;
            value -= trial;
        }
        bit >>= 1U;
    } while (bit != 0U);
    return result;
}

static size_t histogram_index(int32_t x_bin, int32_t y_bin, int32_t angle_bin) {
    return (size_t)((y_bin + 1) * GX_FEATURE_DESCRIPTOR_GRID_SIDE +
                    (x_bin + 1)) * GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS +
           (size_t)angle_bin;
}

void gx_feature_descriptor_accumulate(
    int32_t histogram[GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS],
    int32_t x_q9,
    int32_t y_q9,
    int32_t orientation_q12,
    int32_t weight_q9) {
    int32_t x_bin;
    int32_t y_bin;
    int32_t orientation_bin;
    int32_t x_fraction;
    int32_t orientation_fraction;
    int32_t orientation_bin0;
    int32_t orientation_bin1;
    uint32_t y_weight0;
    int32_t y_weight1;
    uint32_t x0_y0;
    uint32_t x1_y0;
    uint32_t x0_y1;
    uint32_t orient_mix_a;
    uint32_t orient_mix_b;

    if (histogram == NULL) return;

    x_bin = arithmetic_shift_right_i32(x_q9, 9U);
    y_bin = arithmetic_shift_right_i32(y_q9, 9U);
    orientation_bin = arithmetic_shift_right_i32(orientation_q12, 12U);
    x_fraction = x_q9 - x_bin * 0x200;
    orientation_fraction = orientation_q12 - orientation_bin * 0x1000;
    orientation_bin0 = orientation_bin % 8;
    orientation_bin1 = (orientation_bin + 1) % 8;
    if (orientation_bin0 < 0) orientation_bin0 += 8;
    if (orientation_bin1 < 0) orientation_bin1 += 8;

    y_weight0 = (uint32_t)((y_q9 - y_bin * 0x200) *
                           arithmetic_shift_right_i32(weight_q9, 9U)) >> 9U;
    y_weight1 = arithmetic_shift_right_i32(weight_q9, 9U) - (int32_t)y_weight0;
    x0_y0 = y_weight0 * (uint32_t)x_fraction >> 9U;
    x1_y0 = (uint32_t)(y_weight1 * x_fraction) >> 9U;
    y_weight1 -= (int32_t)x1_y0;
    x0_y1 = (y_weight0 - x0_y0) * (uint32_t)orientation_fraction;
    orient_mix_a = (uint32_t)y_weight1 * (uint32_t)orientation_fraction;
    orient_mix_b = x1_y0 * (uint32_t)orientation_fraction;

#define GX_ADD(INDEX, VALUE) \
    do { \
        const size_t gx_i = (INDEX); \
        histogram[gx_i] = (int32_t)((uint32_t)histogram[gx_i] + (uint32_t)(VALUE)); \
    } while (0)

    GX_ADD(histogram_index(x_bin, y_bin, orientation_bin0),
           arithmetic_shift_right_i32(
               y_weight1 - (int32_t)(orient_mix_a >> 12U), 5U));
    GX_ADD(histogram_index(x_bin, y_bin, orientation_bin1),
           (int32_t)(orient_mix_a >> 17U));
    GX_ADD(histogram_index(x_bin + 1, y_bin, orientation_bin0),
           arithmetic_shift_right_i32(
               (int32_t)x1_y0 - (int32_t)(orient_mix_b >> 12U), 5U));
    GX_ADD(histogram_index(x_bin + 1, y_bin, orientation_bin1),
           (int32_t)(orient_mix_b >> 17U));
    GX_ADD(histogram_index(x_bin, y_bin + 1, orientation_bin0),
           arithmetic_shift_right_i32(
               (int32_t)(y_weight0 - x0_y0) - (int32_t)(x0_y1 >> 12U), 5U));
    GX_ADD(histogram_index(x_bin, y_bin + 1, orientation_bin1),
           (int32_t)(x0_y1 >> 17U));
    GX_ADD(histogram_index(x_bin + 1, y_bin + 1, orientation_bin0),
           arithmetic_shift_right_i32(
               (int32_t)x0_y0 -
                   (int32_t)(((uint32_t)x0_y0 *
                              (uint32_t)orientation_fraction) >> 12U),
               5U));
    GX_ADD(histogram_index(x_bin + 1, y_bin + 1, orientation_bin1),
           (int32_t)(((uint32_t)x0_y0 *
                      (uint32_t)orientation_fraction) >> 17U));

#undef GX_ADD
}

void gx_feature_descriptor_sample(
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    int16_t angle_units,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    int32_t output[GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES]) {
    int32_t histogram[GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS];
    uint32_t gaussian[33U * 33U];
    const int16_t *orientation_pixels;
    const int32_t *magnitude_pixels;
    int32_t scale3;
    int32_t radius;
    int32_t sin_q14 = 0;
    int32_t cos_q14 = 0;
    int64_t reciprocal;
    int32_t cos_step;
    int32_t sin_step;
    int32_t min_y;
    int32_t max_y;
    int32_t min_x;
    int32_t max_x;
    int32_t dy;
    uint32_t coefficient;

    if (output == NULL) return;
    memset(output, 0, GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES * sizeof(output[0]));
    if (orientation == NULL || magnitude == NULL || orientation->pixels == NULL ||
        magnitude->pixels == NULL || scale_q16 == 0 || magnitude->width <= 2 ||
        magnitude->height <= 2 || orientation->width != magnitude->width ||
        orientation->height != magnitude->height) {
        return;
    }

    scale3 = wrap_mul_i32(scale_q16, 3);
    if (scale3 == 0) return;
    radius = round_signed_q32((int64_t)scale3 * INT64_C(0x38916));
    if (radius < 0) radius = -radius;
    if (radius > 32) radius = 32;

    gx_feature_angle_sincos_q14((int32_t)angle_units, &sin_q14, &cos_q14);
    reciprocal = INT64_C(0x1000000000) / (int64_t)scale3;
    cos_step = (int32_t)arithmetic_shift_right_i64((int64_t)cos_q14 * reciprocal, 25U);
    sin_step = (int32_t)arithmetic_shift_right_i64((int64_t)sin_q14 * reciprocal, 25U);

    min_y = -radius;
    if (min_y < 1 - y) min_y = 1 - y;
    max_y = radius;
    if (magnitude->height - y - 2 < max_y) max_y = magnitude->height - y - 2;
    min_x = -radius;
    if (min_x < 1 - x) min_x = 1 - x;
    max_x = radius;
    if (magnitude->width - x - 2 < max_x) max_x = magnitude->width - x - 2;

    memset(histogram, 0, sizeof(histogram));
    memset(gaussian, 0, sizeof(gaussian));
    {
        const int64_t square = (int64_t)scale3 * (int64_t)scale3;
        if (square == 0) return;
        coefficient = (uint32_t)(INT64_C(0x200000000000) / square);
    }
    gx_feature_gaussian_weights(gaussian, radius + 1, coefficient);

    orientation_pixels = (const int16_t *)orientation->pixels;
    magnitude_pixels = (const int32_t *)magnitude->pixels;

    for (dy = min_y; dy <= max_y; ++dy) {
        int32_t dx;
        int32_t rotated_y = min_x * sin_step + dy * cos_step;
        int32_t rotated_x = min_x * cos_step - dy * sin_step;
        for (dx = min_x; dx <= max_x; ++dx) {
            if (((rotated_x < 0 ? -rotated_x : rotated_x) < 0x500) &&
                ((rotated_y < 0 ? -rotated_y : rotated_y) < 0x500)) {
                const size_t pixel = (size_t)(y + dy) * (size_t)magnitude->width +
                                     (size_t)(x + dx);
                int32_t relative_angle =
                    GX_ANGLE_HALF - (int32_t)orientation_pixels[pixel] -
                    (int32_t)angle_units;
                const uint32_t abs_x = (uint32_t)(dx < 0 ? -dx : dx);
                const uint32_t abs_y = (uint32_t)(dy < 0 ? -dy : dy);
                int32_t orientation_q12;
                int32_t weight_q9;

                while (relative_angle < 0) relative_angle += GX_ANGLE_PERIOD;
                while (relative_angle >= GX_ANGLE_PERIOD) {
                    relative_angle -= GX_ANGLE_PERIOD;
                }
                orientation_q12 =
                    (int32_t)(((int64_t)relative_angle * INT64_C(0x145f3)) >> 16U);
                weight_q9 = wrap_mul_i32(
                    magnitude_pixels[pixel],
                    (int32_t)gaussian[(size_t)abs_y * (size_t)(radius + 1) + abs_x]);
                gx_feature_descriptor_accumulate(
                    histogram,
                    rotated_x + 0x300,
                    rotated_y + 0x300,
                    orientation_q12,
                    weight_q9);
            }
            rotated_x += cos_step;
            rotated_y += sin_step;
        }
    }

    {
        int32_t out_row;
        for (out_row = 0; out_row < 4; ++out_row) {
            const size_t source =
                (size_t)((out_row + 1) * GX_FEATURE_DESCRIPTOR_GRID_SIDE + 1) *
                GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS;
            memcpy(output + (size_t)out_row * 32U,
                   histogram + source,
                   32U * sizeof(output[0]));
        }
    }
}

void gx_feature_descriptor_rotation_table_init(
    int16_t table[GX_FEATURE_ROTATION_TABLE_SIDE *
                  GX_FEATURE_ROTATION_TABLE_SIDE]) {
    int32_t size;

    if (table == NULL) return;
    table[0] = 1;
    table[1] = 1;
    table[128] = 1;
    table[129] = -1;

    for (size = 2; size < 128; size *= 2) {
        int32_t outer = size;
        int16_t *row_start = table + (size_t)size * 128U;
        int16_t *cursor = row_start;
        while (outer > 0) {
            int32_t inner = size;
            while (inner > 0) {
                const int16_t value = cursor[-(ptrdiff_t)size * 128];
                *cursor = value;
                cursor[-(ptrdiff_t)size * 127] = value;
                cursor[size] = (int16_t)-value;
                ++cursor;
                --inner;
            }
            row_start += 128;
            cursor = row_start;
            --outer;
        }
    }
}
