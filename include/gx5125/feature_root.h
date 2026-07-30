#ifndef GX5125_FEATURE_ROOT_H
#define GX5125_FEATURE_ROOT_H

#include <stdint.h>

#include "gx5125/feature_pyramid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_PROFILE24_PARAMETER_WORDS 15U
#define GX_FEATURE_PROFILE24_ID UINT32_C(24)
#define GX_FEATURE_PROFILE24_MAX_RECORDS UINT32_C(300)
#define GX_FEATURE_PROFILE24_EDGE_THRESHOLD UINT32_C(40)
#define GX_FEATURE_PROFILE24_SCALE_Q16 UINT32_C(0x0001822e)
#define GX_FEATURE_OPTIONAL_WORKSPACE_BYTES 19600U
#define GX_FEATURE_OPTIONAL_RADIUS UINT32_C(6)

typedef gx_feature_pyramid_image *(*gx_feature_descriptor_alloc_fn)(
    int32_t width, int32_t height, int32_t element_type);
typedef uint32_t (*gx_feature_descriptor_release_fn)(
    gx_feature_pyramid_image **descriptor);
typedef int (*gx_feature_root_filter_fn)(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    int32_t scale_code, void *context);
typedef void (*gx_feature_optional_orientation_fn)(
    const uint8_t *source, uint8_t *workspace,
    int32_t width, int32_t height, uint32_t radius);
typedef void (*gx_feature_optional_reconstruct_fn)(
    const uint8_t *workspace, const uint8_t *source, uint8_t *destination,
    int32_t width, int32_t height);

/* Build the exact 0x3c-byte detector/generator parameter block used by the
 * active Milan_v_3.02.00.15 profile-24 branch. The two four-word templates
 * are copied from the pinned DLL's initialized data and the profile-specific
 * scale word is then overwritten with 0x1822e, matching FUN_180013c90. */
int gx_feature_profile24_parameters_build(
    uint32_t output[GX_FEATURE_PROFILE24_PARAMETER_WORDS],
    const uint32_t template_a[4], const uint32_t template_b[4],
    uint32_t capacity, uint32_t quality, uint32_t coverage);

/* Exact outer shell of FUN_180055810. The descriptor allocation and the
 * 19,600-byte temporary workspace lifecycle are native. The two callbacks
 * represent FUN_1800558c0 and FUN_180055630 and may be independently replaced
 * as those dense kernels are migrated. */

/* Exact native implementation targets for the two dense inner kernels used
 * by FUN_180055810. The orientation kernel writes width*height direction
 * bytes into workspace. The reconstruction kernel applies the pinned DLL's
 * 12-direction, seven-sample weighted reconstruction. */
int gx_feature_optional_orientation_kernel(
    const uint8_t *source, uint8_t *workspace,
    int32_t width, int32_t height, uint32_t radius);
int gx_feature_optional_reconstruct_kernel(
    const uint8_t *workspace, const uint8_t *source, uint8_t *destination,
    int32_t width, int32_t height);


/* Exact active-path implementation of FUN_180050800. It builds the native
 * profile-24 segmentation mask from an 8-bit gray descriptor, including the
 * code-7 Q16 smoothing, Sobel magnitude, 7x7 reflect-101 box average and the
 * Q16 coverage return value. When enabled is zero the output mask is left
 * untouched, matching the pinned DLL. */
int gx_feature_root_mask_prepare(
    const gx_feature_pyramid_image *gray,
    gx_feature_pyramid_image *mask,
    int32_t enabled, uint16_t threshold, uint8_t fill_value,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release,
    gx_feature_root_filter_fn filter_execute, void *filter_context,
    uint32_t *coverage_q16);

/* Exact active profile-24 implementation of FUN_180054d10. The four pointer
 * slots at feature_object offsets +0x08, +0x10, +0x18 and +0x20 are populated
 * with bit-packed adaptive/fixed maps and the copied working mask. Both the
 * full-resolution mode (mode==0) and the pinned half-resolution branch are
 * implemented. */
int gx_feature_root_feature_maps_prepare(
    int32_t mode,
    const gx_feature_pyramid_image *primary,
    const gx_feature_pyramid_image *optional,
    const gx_feature_pyramid_image *mask,
    uint8_t *feature_object,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release);

gx_feature_pyramid_image *gx_feature_optional_image_prepare(
    const gx_feature_pyramid_image *source,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_optional_orientation_fn orientation_kernel,
    gx_feature_optional_reconstruct_fn reconstruct_kernel);

/* Non-biometric fixed-layout, branch-parameter and optional-shell selftest. */
int gx_feature_profile24_root_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
