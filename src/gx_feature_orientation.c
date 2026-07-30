#include "gx5125/feature_orientation.h"

#include <limits.h>
#include <string.h>

#define GX_ANGLE_PERIOD INT32_C(0x6488)
#define GX_ANGLE_HALF INT32_C(0x3244)
#define GX_ORIENTATION_FINE_PERIOD INT32_C(0x4800)

static int32_t wrap_mul_i32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

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

static int32_t radius_from_scale(int32_t scale_q16) {
    const int32_t multiplied = wrap_mul_i32(scale_q16, 18);
    const int32_t quarter = arithmetic_shift_right_i32(multiplied, 2U);
    uint32_t magnitude;
    int32_t rounded;

    if (quarter < 0) {
        magnitude = UINT32_C(0) - (uint32_t)quarter;
        rounded = (int32_t)((magnitude >> 16U) +
                            (((magnitude & UINT32_C(0x8000)) != 0U) ? 1U : 0U));
        return -rounded;
    }
    magnitude = (uint32_t)quarter;
    return (int32_t)((magnitude >> 16U) +
                     (((magnitude & UINT32_C(0x8000)) != 0U) ? 1U : 0U));
}

static uint32_t gaussian_coefficient_from_scale(int32_t scale_q16) {
    const int32_t multiplied = wrap_mul_i32(scale_q16, 6);
    const int32_t sigma_q16 = arithmetic_shift_right_i32(multiplied, 2U);
    const int64_t sigma = (int64_t)sigma_q16;
    const uint64_t denominator = (uint64_t)(sigma * sigma);

    if (denominator == 0U) return 0U;
    return (uint32_t)(UINT64_C(0x800000000000) / denominator);
}

void gx_feature_gaussian_weights(uint32_t *weights,
                                 int32_t side,
                                 uint32_t coefficient) {
    int32_t row;

    if (weights == NULL || side <= 0) return;
    for (row = 0; row < side; ++row) {
        int32_t column;
        for (column = row; column < side; ++column) {
            uint32_t radius2 = (uint32_t)(row * row + column * column);
            uint32_t scaled = radius2 * coefficient;
            uint32_t value;

            if (scaled < UINT32_C(0x6ee76)) {
                const uint32_t shifted = scaled >> 8U;
                const uint32_t square = shifted * shifted;
                uint32_t polynomial = square >> 8U;
                const uint32_t term_a = (scaled / 5U + UINT32_C(0x10000)) >> 8U;
                const uint32_t term_b = (scaled / 6U + UINT32_C(0x8000)) >> 8U;

                polynomial = (((polynomial * polynomial) >> 8U) * term_a) /
                                 UINT32_C(0x0c00) +
                             UINT32_C(0x200) +
                             ((term_b * square) >> 15U) +
                             (scaled >> 7U);
                value = (((polynomial >> 1U) + UINT32_C(0x40000)) /
                         polynomial);
            } else {
                value = 0U;
            }
            weights[(size_t)row * (size_t)side + (size_t)column] = value;
            weights[(size_t)column * (size_t)side + (size_t)row] = value;
        }
    }
}

static int32_t clamp_min_i32(int32_t a, int32_t b) {
    return a < b ? b : a;
}

static int32_t clamp_max_i32(int32_t a, int32_t b) {
    return a > b ? b : a;
}

uint32_t gx_feature_orientation_histogram(
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    uint32_t histogram[GX_FEATURE_ORIENTATION_BINS],
    int32_t *peak_bin,
    int32_t duplicate_opposite) {
    uint32_t raw[GX_FEATURE_ORIENTATION_BINS];
    uint32_t weights[33U * 33U];
    const int32_t *magnitude_pixels;
    const int16_t *orientation_pixels;
    int32_t radius;
    int32_t side;
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int32_t dy;
    uint32_t maximum;
    int32_t maximum_index;
    uint32_t coefficient;
    size_t bin;

    if (orientation == NULL || magnitude == NULL || histogram == NULL ||
        (duplicate_opposite != 0 && peak_bin == NULL) ||
        orientation->pixels == NULL ||
        magnitude->pixels == NULL || magnitude->width <= 2 ||
        magnitude->height <= 2 || orientation->width != magnitude->width ||
        orientation->height != magnitude->height) {
        return 0U;
    }
    radius = radius_from_scale(scale_q16);
    if (radius < 0) radius = -radius;
    if (radius > 32) radius = 32;
    side = radius + 1;
    coefficient = gaussian_coefficient_from_scale(scale_q16);
    memset(raw, 0, sizeof(raw));
    memset(histogram, 0, GX_FEATURE_ORIENTATION_BINS * sizeof(histogram[0]));
    gx_feature_gaussian_weights(weights, side, coefficient);

    min_y = clamp_min_i32(-radius, 1 - y);
    max_y = clamp_max_i32(radius, magnitude->height - y - 2);
    min_x = clamp_min_i32(-radius, 1 - x);
    max_x = clamp_max_i32(radius, magnitude->width - x - 2);
    magnitude_pixels = (const int32_t *)magnitude->pixels;
    orientation_pixels = (const int16_t *)orientation->pixels;

    for (dy = min_y; dy <= max_y; ++dy) {
        int32_t dx;
        for (dx = min_x; dx <= max_x; ++dx) {
            const size_t index = (size_t)(y + dy) * (size_t)magnitude->width +
                                 (size_t)(x + dx);
            const int32_t angle = (int32_t)orientation_pixels[index];
            int32_t orientation_bin =
                (angle * -36 + INT32_C(0x743d4)) / GX_ANGLE_PERIOD;
            const uint32_t abs_x = (uint32_t)(dx < 0 ? -dx : dx);
            const uint32_t abs_y = (uint32_t)(dy < 0 ? -dy : dy);
            const uint32_t weight = weights[(size_t)abs_y * (size_t)side + abs_x];
            const uint32_t weighted =
                ((uint32_t)magnitude_pixels[index] * weight) >> 8U;

            if (orientation_bin >= (int32_t)GX_FEATURE_ORIENTATION_BINS) {
                orientation_bin = 0;
            }
            raw[(size_t)orientation_bin] += weighted;
            if (duplicate_opposite != 0) {
                raw[(size_t)((orientation_bin + 18) % 36)] += weighted;
            }
        }
    }

    for (bin = 0U; bin < GX_FEATURE_ORIENTATION_BINS; ++bin) {
        const size_t m2 = (bin + 34U) % 36U;
        const size_t m1 = (bin + 35U) % 36U;
        const size_t p1 = (bin + 1U) % 36U;
        const size_t p2 = (bin + 2U) % 36U;
        histogram[bin] =
            (raw[m2] + raw[m1] * 4U + raw[bin] * 6U +
             raw[p1] * 4U + raw[p2]) >> 4U;
    }

    maximum = histogram[0];
    maximum_index = 0;
    for (bin = 1U; bin < GX_FEATURE_ORIENTATION_BINS; ++bin) {
        if (maximum < histogram[bin]) {
            maximum = histogram[bin];
            maximum_index = (int32_t)bin;
        }
    }
    if (duplicate_opposite != 0) {
        maximum_index += 18;
        *peak_bin = maximum_index;
    }
    return maximum;
}

static int16_t read_i16(const uint8_t *data, size_t offset) {
    int16_t value;
    memcpy(&value, data + offset, sizeof(value));
    return value;
}

static void write_i16(uint8_t *data, size_t offset, int16_t value) {
    memcpy(data + offset, &value, sizeof(value));
}

static void write_i32(uint8_t *data, size_t offset, int32_t value) {
    memcpy(data + offset, &value, sizeof(value));
}

void gx_feature_append_orientations(
    const uint8_t *gray_center,
    const gx_feature_refined_candidate *candidate,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *record_count,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t max_records,
    uint32_t profile) {
    uint32_t histogram[GX_FEATURE_ORIENTATION_BINS];
    uint32_t histogram_max;
    uint32_t threshold = 0U;
    uint32_t count;
    int32_t peak_bin = 0;
    int special;
    int32_t scan_bin = 0;
    uint32_t center_sum;

    if (gray_center == NULL || candidate == NULL || records == NULL ||
        rank_pairs == NULL || metadata == NULL || record_count == NULL ||
        magnitude == NULL || orientation == NULL || max_records <= 0 ||
        magnitude->width <= 0) {
        return;
    }
    count = *record_count;
    special = (profile == 9U || profile == 18U);
    center_sum = (uint32_t)gray_center[0] +
                 (uint32_t)gray_center[magnitude->width] +
                 (uint32_t)gray_center[-magnitude->width] +
                 (uint32_t)gray_center[-1] +
                 (uint32_t)gray_center[1];
    histogram_max = gx_feature_orientation_histogram(
        candidate->x, candidate->y, candidate->scale_q16,
        orientation, magnitude, histogram, &peak_bin, !special);
    if (special) {
        threshold = (uint32_t)(((uint64_t)histogram_max * UINT64_C(0xcccd) +
                                UINT64_C(0x8000)) >> 16U);
    }

    while (scan_bin < 36) {
        int32_t bin = special ? scan_bin : peak_bin;
        int32_t previous_bin = bin == 0 ? 35 : bin - 1;
        int32_t next_bin = (bin + 1) % 36;
        const uint32_t current = histogram[(size_t)bin];
        const uint32_t previous = histogram[(size_t)previous_bin];
        const uint32_t next = histogram[(size_t)next_bin];
        int accept = !special;

        ++scan_bin;
        if (special && previous < current && next < current &&
            threshold <= current) {
            accept = 1;
        }
        if (accept) {
            const int64_t denominator =
                (int64_t)previous * 2 - (int64_t)current * 4 +
                (int64_t)next * 2;
            int32_t fine_angle = bin * 0x200;
            int16_t angle;
            int32_t response_abs = candidate->response;
            int32_t signed_metric;
            uint8_t *record;
            int32_t *rank;
            int32_t *meta;

            if (denominator != 0) {
                const int64_t numerator =
                    ((int64_t)previous - (int64_t)next) * 0x200 -
                    arithmetic_shift_right_i64(denominator, 1U);
                fine_angle += (int32_t)(numerator / denominator);
            }
            while (fine_angle < 0) fine_angle += GX_ORIENTATION_FINE_PERIOD;
            while (fine_angle > GX_ORIENTATION_FINE_PERIOD - 1) {
                fine_angle -= GX_ORIENTATION_FINE_PERIOD;
            }
            angle = (int16_t)(((fine_angle * GX_ANGLE_PERIOD) / 36) >> 9);
            angle = (int16_t)(angle - GX_ANGLE_HALF);
            if (!special && angle < 0) {
                angle = (int16_t)(angle + GX_ANGLE_HALF);
            }
            if (response_abs < 1) response_abs = -response_abs;
            signed_metric = -response_abs;

            if ((int32_t)count >= max_records) break;
            record = records + (size_t)count * GX_FEATURE_RECORD_BYTES;
            rank = rank_pairs + (size_t)count * 2U;
            meta = metadata + (size_t)count * 6U;

            write_i16(record, 0x00U, (int16_t)(center_sum > 0x27fU));
            write_i16(record, 0x02U, candidate->x_q8);
            write_i16(record, 0x04U, candidate->y_q8);
            write_i16(record, 0x06U, angle);
            write_i32(record, 0x08U, signed_metric);
            rank[0] = signed_metric;
            rank[1] = (int32_t)count;
            meta[0] = candidate->x;
            meta[1] = candidate->y;
            meta[2] = candidate->scale_q16;
            meta[3] = (int32_t)histogram_max;
            meta[4] = (int32_t)current;
            {
                const uint16_t distinct = (uint16_t)(current != histogram_max);
                memcpy((uint8_t *)meta + 20U, &distinct, sizeof(distinct));
            }
            ++count;
            if ((int32_t)count >= max_records || !special) break;
        }
    }
    *record_count = count;
    (void)read_i16;
}
