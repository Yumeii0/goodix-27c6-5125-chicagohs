#include "gx5125/feature_refine.h"

#include <limits.h>

#define GX_PROFILE_SET_ALL UINT32_C(0x07a20c80)
#define GX_PROFILE_SET_SCALE_DIRECT UINT32_C(0x07220c00)
#define GX_SCALE_MULTIPLIER UINT32_C(0x00013333)
#define GX_RESPONSE_MIN INT64_C(0x01478000)

static int profile_in_set(uint32_t profile, uint32_t set) {
    return profile < 27U && ((set >> profile) & 1U) != 0U;
}

static int32_t abs_i32_vendor(int32_t value) {
    if (value >= 0) {
        return value;
    }
    return (int32_t)(UINT32_C(0) - (uint32_t)value);
}

static int32_t round_q12_to_integer_vendor(int32_t value) {
    uint32_t magnitude;
    int32_t rounded;

    if (value < 0) {
        magnitude = UINT32_C(0) - (uint32_t)value;
        rounded = (int32_t)((magnitude >> 12U) +
                            (((magnitude & UINT32_C(0x800)) != 0U) ? 1U : 0U));
        return -rounded;
    }
    magnitude = (uint32_t)value;
    return (int32_t)((magnitude >> 12U) +
                     (((magnitude & UINT32_C(0x800)) != 0U) ? 1U : 0U));
}

uint32_t gx_feature_scale_exp_q16(int32_t input_q16) {
    static const uint32_t thresholds[16] = {
        UINT32_C(0x95c0), UINT32_C(0x526a), UINT32_C(0x2b80), UINT32_C(0x1664),
        UINT32_C(0x0b5d), UINT32_C(0x05ba), UINT32_C(0x02e0), UINT32_C(0x0171),
        UINT32_C(0x00b8), UINT32_C(0x005c), UINT32_C(0x002e), UINT32_C(0x0017),
        UINT32_C(0x000c), UINT32_C(0x0006), UINT32_C(0x0003), UINT32_C(0x0001)
    };
    uint32_t magnitude;
    uint32_t remainder;
    uint64_t value = UINT64_C(0x10000);
    uint32_t integer_part;
    size_t index;

    magnitude = input_q16 < 0
                    ? UINT32_C(0) - (uint32_t)input_q16
                    : (uint32_t)input_q16;
    remainder = magnitude & UINT32_C(0xffff);
    integer_part = magnitude >> 16U;
    if (integer_part != 0U) {
        value <<= (integer_part & 63U);
    }
    for (index = 0U; index < 16U; ++index) {
        if (thresholds[index] <= remainder) {
            remainder -= thresholds[index];
            value += value >> (index + 1U);
        }
    }
    if (input_q16 > 0) {
        return (uint32_t)value;
    }
    if (value == 0U) {
        return 0U;
    }
    return (uint32_t)(UINT64_C(0x100000000) / value);
}

static int validate_level_maps(const gx_feature_u16_map *levels,
                               size_t level_count,
                               int32_t level,
                               int32_t x,
                               int32_t y) {
    const gx_feature_u16_map *reference;
    int offset;

    if (levels == NULL || level < 1 || (size_t)(level + 2) >= level_count) {
        return 0;
    }
    reference = &levels[level];
    if (reference->pixels == NULL || reference->width < 3 ||
        reference->height < 3 || x < 1 || y < 1 ||
        x >= reference->width - 1 || y >= reference->height - 1) {
        return 0;
    }
    for (offset = -1; offset <= 2; ++offset) {
        const gx_feature_u16_map *map = &levels[level + offset];
        if (map->pixels == NULL || map->width != reference->width ||
            map->height != reference->height) {
            return 0;
        }
    }
    return 1;
}

static int32_t abs_wrapped_u32(uint32_t value) {
    const int32_t signed_value = (int32_t)value;
    return signed_value < 0
               ? (int32_t)(UINT32_C(0) - value)
               : signed_value;
}

int gx_feature_candidate_sample_derivatives(const gx_feature_u16_map *levels,
                                            size_t level_count,
                                            int32_t x,
                                            int32_t y,
                                            int32_t level,
                                            int16_t hessian6[6],
                                            int16_t gradient3[3],
                                            int16_t *response_out) {
    const gx_feature_u16_map *previous;
    const gx_feature_u16_map *current;
    const gx_feature_u16_map *next;
    const gx_feature_u16_map *following;
    size_t center;
    int32_t width;
    const uint16_t *p_prev;
    const uint16_t *p_cur;
    const uint16_t *p_next;
    const uint16_t *p_follow;
    uint32_t value;

    if (hessian6 == NULL || gradient3 == NULL || response_out == NULL ||
        !validate_level_maps(levels, level_count, level, x, y)) {
        return -1;
    }
    previous = &levels[level - 1];
    current = &levels[level];
    next = &levels[level + 1];
    following = &levels[level + 2];
    width = current->width;
    center = (size_t)y * (size_t)width + (size_t)x;
    p_prev = previous->pixels + center;
    p_cur = current->pixels + center;
    p_next = next->pixels + center;
    p_follow = following->pixels + center;

    *response_out = (int16_t)((uint16_t)(*p_next - *p_cur) * 2U);

    value = ((((uint32_t)p_cur[-1] - (uint32_t)p_next[-1]) -
              (uint32_t)p_cur[1]) + (uint32_t)p_next[1]) * 2U;
    if (abs_wrapped_u32(value) > 0x7fff) return 0;
    gradient3[0] = (int16_t)value;

    value = ((((uint32_t)p_cur[-width] - (uint32_t)p_next[-width]) -
              (uint32_t)p_cur[width]) + (uint32_t)p_next[width]) * 2U;
    if (abs_wrapped_u32(value) >= 0x8000) return 0;
    gradient3[1] = (int16_t)value;

    value = ((((uint32_t)*p_prev - (uint32_t)*p_cur) -
              (uint32_t)*p_next) + (uint32_t)*p_follow) * 2U;
    if (abs_wrapped_u32(value) >= 0x8000) return 0;
    gradient3[2] = (int16_t)value;

    value = (((((uint32_t)p_next[-1] - (uint32_t)p_cur[-1]) -
               (uint32_t)p_cur[1]) - (int32_t)*response_out) +
             (uint32_t)p_next[1]) * 4U;
    if (abs_wrapped_u32(value) >= 0x8000) return 0;
    hessian6[0] = (int16_t)value;

    value = (((((uint32_t)p_next[-width] - (uint32_t)p_cur[-width]) -
               (int32_t)*response_out) - (uint32_t)p_cur[width]) +
             (uint32_t)p_next[width]) * 4U;
    if (abs_wrapped_u32(value) >= 0x8000) return 0;
    hessian6[1] = (int16_t)value;

    value = (((((uint32_t)*p_cur - (uint32_t)*p_prev) -
               (uint32_t)*p_next) - (int32_t)*response_out) +
             (uint32_t)*p_follow) * 4U;
    if (abs_wrapped_u32(value) >= 0x8000) return 0;
    hessian6[2] = (int16_t)value;

    value = (((((uint32_t)p_cur[width - 1] -
                (uint32_t)p_next[width - 1]) -
               (uint32_t)p_next[1 - width]) -
              (uint32_t)p_cur[width + 1]) -
             (uint32_t)p_cur[-width - 1]) +
            (uint32_t)p_cur[1 - width] +
            (uint32_t)p_next[width + 1] +
            (uint32_t)p_next[-width - 1];
    if (abs_wrapped_u32(value) >= 0x2000) return 0;
    hessian6[3] = (int16_t)value;

    value = ((((((uint32_t)p_prev[width] - (uint32_t)p_prev[-width]) -
                (uint32_t)p_follow[-width]) + (uint32_t)p_follow[width]) -
              (uint32_t)p_cur[width]) - (uint32_t)p_next[width]) +
            (uint32_t)p_cur[-width] + (uint32_t)p_next[-width];
    if (abs_wrapped_u32(value) >= 0x2000) return 0;
    hessian6[4] = (int16_t)value;

    value = ((((((uint32_t)p_prev[1] - (uint32_t)p_prev[-1]) -
                (uint32_t)p_follow[-1]) + (uint32_t)p_follow[1]) -
              (uint32_t)p_cur[1]) - (uint32_t)p_next[1]) +
            (uint32_t)p_cur[-1] + (uint32_t)p_next[-1];
    if (abs_wrapped_u32(value) >= 0x2000) return 0;
    hessian6[5] = (int16_t)value;
    return 1;
}

int gx_feature_candidate_refine(const gx_feature_u16_map *levels,
                                size_t level_count,
                                gx_feature_candidate_state *candidate,
                                int32_t *curvature_out,
                                int32_t edge_threshold,
                                uint32_t profile) {
    int32_t x;
    int32_t y;
    int32_t level;
    int32_t max_level = 3;
    int32_t scale_divisor = 3;
    int32_t special_response_mode = 0;
    int iteration = 0;

    if (levels == NULL || candidate == NULL || curvature_out == NULL ||
        edge_threshold < 0 || level_count < 4U) {
        return -1;
    }
    x = candidate->x;
    y = candidate->y;
    level = candidate->level;

    if (profile == 9U || profile == 18U) {
        special_response_mode = 1;
        scale_divisor = 6;
        max_level = 6;
    } else if (profile_in_set(profile, GX_PROFILE_SET_ALL)) {
        scale_divisor = 4;
        max_level = 6;
    }

    for (;;) {
        int16_t hessian[6];
        int16_t gradient[3];
        int16_t response;
        int32_t offsets[3];
        int32_t determinant;
        int32_t trace;
        int64_t dot;
        int64_t interpolated;
        int32_t x_offset;
        int32_t y_offset;
        int32_t level_offset;
        int64_t scale_input;
        uint32_t scale;

        if (!validate_level_maps(levels, level_count, level, x, y)) {
            return 0;
        }
        if (gx_feature_candidate_sample_derivatives(levels, level_count, x, y,
                                                    level, hessian,
                                                    gradient, &response) != 1) {
            return 0;
        }
        if (gx_feature_candidate_solve_q12(hessian, gradient, offsets) != 1) {
            return 0;
        }
        x_offset = offsets[0];
        y_offset = offsets[1];
        level_offset = offsets[2];

        if (abs_i32_vendor(x_offset) < 0x800 &&
            abs_i32_vendor(y_offset) < 0x800 &&
            abs_i32_vendor(level_offset) < 0x800) {
            dot = (int64_t)gradient[0] * x_offset +
                  (int64_t)gradient[2] * level_offset +
                  (int64_t)gradient[1] * y_offset;
            if (profile_in_set(profile, GX_PROFILE_SET_ALL)) {
                interpolated = (int64_t)response * INT64_C(0x2000) + dot;
                if (interpolated <= 0) {
                    interpolated = -((int64_t)response * INT64_C(0x2000) + dot);
                }
            } else {
                interpolated = (int64_t)response * INT64_C(0x4000) + dot;
                if (interpolated <= 0) {
                    interpolated = -((int64_t)response * INT64_C(0x4000) + dot);
                }
            }
            if (interpolated < GX_RESPONSE_MIN) {
                return 0;
            }
            if (special_response_mode == 0 || profile == 7U || profile == 23U) {
                candidate->response = (int32_t)(interpolated >> 12U);
            }

            trace = (int32_t)hessian[1] + (int32_t)hessian[0];
            determinant = (int32_t)hessian[1] * (int32_t)hessian[0] -
                          (int32_t)hessian[3] * (int32_t)hessian[3];
            if (profile_in_set(profile, GX_PROFILE_SET_ALL)) {
                if (determinant < 1) return 0;
            } else {
                const int64_t lhs = (int64_t)determinant *
                                    (int64_t)(edge_threshold + 1) *
                                    (int64_t)(edge_threshold + 1);
                const int64_t rhs = (int64_t)edge_threshold *
                                    (int64_t)trace * (int64_t)trace;
                if (determinant < 1 || lhs <= rhs) return 0;
            }

            scale_input = ((int64_t)level * 0x1000 + level_offset) * 0x10;
            candidate->x_q8 = (int16_t)(((int64_t)x * 0x1000 + x_offset) >> 4U);
            candidate->y_q8 = (int16_t)(((int64_t)y * 0x1000 + y_offset) >> 4U);
            *curvature_out = (int32_t)(((int64_t)trace * trace * 0x400) /
                                       determinant);
            scale = gx_feature_scale_exp_q16((int32_t)(scale_input / scale_divisor));
            if (profile_in_set(profile, GX_PROFILE_SET_SCALE_DIRECT)) {
                candidate->scale_q16 = (int32_t)scale;
            } else if (profile == 7U || profile == 23U) {
                candidate->scale_q16 = scale > INT32_MAX
                                           ? INT32_MAX
                                           : (int32_t)scale;
            } else {
                candidate->scale_q16 =
                    (int32_t)(((uint64_t)scale * GX_SCALE_MULTIPLIER) >> 16U);
            }
            candidate->x = x;
            candidate->y = y;
            candidate->level = level;
            return 1;
        }

        x += round_q12_to_integer_vendor(x_offset);
        y += round_q12_to_integer_vendor(y_offset);
        level += round_q12_to_integer_vendor(level_offset);
        candidate->x = x;
        candidate->y = y;
        candidate->level = level;

        if (level < 1 || level > max_level ||
            (size_t)(level + 2) >= level_count) {
            return 0;
        }
        if (x < 1 || y < 1 || x >= levels[level].width - 1 ||
            y >= levels[level].height - 1) {
            return 0;
        }
        ++iteration;
        if (iteration > 4) {
            return 0;
        }
    }
}
