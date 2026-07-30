#ifndef GX5125_FEATURE_DETECTOR_H
#define GX5125_FEATURE_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_candidate.h"
#include "gx5125/feature_orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_DETECTOR_MAX_FALLBACK 600U
#define GX_FEATURE_DETECTOR_RECORD_LIMIT 120U

typedef struct gx_feature_detector_parameters {
    uint8_t reserved_00_33[0x34];
    int32_t metric_34;
    int32_t metric_38;
} gx_feature_detector_parameters;

/* Native equivalent of AlgoChicago FUN_180015660.
 *
 * gray->pixels points to uint8_t image data. levels is a same-sized uint16_t
 * response pyramid. magnitude->pixels points to int32_t values and
 * orientation->pixels points to int16_t values. rank_pairs has two int32_t
 * values per record; metadata has six int32_t slots per record.
 *
 * The vendor function resets record_count to zero at entry and writes the
 * final count on return. All buffers must have room for max_records entries.
 */
int gx_feature_detect_candidates(
    const gx_feature_map_descriptor *gray,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *record_count,
    const gx_feature_u16_map *levels,
    size_t level_count,
    uint8_t *visited,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t edge_threshold,
    int32_t max_records,
    uint32_t profile,
    const gx_feature_detector_parameters *parameters);

#ifdef __cplusplus
}
#endif

#endif
