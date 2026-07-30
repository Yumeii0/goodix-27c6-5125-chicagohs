#ifndef GX5125_FEATURE_REFINE_H
#define GX5125_FEATURE_REFINE_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_candidate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gx_feature_candidate_state {
    int32_t x;
    int32_t y;
    int32_t level;
    int32_t response;
    int16_t x_q8;
    int16_t y_q8;
    int32_t scale_q16;
} gx_feature_candidate_state;

/* Native equivalent of AlgoChicago FUN_1800577c0.
 * Returns the vendor fixed-point exponential-like scale in unsigned Q16. */
uint32_t gx_feature_scale_exp_q16(int32_t input_q16);

/* Native equivalent of AlgoChicago FUN_180015e60 for valid pyramid points.
 * hessian6 order: xx, yy, zz, xy, yz, xz.
 * gradient3 order: response, x, y as used by the vendor refinement path. */
int gx_feature_candidate_sample_derivatives(const gx_feature_u16_map *levels,
                                            size_t level_count,
                                            int32_t x,
                                            int32_t y,
                                            int32_t level,
                                            int16_t hessian6[6],
                                            int16_t gradient3[3],
                                            int16_t *response_out);

/* Native equivalent of AlgoChicago FUN_180015060.
 * Mutates candidate to the refined integer/Q8 location and scale. Returns 1
 * for an accepted refined candidate, 0 for a vendor-equivalent rejection,
 * and -1 for invalid native arguments. */
int gx_feature_candidate_refine(const gx_feature_u16_map *levels,
                                size_t level_count,
                                gx_feature_candidate_state *candidate,
                                int32_t *curvature_out,
                                int32_t edge_threshold,
                                uint32_t profile);

#ifdef __cplusplus
}
#endif

#endif
