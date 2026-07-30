#ifndef GX5125_FEATURE_PYRAMID_H
#define GX5125_FEATURE_PYRAMID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AlgoChicago's internal image descriptor. The field at +0x0c is an
 * element count, not a byte count. The storage type is described at +0x10. */
#pragma pack(push, 1)
typedef struct gx_feature_pyramid_image {
    int32_t width;
    int32_t height;
    int32_t reserved_08;
    int32_t element_count;
    int32_t element_bytes;
    int32_t reserved_14;
    void *pixels;
} gx_feature_pyramid_image;
#pragma pack(pop)

typedef uint64_t (*gx_feature_pyramid_params_fn)(
    int32_t first, int32_t second, void *context);

typedef void (*gx_feature_pyramid_filter_fn)(
    gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    uint64_t parameters,
    int32_t scale_code,
    int32_t option_a,
    int32_t option_b,
    void *context);

typedef struct gx_feature_pyramid_callbacks {
    gx_feature_pyramid_params_fn make_parameters;
    gx_feature_pyramid_filter_fn filter;
    void *context;
} gx_feature_pyramid_callbacks;

typedef struct gx_feature_pyramid_stats {
    uint32_t level_count;
    uint32_t filter_calls;
    int32_t first_scale_code;
    int32_t last_scale_code;
} gx_feature_pyramid_stats;

/* Exact control-flow reimplementation of AlgoChicago Milan_v_3.02.00.15
 * FUN_1800125c0. It prepares the first 16-bit response level from the 8-bit
 * source and invokes the supplied filter callback in the vendor order.
 *
 * The filter executor is supplied as a callback so orchestration and fixed-point
 * convolution remain independently testable. The native implementation keeps the profile-24 Q16 reflect-101
 * implementation for codes 300..308 while the active auxiliary filters are
 * handled by the shared native filter module.
 */
int gx_feature_response_pyramid_prepare(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *const *levels,
    size_t level_capacity,
    int32_t scale_start,
    uint32_t profile,
    const gx_feature_pyramid_callbacks *callbacks,
    gx_feature_pyramid_stats *stats);

size_t gx_feature_response_pyramid_level_count(uint32_t profile);

#ifdef __cplusplus
}
#endif

#endif
