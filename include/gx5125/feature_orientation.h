#ifndef GX5125_FEATURE_ORIENTATION_H
#define GX5125_FEATURE_ORIENTATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_ORIENTATION_BINS 36U
#define GX_FEATURE_RECORD_BYTES 0x3cU
#define GX_FEATURE_APPEND_META_BYTES 24U

typedef struct gx_feature_map_descriptor {
    int32_t width;
    int32_t height;
    int32_t reserved_08;
    int32_t reserved_0c;
    int32_t reserved_10;
    int32_t reserved_14;
    void *pixels;
} gx_feature_map_descriptor;

typedef struct gx_feature_refined_candidate {
    int32_t x;
    int32_t y;
    int32_t level;
    int32_t response;
    int16_t x_q8;
    int16_t y_q8;
    int32_t scale_q16;
} gx_feature_refined_candidate;

/* Native equivalent of AlgoChicago FUN_180015510. The destination is a
 * square side x side array. The vendor function mirrors every computed
 * upper-triangle value into the corresponding lower-triangle location. */
void gx_feature_gaussian_weights(uint32_t *weights,
                                 int32_t side,
                                 uint32_t coefficient);

/* Native equivalent of AlgoChicago FUN_180011d10. magnitude->pixels points
 * to int32_t values and orientation->pixels points to int16_t values.
 * The vendor function writes peak_bin only when duplicate_opposite is nonzero;
 * in the ordinary one-orientation path that returned bin is shifted by +18. */
uint32_t gx_feature_orientation_histogram(
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    uint32_t histogram[GX_FEATURE_ORIENTATION_BINS],
    int32_t *peak_bin,
    int32_t duplicate_opposite);

/* Native equivalent of AlgoChicago FUN_180014810. It appends one record for
 * ordinary profiles, or one record per accepted orientation peak for profile
 * 9/18. All output buffers must have room for max_records entries. */
void gx_feature_append_orientations(
    const uint8_t *gray_center,
    const gx_feature_refined_candidate *candidate,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *record_count,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t max_records,
    uint32_t profile);

#ifdef __cplusplus
}
#endif

#endif
