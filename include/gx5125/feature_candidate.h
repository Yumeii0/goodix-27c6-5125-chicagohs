#ifndef GX5125_FEATURE_CANDIDATE_H
#define GX5125_FEATURE_CANDIDATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gx_feature_u16_map {
    int32_t width;
    int32_t height;
    const uint16_t *pixels;
} gx_feature_u16_map;

/* Native equivalent of AlgoChicago FUN_180016640 for valid interior points.
 * The tested vendor function evaluates a signed difference extremum across
 * four adjacent pyramid levels. Returns 1 for an accepted extremum, 0 for a
 * rejection, and -1 for invalid native arguments. */
int gx_feature_candidate_extremum_gate(const gx_feature_u16_map *levels,
                                       size_t level_count,
                                       int32_t x,
                                       int32_t y,
                                       int32_t level,
                                       int32_t threshold,
                                       int32_t *response_out);

/* Native equivalent of AlgoChicago FUN_1800133c0.
 * hessian6 and gradient3 are signed fixed-point inputs. The three Q12 outputs
 * preserve the vendor's saturation and signed-division behavior. */
int gx_feature_candidate_solve_q12(const int16_t hessian6[6],
                                   const int16_t gradient3[3],
                                   int32_t output3[3]);

#ifdef __cplusplus
}
#endif

#endif
