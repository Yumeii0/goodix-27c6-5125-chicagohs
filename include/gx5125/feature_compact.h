#ifndef GX5125_FEATURE_COMPACT_H
#define GX5125_FEATURE_COMPACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linear, least-significant-bit-first bit stream used by FUN_180054070. */
size_t gx_feature_compact_linear_bytes(size_t value_count);

/* Exact native equivalent of FUN_180054070 for runtime map values. */
int gx_feature_compact_pack_linear(const uint8_t *values,
                                   size_t value_count,
                                   uint8_t *packed,
                                   size_t packed_bytes);

/*
 * Exact runtime-domain equivalent of FUN_180052210.
 *
 * Each ceil(width/4) x ceil(height/4) output cell is set when the sum of
 * source bytes in its (possibly clipped) 4x4 block reaches at least half of
 * the number of source pixels. The compact cells are then packed LSB-first.
 * The vendor function initializes the entire 200-byte destination to 0xff;
 * this function preserves that observable ABI.
 */
int gx_feature_compact_build_quarter(const uint8_t *pixels,
                                     int32_t width,
                                     int32_t height,
                                     int32_t row_stride,
                                     uint8_t output[200]);

/* Exact native pixel result of FUN_180051820. */
int gx_feature_compact_expand(const uint8_t *packed,
                              size_t packed_bytes,
                              int32_t mode,
                              int32_t full_width,
                              int32_t full_height,
                              uint8_t *output,
                              size_t output_bytes,
                              int32_t *output_width,
                              int32_t *output_height);

#ifdef __cplusplus
}
#endif

#endif
