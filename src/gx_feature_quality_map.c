#include "gx5125/feature_quality_map.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int gx_valid_image(int32_t width, int32_t height) {
    return width > 0 && height > 0 &&
           (size_t)height <= SIZE_MAX / (size_t)width;
}

int gx_feature_quality_gradient(const uint8_t *gray,
                                const uint8_t *mask,
                                int32_t width,
                                int32_t height,
                                int32_t gray_stride,
                                int32_t mask_stride,
                                int32_t *gradient_x,
                                int32_t *gradient_y,
                                int32_t *magnitude_squared) {
    int32_t y;
    size_t pixels;

    if (gray == NULL || mask == NULL || gradient_x == NULL ||
        gradient_y == NULL || magnitude_squared == NULL ||
        !gx_valid_image(width, height) || gray_stride < width ||
        mask_stride < width) return -1;

    pixels = (size_t)width * (size_t)height;
    memset(gradient_x, 0, pixels * sizeof(*gradient_x));
    memset(gradient_y, 0, pixels * sizeof(*gradient_y));
    memset(magnitude_squared, 0, pixels * sizeof(*magnitude_squared));

    for (y = 1; y < height - 1; ++y) {
        int32_t x;
        for (x = 1; x < width - 1; ++x) {
            int valid = 1;
            int32_t yy;
            for (yy = -1; yy <= 1 && valid; ++yy) {
                int32_t xx;
                const uint8_t *mrow = mask + (size_t)(y + yy) * (size_t)mask_stride;
                for (xx = -1; xx <= 1; ++xx) {
                    if (mrow[x + xx] == 0U) {
                        valid = 0;
                        break;
                    }
                }
            }
            if (valid) {
                const uint8_t *row = gray + (size_t)y * (size_t)gray_stride;
                const int32_t dx = (int32_t)row[x + 1] - (int32_t)row[x - 1];
                const int32_t dy =
                    (int32_t)gray[(size_t)(y + 1) * (size_t)gray_stride + (size_t)x] -
                    (int32_t)gray[(size_t)(y - 1) * (size_t)gray_stride + (size_t)x];
                const size_t index = (size_t)y * (size_t)width + (size_t)x;
                gradient_x[index] = dx;
                gradient_y[index] = dy;
                magnitude_squared[index] = dx * dx + dy * dy;
            }
        }
    }
    return 0;
}

int gx_feature_quality_integrals(uint8_t *mask,
                                 int32_t *gradient_x,
                                 int32_t *gradient_y,
                                 const int32_t *magnitude_squared,
                                 int32_t width,
                                 int32_t height,
                                 uint32_t *integral_mask,
                                 int32_t *integral_x_squared,
                                 int32_t *integral_y_squared,
                                 int32_t *integral_xy) {
    size_t pixels;
    size_t i;
    int32_t y;

    if (mask == NULL || gradient_x == NULL || gradient_y == NULL ||
        magnitude_squared == NULL || integral_mask == NULL ||
        integral_x_squared == NULL || integral_y_squared == NULL ||
        integral_xy == NULL || !gx_valid_image(width, height)) return -1;

    pixels = (size_t)width * (size_t)height;
    memset(integral_mask, 0, pixels * sizeof(*integral_mask));
    memset(integral_x_squared, 0, pixels * sizeof(*integral_x_squared));
    memset(integral_y_squared, 0, pixels * sizeof(*integral_y_squared));
    memset(integral_xy, 0, pixels * sizeof(*integral_xy));

    for (i = 0U; i < pixels; ++i) {
        if (magnitude_squared[i] < 25 || mask[i] == 0U) {
            gradient_x[i] = 0;
            gradient_y[i] = 0;
            mask[i] = 0U;
        }
    }

    for (y = 0; y < height; ++y) {
        int32_t x;
        for (x = 0; x < width; ++x) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            const int32_t gx = gradient_x[index];
            const int32_t gy = gradient_y[index];
            uint32_t sm = (uint32_t)mask[index];
            int64_t sxx = (int64_t)gx * gx;
            int64_t syy = (int64_t)gy * gy;
            int64_t sxy = (int64_t)gx * gy;

            if (x > 0) {
                const size_t left = index - 1U;
                sm += integral_mask[left];
                sxx += integral_x_squared[left];
                syy += integral_y_squared[left];
                sxy += integral_xy[left];
            }
            if (y > 0) {
                const size_t up = index - (size_t)width;
                sm += integral_mask[up];
                sxx += integral_x_squared[up];
                syy += integral_y_squared[up];
                sxy += integral_xy[up];
            }
            if (x > 0 && y > 0) {
                const size_t diagonal = index - (size_t)width - 1U;
                sm -= integral_mask[diagonal];
                sxx -= integral_x_squared[diagonal];
                syy -= integral_y_squared[diagonal];
                sxy -= integral_xy[diagonal];
            }
            integral_mask[index] = sm;
            integral_x_squared[index] = (int32_t)sxx;
            integral_y_squared[index] = (int32_t)syy;
            integral_xy[index] = (int32_t)sxy;
        }
    }
    return 0;
}

int32_t gx_feature_integral_rect_sum(const int32_t *integral,
                                     int32_t left,
                                     int32_t top,
                                     int32_t right,
                                     int32_t bottom,
                                     int32_t height,
                                     int32_t width) {
    int32_t left_before;
    int32_t top_before;
    int64_t result;

    if (integral == NULL || left < 0 || right >= width || top < 0 ||
        bottom >= height || bottom >= height || width <= 0 || height <= 0) return 0;
    if (bottom >= height) return 0;

    left_before = left - 1;
    if (left_before < 0) left_before = 0;
    if (right > width - 1) right = width - 1;
    top_before = top - 1;
    if (top_before < 0) top_before = 0;
    if (bottom > height - 1) bottom = height - 1;

    result = integral[(size_t)left_before + (size_t)top_before * (size_t)width];
    result -= integral[(size_t)left_before + (size_t)bottom * (size_t)width];
    result -= integral[(size_t)right + (size_t)top_before * (size_t)width];
    result += integral[(size_t)right + (size_t)bottom * (size_t)width];
    return (int32_t)result;
}

int32_t gx_feature_quality_window_score(const uint32_t *integral_mask,
                                        const int32_t *integral_x_squared,
                                        const int32_t *integral_y_squared,
                                        const int32_t *integral_xy,
                                        int32_t width,
                                        int32_t height,
                                        int32_t left,
                                        int32_t top,
                                        int32_t right,
                                        int32_t bottom) {
    int32_t count;
    const int32_t area = (right - left + 1) * (bottom - top + 1);

    if (integral_mask == NULL || integral_x_squared == NULL ||
        integral_y_squared == NULL || integral_xy == NULL ||
        width <= 0 || height <= 0) return 0;

    count = gx_feature_integral_rect_sum((const int32_t *)integral_mask,
                                         left, top, right, bottom,
                                         height, width);
    if (count > 0 && area / 2 <= count) {
        const int32_t half = count >> 1;
        const int32_t sum_xx = gx_feature_integral_rect_sum(integral_x_squared,
                                                            left, top, right, bottom,
                                                            height, width);
        const int32_t sum_yy = gx_feature_integral_rect_sum(integral_y_squared,
                                                            left, top, right, bottom,
                                                            height, width);
        const int32_t sum_xy = gx_feature_integral_rect_sum(integral_xy,
                                                            left, top, right, bottom,
                                                            height, width);
        const int32_t mean_xx = (sum_xx + half) / count;
        const int32_t mean_yy = (sum_yy + half) / count;
        const int32_t mean_xy = (sum_xy + half) / count;
        const int64_t mean_energy = ((int64_t)mean_yy + mean_xx) / 2;
        int64_t ratio =
            (((int64_t)mean_yy * mean_xx - (int64_t)mean_xy * mean_xy) * 65536) /
            (mean_energy * mean_energy + 1);
        int32_t nonnegative;
        if (ratio < 0) ratio = 0;
        if (ratio > INT32_MAX) ratio = INT32_MAX;
        nonnegative = (int32_t)ratio;
        return 65536 - nonnegative > 0 ? 65536 - nonnegative : 0;
    }
    return 0;
}

int gx_feature_quality_classify_local(const int8_t *values,
                                      int32_t rows,
                                      int32_t columns,
                                      int32_t radius,
                                      uint8_t *output,
                                      gx_feature_quality_stats *stats) {
    int32_t *integral;
    int32_t negative = 0;
    int32_t positive = 0;
    int32_t neutral = 0;
    int32_t y;
    size_t pixels;

    if (values == NULL || output == NULL || stats == NULL ||
        !gx_valid_image(columns, rows) || radius < 0) return -1;
    pixels = (size_t)rows * (size_t)columns;
    if (pixels > SIZE_MAX / sizeof(*integral)) return -1;
    integral = (int32_t *)calloc(pixels, sizeof(*integral));
    if (integral == NULL) return -1;

    for (y = 0; y < rows; ++y) {
        int32_t x;
        for (x = 0; x < columns; ++x) {
            const size_t index = (size_t)y * (size_t)columns + (size_t)x;
            int32_t sum = values[index];
            if (x > 0) sum += integral[index - 1U];
            if (y > 0) sum += integral[index - (size_t)columns];
            if (x > 0 && y > 0) sum -= integral[index - (size_t)columns - 1U];
            integral[index] = sum;
        }
    }

    for (y = 0; y < rows; ++y) {
        int32_t x;
        for (x = 0; x < columns; ++x) {
            int32_t left = x - radius;
            int32_t right = x + radius;
            int32_t top = y - radius;
            int32_t bottom = y + radius;
            int32_t area;
            int32_t sum;
            int32_t average;
            const size_t index = (size_t)y * (size_t)columns + (size_t)x;

            if (left < 0) left = 0;
            if (right > columns - 1) right = columns - 1;
            if (top < 0) top = 0;
            if (bottom > rows - 1) bottom = rows - 1;
            area = (right - left + 1) * (bottom - top + 1);

            sum = integral[(size_t)right + (size_t)bottom * (size_t)columns];
            if (left > 0) sum -= integral[(size_t)(left - 1) + (size_t)bottom * (size_t)columns];
            if (top > 0) sum -= integral[(size_t)right + (size_t)(top - 1) * (size_t)columns];
            if (left > 0 && top > 0)
                sum += integral[(size_t)(left - 1) + (size_t)(top - 1) * (size_t)columns];
            average = area > 0 ? (sum + (area >> 1)) / area : 0;

            if (average < 2) {
                if (average < -3) {
                    ++negative;
                    output[index] = 0U;
                } else {
                    ++neutral;
                    output[index] = 0x80U;
                }
            } else {
                ++positive;
                output[index] = 0xffU;
            }
        }
    }

    free(integral);
    stats->negative_percent = (negative * 100) / (int32_t)pixels;
    stats->positive_percent = (positive * 100) / (int32_t)pixels;
    stats->neutral_percent = (neutral * 100) / (int32_t)pixels;
    return 0;
}
