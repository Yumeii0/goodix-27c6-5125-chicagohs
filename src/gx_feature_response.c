#include "gx5125/feature_response.h"

#include <stddef.h>

static int32_t wrap_add_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t wrap_sub_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t wrap_neg_i32(int32_t value) {
    return (int32_t)(UINT32_C(0) - (uint32_t)value);
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

static int32_t clamp_gradient(int32_t value) {
    if (value < INT32_C(-0x80000)) {
        return INT32_C(-0x80000);
    }
    if (value > INT32_C(0x7ffff)) {
        return INT32_C(0x7ffff);
    }
    return value;
}

uint16_t gx_feature_vector_angle_magnitude(int32_t vertical,
                                           int32_t horizontal,
                                           int32_t *out_magnitude) {
    static const int16_t cordic_angles[13] = {
        3217, 1899, 1003, 509, 256, 128, 64, 32, 16, 8, 4, 2, 1
    };
    static const int32_t cordic_scale[13] = {
        46341, 41449, 40211, 39901, 39823, 39803, 39799,
        39797, 39797, 39797, 39797, 39797, 39797
    };
    const int32_t original_vertical = vertical;
    const int32_t original_horizontal = horizontal;
    int32_t abs_horizontal = horizontal;
    int32_t abs_vertical = vertical;
    uint16_t angle = 0U;
    unsigned last_iteration = 0U;
    unsigned iteration;

    if (out_magnitude == NULL) {
        return 0U;
    }

    if (abs_horizontal < 1) {
        abs_horizontal = wrap_neg_i32(abs_horizontal);
    }
    if (abs_vertical < 1) {
        abs_vertical = wrap_neg_i32(abs_vertical);
    }

    if (abs_vertical == 0) {
        *out_magnitude = abs_horizontal;
        if (original_horizontal < 1) {
            angle = (uint16_t)GX_FEATURE_VECTOR_ANGLE_HALF;
        }
        return angle;
    }
    if (abs_horizontal == 0) {
        *out_magnitude = abs_vertical;
        if (original_vertical < 1) {
            return UINT16_C(0xe6de);
        }
        return (uint16_t)GX_FEATURE_VECTOR_ANGLE_QUARTER;
    }

    for (iteration = 0U; iteration < 13U; ++iteration) {
        int32_t vertical_step = arithmetic_shift_right_i32(abs_vertical, iteration);
        int32_t horizontal_step = arithmetic_shift_right_i32(abs_horizontal, iteration);
        int16_t angle_step;

        if (abs_vertical < 1) {
            vertical_step = wrap_neg_i32(vertical_step);
            angle_step = (int16_t)-cordic_angles[iteration];
        } else {
            horizontal_step = wrap_neg_i32(horizontal_step);
            angle_step = cordic_angles[iteration];
        }

        abs_vertical = wrap_add_i32(abs_vertical, horizontal_step);
        abs_horizontal = wrap_add_i32(abs_horizontal, vertical_step);
        angle = (uint16_t)(angle + (uint16_t)angle_step);
        last_iteration = iteration;
        if (abs_vertical == 0) {
            break;
        }
    }

    if (original_horizontal < 1) {
        if (original_vertical < 1) {
            angle = (uint16_t)(angle + UINT16_C(0xcdbc));
        } else {
            angle = (uint16_t)(UINT16_C(0x3244) - angle);
        }
    } else if (original_vertical < 0) {
        angle = (uint16_t)(UINT16_C(0) - angle);
    }

    *out_magnitude = (int32_t)(((int64_t)cordic_scale[last_iteration] *
                                (int64_t)abs_horizontal + INT64_C(0x8000)) >> 16);
    return angle;
}

int gx_feature_build_response_map(const int32_t *source,
                                  int32_t width,
                                  int32_t height,
                                  int32_t *magnitude,
                                  int16_t *angle) {
    int32_t y;

    if (source == NULL || magnitude == NULL || angle == NULL ||
        width < 3 || height < 3) {
        return -1;
    }

    for (y = 1; y < height - 1; ++y) {
        int32_t x;
        for (x = 1; x < width - 1; ++x) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            int32_t horizontal = wrap_sub_i32(source[index + 1U], source[index - 1U]);
            int32_t vertical = wrap_sub_i32(source[index + (size_t)width],
                                            source[index - (size_t)width]);
            int32_t cordic_magnitude = 0;
            int32_t output_magnitude;

            horizontal = clamp_gradient(horizontal);
            vertical = clamp_gradient(vertical);
            horizontal = (int32_t)((uint32_t)horizontal << 12U);
            vertical = (int32_t)((uint32_t)vertical << 12U);

            angle[index] = (int16_t)gx_feature_vector_angle_magnitude(
                vertical, horizontal, &cordic_magnitude);
            output_magnitude = arithmetic_shift_right_i32(cordic_magnitude, 12U);
            if (output_magnitude > INT32_C(0x3ffff)) {
                output_magnitude = INT32_C(0x3ffff);
            }
            magnitude[index] = output_magnitude;
        }
    }
    return 0;
}
