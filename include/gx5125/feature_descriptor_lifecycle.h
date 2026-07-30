#ifndef GX5125_FEATURE_DESCRIPTOR_LIFECYCLE_H
#define GX5125_FEATURE_DESCRIPTOR_LIFECYCLE_H

#include <stdint.h>

#include "gx5125/feature_pyramid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_DESCRIPTOR_HEADER_BYTES 0x20U
#define GX_FEATURE_DESCRIPTOR_RELEASE_INVALID UINT32_C(0x80000002)

/*
 * Exact descriptor-object layout used by AlgoChicago Milan_v_3.02.00.15.
 * The returned allocation is a single block containing the 0x20-byte
 * descriptor header followed immediately by its pixel storage.
 *
 * element_type values 1, 2 and 4 allocate width*height*element_type bytes.
 * The special value 8 allocates a bit-packed ceil(width*height/8) payload and
 * stores -1 in reserved_08, matching FUN_180042c60.
 */
gx_feature_pyramid_image *gx_feature_descriptor_allocate(
    int32_t width, int32_t height, int32_t element_type);

/* Mirrors FUN_1800436d0: free a descriptor through a pointer-to-pointer,
 * clear the caller's pointer, and return 0. A null argument or already-null
 * descriptor returns 0x80000002. */
uint32_t gx_feature_descriptor_release(
    gx_feature_pyramid_image **descriptor);

/* Non-biometric ABI and lifecycle selftest. */
int gx_feature_descriptor_lifecycle_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
