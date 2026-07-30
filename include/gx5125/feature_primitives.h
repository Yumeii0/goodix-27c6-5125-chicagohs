#ifndef GX5125_FEATURE_PRIMITIVES_H
#define GX5125_FEATURE_PRIMITIVES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_ANGLE_PERIOD 0x6488
#define GX_FEATURE_ANGLE_HALF   0x3244
#define GX_FEATURE_ANGLE_QUARTER 0x1922

/* Exact native implementation target: AlgoChicago FUN_180057620.
 * Angle units use a full-turn modulus of 0x6488. Outputs are Q14. */
void gx_feature_angle_sincos_q14(int32_t angle_units,
                                 int32_t *out_sin_q14,
                                 int32_t *out_cos_q14);

/* Exact return-value implementation target: FUN_180056dc0.
 * The vendor routine mutates its working array; this API does not mutate input.
 * It returns the lower median using signed int16 ordering. */
int16_t gx_feature_signed_median_i16(const int16_t *values, size_t count);

/* Exact implementation target: FUN_1800567d0.
 * ORs threshold bits into four words; caller controls initial word contents.
 * rows == config[2], channels == config[3] in the vendor routine. */
void gx_feature_build_tail_bits(uint32_t out_words[4],
                                const int16_t *values,
                                size_t value_count,
                                int32_t rows,
                                int32_t channels);

/* Exact implementation target: FUN_180056920.
 * Clears record bytes 0x28..0x37 and writes the 32-way correlation word
 * at 0x28. bank contains 32 blocks with stride 128 int16 elements; only
 * the first 32 elements of each block participate. */
void gx_feature_build_correlation_word(uint8_t record[0x3c],
                                       const int16_t reference[32],
                                       const int16_t bank[32 * 128]);

/* Exact implementation target for valid runtime inputs: FUN_180014e00.
 * values are 128 nonnegative descriptor-energy samples. */
int gx_feature_descriptor_quality_flag(const int32_t values[128],
                                       int use_mean_gate,
                                       int32_t profile);

#ifdef __cplusplus
}
#endif

#endif
