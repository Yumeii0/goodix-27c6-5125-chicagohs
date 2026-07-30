#ifndef GX5125_FEATURE_POSTPROCESS_H
#define GX5125_FEATURE_POSTPROCESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_POST_RECORD_BYTES 0x3cU
#define GX_FEATURE_POST_MAX_RECORDS 120U

typedef struct gx_feature_post_map {
    int32_t width;
    int32_t height;
    uint8_t *pixels;
} gx_feature_post_map;

/* Exact native equivalent of FUN_18000f090 for the runtime 0/1 record classes. */
int gx_feature_post_partition_records(uint8_t *records,
                                      uint8_t *parallel_flags,
                                      uint32_t count,
                                      uint32_t *zero_class_count);

/* Exact native equivalent of FUN_180012140. */
int gx_feature_post_prune_mask(uint8_t *records,
                               uint32_t *count,
                               const gx_feature_post_map *mask);

/* Exact native equivalent of FUN_1800533e0. Output is row-major, ceil(width/8). */
int gx_feature_post_pack_map(const gx_feature_post_map *map,
                             uint8_t *packed,
                             size_t packed_bytes);

size_t gx_feature_post_packed_bytes(int32_t width, int32_t height);

#ifdef __cplusplus
}
#endif

#endif
