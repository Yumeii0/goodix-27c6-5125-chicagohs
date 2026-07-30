#include "gx5125/feature_primitives.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int compare_i16(const void *left, const void *right) {
    const int16_t a = *(const int16_t *)left;
    const int16_t b = *(const int16_t *)right;
    return (a > b) - (a < b);
}

int16_t gx_feature_signed_median_i16(const int16_t *values, size_t count) {
    int16_t copy[128];
    size_t usable = count;

    if (values == NULL || count == 0U) {
        return 0;
    }
    if (usable > 128U) {
        usable = 128U;
    }
    memcpy(copy, values, usable * sizeof(copy[0]));
    qsort(copy, usable, sizeof(copy[0]), compare_i16);
    return copy[(usable - 1U) / 2U];
}

void gx_feature_build_tail_bits(uint32_t out_words[4],
                                const int16_t *values,
                                size_t value_count,
                                int32_t rows,
                                int32_t channels) {
    int16_t working[128];
    int16_t median;
    int32_t row;

    if (out_words == NULL || values == NULL || rows <= 0 || channels <= 0) {
        return;
    }
    if ((int64_t)rows * (int64_t)channels > (int64_t)value_count ||
        value_count > 128U) {
        return;
    }

    memset(working, 0, sizeof(working));
    memcpy(working, values, value_count * sizeof(working[0]));
    median = gx_feature_signed_median_i16(working, value_count);

    for (row = 0; row < rows; ++row) {
        size_t index;
        if (channels == 1) {
            index = (size_t)row;
        } else {
            index = (size_t)row * (size_t)channels + 1U;
        }
        if (values[index] > median) {
            out_words[(uint32_t)row >> 5U] |=
                UINT32_C(1) << ((uint32_t)row & 31U);
        }
    }
}

void gx_feature_build_correlation_word(uint8_t record[0x3c],
                                       const int16_t reference[32],
                                       const int16_t bank[32 * 128]) {
    uint32_t word = 0U;
    uint32_t bit = 1U;
    size_t block;

    if (record == NULL || reference == NULL || bank == NULL) {
        return;
    }
    memset(record + 0x28U, 0, 0x10U);

    for (block = 0U; block < 32U; ++block) {
        uint32_t accumulator = 0U;
        size_t element;
        for (element = 0U; element < 32U; ++element) {
            const int32_t product =
                (int32_t)reference[element] *
                (int32_t)bank[block * 128U + element];
            accumulator += (uint32_t)product;
        }
        if ((int32_t)accumulator > 0) {
            word |= bit;
        }
        bit = (bit << 1U) | (bit >> 31U);
    }
    memcpy(record + 0x28U, &word, sizeof(word));
}

static int32_t wrap_add_i32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a + (uint32_t)b);
}


static int32_t arithmetic_shift_right_i32(int32_t value, unsigned shift) {
    if (shift == 0U) {
        return value;
    }
    if (value >= 0) {
        return value >> shift;
    }
    return (int32_t)(~((uint32_t)~value >> shift));
}

static int16_t wrap_i16(int32_t value) {
    return (int16_t)(uint16_t)value;
}

void gx_feature_angle_sincos_q14(int32_t angle_units,
                                 int32_t *out_sin_q14,
                                 int32_t *out_cos_q14) {
    static const int16_t cordic_angles[13] = {
        3217, 1899, 1003, 509, 256, 128, 64, 32, 16, 8, 4, 2, 1
    };
    int64_t absolute;
    int32_t normalized_abs;
    int32_t normalized;
    int32_t folded;
    int16_t cosine = INT16_C(0x4000);
    int16_t sine = 0;
    int32_t current = 0;
    int index;
    int32_t sin_out;
    int32_t cos_out;

    if (out_sin_q14 == NULL || out_cos_q14 == NULL) {
        return;
    }

    absolute = angle_units;
    if (absolute < 0) {
        absolute = -absolute;
    }
    normalized_abs = (int32_t)(absolute % GX_FEATURE_ANGLE_PERIOD);

    if (normalized_abs < GX_FEATURE_ANGLE_HALF + 1) {
        normalized = (angle_units < 1) ? -normalized_abs : normalized_abs;
    } else if (angle_units < 0) {
        normalized = GX_FEATURE_ANGLE_PERIOD - normalized_abs;
    } else {
        normalized = normalized_abs - GX_FEATURE_ANGLE_PERIOD;
    }

    if (normalized < GX_FEATURE_ANGLE_QUARTER + 1) {
        if (normalized < -GX_FEATURE_ANGLE_QUARTER) {
            folded = normalized + GX_FEATURE_ANGLE_HALF;
        } else {
            folded = normalized < 0 ? -normalized : normalized;
        }
    } else {
        folded = GX_FEATURE_ANGLE_HALF - normalized;
    }

    if (folded == 0) {
        sine = 0;
        cosine = INT16_C(0x4000);
    } else if (folded == GX_FEATURE_ANGLE_QUARTER) {
        sine = INT16_C(0x4000);
        cosine = 0;
    } else {
        for (index = 0; index < 13; ++index) {
            int16_t cosine_step =
                wrap_i16(arithmetic_shift_right_i32(cosine, (unsigned)index));
            int16_t sine_step =
                wrap_i16(arithmetic_shift_right_i32(sine, (unsigned)index));
            if (folded - current < 0) {
                cosine_step = wrap_i16(-(int32_t)cosine_step);
                current -= cordic_angles[index];
            } else {
                sine_step = wrap_i16(-(int32_t)sine_step);
                current += cordic_angles[index];
            }
            sine = wrap_i16((int32_t)sine + cosine_step);
            cosine = wrap_i16((int32_t)cosine + sine_step);
        }
        cos_out = ((int32_t)cosine * INT32_C(0x9b75) + INT32_C(0x8000)) >> 16;
        sin_out = ((int32_t)sine * INT32_C(0x9b75) + INT32_C(0x8000)) >> 16;
        cosine = wrap_i16(cos_out);
        sine = wrap_i16(sin_out);
    }

    sin_out = sine;
    cos_out = cosine;
    if (normalized < GX_FEATURE_ANGLE_QUARTER + 1) {
        if (normalized < -GX_FEATURE_ANGLE_QUARTER) {
            cos_out = -cos_out;
        } else if (normalized >= 0) {
            *out_sin_q14 = sin_out;
            *out_cos_q14 = cos_out;
            return;
        }
        sin_out = -sin_out;
    } else {
        cos_out = -cos_out;
    }

    *out_sin_q14 = sin_out;
    *out_cos_q14 = cos_out;
}

int gx_feature_descriptor_quality_flag(const int32_t values[128],
                                       int use_mean_gate,
                                       int32_t profile) {
    uint32_t group_mean[16];
    uint32_t mean_sum = 0U;
    int32_t global_mean;
    int low_divisor = 25;
    int high_divisor = 8;
    int low_count = 0;
    int high_count = 0;
    size_t group;

    if (values == NULL) {
        return 0;
    }
    if (profile == 7 || profile == 23 || profile == 10) {
        low_divisor = 30;
        high_divisor = 10;
    }

    for (group = 0U; group < 16U; ++group) {
        uint32_t sum = 0U;
        size_t element;
        for (element = 0U; element < 8U; ++element) {
            sum += (uint32_t)values[group * 8U + element];
        }
        group_mean[group] = sum >> 3U;
        mean_sum += group_mean[group];
    }
    global_mean = (int32_t)(mean_sum >> 4U);

    for (group = 0U; group < 16U; ++group) {
        const int32_t local_mean = (int32_t)group_mean[group];
        if (!use_mean_gate || global_mean <= wrap_add_i32(local_mean, wrap_add_i32(local_mean, local_mean))) {
            uint32_t variance_sum = 0U;
            size_t element;
            for (element = 0U; element < 8U; ++element) {
                const int64_t difference =
                    (int64_t)values[group * 8U + element] - (int64_t)local_mean;
                variance_sum += (uint32_t)(((uint64_t)(difference * difference)) >> 16U);
            }
            {
                const int32_t variance = (int32_t)(variance_sum / 7U);
                if (variance < global_mean / low_divisor) {
                    ++low_count;
                }
                if (global_mean / high_divisor < variance) {
                    ++high_count;
                }
            }
        }
    }

    if (((low_count < 4) || (high_count > 7)) && (low_count < 6)) {
        return 0;
    }
    return 1;
}
