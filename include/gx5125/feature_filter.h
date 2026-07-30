#ifndef GX5125_FEATURE_FILTER_H
#define GX5125_FEATURE_FILTER_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_pyramid.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gx_feature_filter_stats {
    uint32_t horizontal_passes;
    uint32_t vertical_passes;
    uint32_t kernel_length;
    uint32_t kernel_sum;
} gx_feature_filter_stats;

typedef struct gx_feature_filter_kernel {
    int32_t scale_code;
    const int32_t *coefficients;
    size_t coefficient_count;
    uint32_t coefficient_sum;
} gx_feature_filter_kernel;

/* Exact initialized-data kernels used by AlgoChicago
 * Milan_v_3.02.00.15 for internal feature profile 24, scale codes 300..308.
 * The returned coefficient pointer has static lifetime and must not be freed. */
int gx_feature_filter_profile24_kernel(
    int32_t scale_code,
    gx_feature_filter_kernel *kernel);

/* Validates all nine native kernels: code ordering, odd lengths, symmetry,
 * non-negative Q16 values and recorded sums. */
int gx_feature_filter_profile24_validate(void);

/* Exact auxiliary filters used by the active profile-24 feature path.
 * - code 7: pre-feature mask preparation, 5-tap Q16 Gaussian-like kernel
 * - code 0: in-place generator smoothing, 9-tap Q16 kernel
 * - code 1: out-of-place generator smoothing, 7-tap Q16 kernel
 * The returned coefficient pointer has static lifetime and must not be freed. */
int gx_feature_filter_auxiliary_kernel(
    int32_t scale_code,
    gx_feature_filter_kernel *kernel);

/* Validates exact code membership, lengths, symmetry, non-negative values
 * and Q16 sums for auxiliary codes 0, 1 and 7. */
int gx_feature_filter_auxiliary_validate(void);

/* Exact image-prepare filter used by FUN_180013c90 for active ChicagoHS
 * profile-24 extraction. AlgoChicago scale code 6 is a 3-tap uniform Q16
 * kernel: {21845, 21845, 21845}; its sum is 65535. The returned pointer
 * has static lifetime and must not be freed. */
int gx_feature_filter_image_prepare_code6_kernel(
    gx_feature_filter_kernel *kernel);

/* Validates the exact code-6 kernel length, values, symmetry and Q16 sum. */
int gx_feature_filter_image_prepare_code6_validate(void);

/* Exact optimized execution semantics for scale code 6. The vendor fast path
 * sums each three-sample window first, multiplies by the shared Q16 weight
 * 21845, truncates by 16 bits, and repeats vertically. Both axes use
 * BORDER_REFLECT_101. Source and destination may alias. */
int gx_feature_filter_image_prepare_code6_u16_reflect101(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    gx_feature_filter_stats *stats);

/* Fixed-point separable filter used by AlgoChicago Milan_v_3.02.00.15 for
 * the profile-24 response pyramid (scale codes 300..308).
 *
 * Input and output are unsigned 16-bit images. Coefficients are signed
 * Q16 values, symmetric and odd-length. Both axes use BORDER_REFLECT_101.
 * The horizontal pass truncates by 16 bits into a 32-bit intermediate; the
 * vertical pass truncates by 16 bits into the final 16-bit destination.
 * Source and destination may alias.
 */
int gx_feature_filter_u16_q16_reflect101(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    const int32_t *coefficients,
    size_t coefficient_count,
    gx_feature_filter_stats *stats);

#ifdef __cplusplus
}
#endif

#endif
