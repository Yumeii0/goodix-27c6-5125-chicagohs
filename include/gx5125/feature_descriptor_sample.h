#ifndef GX5125_FEATURE_DESCRIPTOR_SAMPLE_H
#define GX5125_FEATURE_DESCRIPTOR_SAMPLE_H

#include <stdint.h>

#include "gx5125/feature_orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_DESCRIPTOR_GRID_SIDE 6
#define GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS 8
#define GX_FEATURE_DESCRIPTOR_OUTPUT_CELLS 16
#define GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES 128
#define GX_FEATURE_ROTATION_TABLE_SIDE 128

/* Exact native equivalents of AlgoChicago integer square-root helpers. */
uint32_t gx_feature_isqrt_u32(uint32_t value);
uint32_t gx_feature_isqrt_u64(uint64_t value);

/* Exact native equivalent of FUN_180012800. The histogram is a flat
 * 6 x 6 x 8 signed-int32 accumulator. Coordinates are Q9 and orientation
 * is Q12 over eight circular bins. */
void gx_feature_descriptor_accumulate(
    int32_t histogram[GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_GRID_SIDE *
                      GX_FEATURE_DESCRIPTOR_ORIENTATION_BINS],
    int32_t x_q9,
    int32_t y_q9,
    int32_t orientation_q12,
    int32_t weight_q9);

/* Exact native equivalent of FUN_180012b10. Produces the central 4 x 4 x 8
 * descriptor tensor (128 int32 values) from orientation and magnitude maps. */
void gx_feature_descriptor_sample(
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    int16_t angle_units,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    int32_t output[GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES]);

/* Exact native equivalent of FUN_180017b70. Initializes the 128 x 128
 * signed descriptor rotation/permutation table. */
void gx_feature_descriptor_rotation_table_init(
    int16_t table[GX_FEATURE_ROTATION_TABLE_SIDE *
                  GX_FEATURE_ROTATION_TABLE_SIDE]);

#ifdef __cplusplus
}
#endif

#endif
