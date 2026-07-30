#include "gx5125/feature_candidate.h"

#include <limits.h>

static int32_t signed_difference_u16(uint16_t left, uint16_t right) {
    return (int32_t)left - (int32_t)right;
}

static int32_t center_response_i16(uint16_t next, uint16_t current) {
    const uint16_t wrapped = (uint16_t)(next - current);
    return (int32_t)(int16_t)wrapped;
}

static int64_t abs_i64_no_overflow(int64_t value) {
    if (value >= 0) {
        return value;
    }
    /* The candidate solver inputs cannot generate INT64_MIN, but avoid signed
     * negation UB anyway. */
    return (int64_t)(UINT64_C(0) - (uint64_t)value);
}

static unsigned vendor_bit_counter(int64_t value) {
    uint64_t magnitude;
    unsigned count = 1U;

    if (value < 0) {
        magnitude = UINT64_C(0) - (uint64_t)value;
    } else {
        magnitude = (uint64_t)value;
    }
    while (magnitude != 0U) {
        ++count;
        magnitude >>= 1U;
    }
    return count;
}

static int64_t arithmetic_shift_right_i64(int64_t value, unsigned shift) {
    if (shift == 0U) {
        return value;
    }
    if (shift >= 64U) {
        return value < 0 ? -1 : 0;
    }
    if (value >= 0) {
        return (int64_t)((uint64_t)value >> shift);
    }
    return (int64_t)~((uint64_t)~value >> shift);
}

int gx_feature_candidate_extremum_gate(const gx_feature_u16_map *levels,
                                       size_t level_count,
                                       int32_t x,
                                       int32_t y,
                                       int32_t level,
                                       int32_t threshold,
                                       int32_t *response_out) {
    const gx_feature_u16_map *previous;
    const gx_feature_u16_map *current;
    const gx_feature_u16_map *next;
    const gx_feature_u16_map *following;
    size_t center;
    int32_t response;
    int32_t dy;

    if (levels == NULL || response_out == NULL || level < 1 ||
        (size_t)(level + 2) >= level_count) {
        return -1;
    }
    previous = &levels[level - 1];
    current = &levels[level];
    next = &levels[level + 1];
    following = &levels[level + 2];
    if (previous->pixels == NULL || current->pixels == NULL ||
        next->pixels == NULL || following->pixels == NULL ||
        current->width < 3 || current->height < 3 ||
        previous->width != current->width || next->width != current->width ||
        following->width != current->width ||
        previous->height != current->height || next->height != current->height ||
        following->height != current->height ||
        x < 1 || y < 1 || x >= current->width - 1 ||
        y >= current->height - 1) {
        return -1;
    }

    center = (size_t)y * (size_t)current->width + (size_t)x;
    response = center_response_i16(next->pixels[center], current->pixels[center]);
    *response_out = response;

    {
        const int64_t magnitude = abs_i64_no_overflow((int64_t)response);
        if ((int64_t)threshold >= magnitude) {
            return 0;
        }
    }
    if (response == 0) {
        return 0;
    }

    for (dy = -1; dy <= 1; ++dy) {
        int32_t dx;
        for (dx = -1; dx <= 1; ++dx) {
            const size_t index = (size_t)(y + dy) * (size_t)current->width +
                                 (size_t)(x + dx);
            int32_t value;

            if (dx != 0 || dy != 0) {
                value = signed_difference_u16(next->pixels[index],
                                              current->pixels[index]);
                if ((response > 0 && value > response) ||
                    (response < 0 && value < response)) {
                    return 0;
                }
            }

            value = signed_difference_u16(following->pixels[index],
                                          next->pixels[index]);
            if ((response > 0 && value > response) ||
                (response < 0 && value < response)) {
                return 0;
            }

            value = signed_difference_u16(current->pixels[index],
                                          previous->pixels[index]);
            if ((response > 0 && value > response) ||
                (response < 0 && value < response)) {
                return 0;
            }
        }
    }
    return 1;
}

int gx_feature_candidate_solve_q12(const int16_t hessian6[6],
                                   const int16_t gradient3[3],
                                   int32_t output3[3]) {
    int64_t cof0;
    int64_t cof1;
    int64_t cof2;
    int64_t determinant;
    int64_t mixed;
    int64_t numerator[3];
    unsigned determinant_bits;
    size_t index;

    if (hessian6 == NULL || gradient3 == NULL || output3 == NULL) {
        return -1;
    }

    cof0 = (int64_t)hessian6[3] * hessian6[4] -
           (int64_t)hessian6[5] * hessian6[1];
    cof1 = (int64_t)hessian6[5] * hessian6[4] -
           (int64_t)hessian6[3] * hessian6[2];
    cof2 = (int64_t)hessian6[1] * hessian6[2] -
           (int64_t)hessian6[4] * hessian6[4];
    determinant = (int64_t)hessian6[0] * cof2 +
                  (int64_t)hessian6[3] * cof1 +
                  (int64_t)hessian6[5] * cof0;

    if (determinant == 0) {
        output3[0] = 0;
        output3[1] = 0;
        output3[2] = 0;
        return 1;
    }

    mixed = (int64_t)hessian6[3] * hessian6[5] -
            (int64_t)hessian6[0] * hessian6[4];
    numerator[0] = -((int64_t)gradient3[0] * cof2 +
                     (int64_t)gradient3[1] * cof1 +
                     (int64_t)gradient3[2] * cof0);
    numerator[1] = -(((int64_t)hessian6[0] * hessian6[2] -
                      (int64_t)hessian6[5] * hessian6[5]) * gradient3[1] +
                     mixed * gradient3[2] +
                     (int64_t)gradient3[0] * cof1);
    numerator[2] = -(((int64_t)hessian6[0] * hessian6[1] -
                      (int64_t)hessian6[3] * hessian6[3]) * gradient3[2] +
                     mixed * gradient3[1] +
                     (int64_t)gradient3[0] * cof0);

    determinant_bits = vendor_bit_counter(determinant);
    for (index = 0U; index < 3U; ++index) {
        const unsigned numerator_bits = vendor_bit_counter(numerator[index]);
        if ((int)numerator_bits - (int)determinant_bits >= 9) {
            output3[index] = INT32_C(0x00ffffff);
        } else {
            const unsigned common_bits = numerator_bits < determinant_bits
                                             ? determinant_bits
                                             : numerator_bits;
            const unsigned shift = common_bits > 32U ? common_bits - 32U : 0U;
            const int64_t scaled_numerator =
                arithmetic_shift_right_i64(numerator[index], shift);
            const int64_t scaled_determinant =
                arithmetic_shift_right_i64(determinant, shift);
            const int64_t quotient =
                (scaled_numerator * INT64_C(4096)) / scaled_determinant;
            output3[index] = (int32_t)(uint32_t)quotient;
        }
    }
    return 1;
}
