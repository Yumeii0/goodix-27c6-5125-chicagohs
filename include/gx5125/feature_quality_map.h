#ifndef GX5125_FEATURE_QUALITY_MAP_H
#define GX5125_FEATURE_QUALITY_MAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gx_feature_quality_stats {
    int32_t negative_percent;
    int32_t positive_percent;
    int32_t neutral_percent;
} gx_feature_quality_stats;

/* Exact runtime-domain equivalent of FUN_18000fcd0. */
int gx_feature_quality_gradient(const uint8_t *gray,
                                const uint8_t *mask,
                                int32_t width,
                                int32_t height,
                                int32_t gray_stride,
                                int32_t mask_stride,
                                int32_t *gradient_x,
                                int32_t *gradient_y,
                                int32_t *magnitude_squared);

/* Exact runtime-domain equivalent of FUN_180018070. The mask and gradients
 * are filtered in place before the four integral maps are produced. */
int gx_feature_quality_integrals(uint8_t *mask,
                                 int32_t *gradient_x,
                                 int32_t *gradient_y,
                                 const int32_t *magnitude_squared,
                                 int32_t width,
                                 int32_t height,
                                 uint32_t *integral_mask,
                                 int32_t *integral_x_squared,
                                 int32_t *integral_y_squared,
                                 int32_t *integral_xy);

/* Exact runtime-domain equivalent of FUN_180050e80. */
int32_t gx_feature_integral_rect_sum(const int32_t *integral,
                                     int32_t left,
                                     int32_t top,
                                     int32_t right,
                                     int32_t bottom,
                                     int32_t height,
                                     int32_t width);

/* Exact runtime-domain equivalent of FUN_180011b60. */
int32_t gx_feature_quality_window_score(const uint32_t *integral_mask,
                                        const int32_t *integral_x_squared,
                                        const int32_t *integral_y_squared,
                                        const int32_t *integral_xy,
                                        int32_t width,
                                        int32_t height,
                                        int32_t left,
                                        int32_t top,
                                        int32_t right,
                                        int32_t bottom);

/* Exact runtime-domain equivalent of FUN_180012f40. Input samples are signed
 * bytes. Output values are 0x00, 0x80, or 0xff. */
int gx_feature_quality_classify_local(const int8_t *values,
                                      int32_t rows,
                                      int32_t columns,
                                      int32_t radius,
                                      uint8_t *output,
                                      gx_feature_quality_stats *stats);

#ifdef __cplusplus
}
#endif

#endif
