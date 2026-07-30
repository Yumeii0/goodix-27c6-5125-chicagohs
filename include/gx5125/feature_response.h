#ifndef GX5125_FEATURE_RESPONSE_H
#define GX5125_FEATURE_RESPONSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_VECTOR_ANGLE_PERIOD 0x6488U
#define GX_FEATURE_VECTOR_ANGLE_HALF 0x3244U
#define GX_FEATURE_VECTOR_ANGLE_QUARTER 0x1922U

/* Exact native implementation target: AlgoChicago FUN_180057870.
 * The first component is the vertical vector and the second is horizontal.
 * The returned angle uses the 0x6488 full-turn unit. out_magnitude receives
 * the vendor CORDIC magnitude in the same fixed-point scale as the inputs. */
uint16_t gx_feature_vector_angle_magnitude(int32_t vertical,
                                           int32_t horizontal,
                                           int32_t *out_magnitude);

/* Exact native implementation target: AlgoChicago FUN_180015cc0.
 * Source and magnitude are signed int32 maps; angle is a signed int16 map.
 * Only interior pixels are written. Border values are intentionally left
 * untouched, matching the vendor routine. */
int gx_feature_build_response_map(const int32_t *source,
                                  int32_t width,
                                  int32_t height,
                                  int32_t *magnitude,
                                  int16_t *angle);

#ifdef __cplusplus
}
#endif

#endif
