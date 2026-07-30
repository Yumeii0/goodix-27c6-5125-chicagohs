#ifndef GX5125_FEATURE_QUALITY_POST_H
#define GX5125_FEATURE_QUALITY_POST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_RECORD_BYTES 0x3cU
#define GX_FEATURE_MAX_RECORDS 120U

/* Exact target-domain equivalent of FUN_180016be0.
 * Computes one 0..100 local ridge-coherence score for each feature record.
 * Record coordinates are read from the high bytes of the Q8 X/Y fields at
 * offsets +0x03 and +0x05. */
int gx_feature_record_window_quality(const uint8_t *gray,
                                     const uint8_t *mask,
                                     int32_t width,
                                     int32_t height,
                                     int32_t gray_stride,
                                     int32_t mask_stride,
                                     const uint8_t *records,
                                     uint32_t record_count,
                                     uint8_t *quality_out);

/* Exact target-domain equivalent of FUN_180016e80.
 * When enabled is zero, record byte +0x39 is cleared and output_scores is
 * untouched. When enabled is nonzero, nearby records contribute to the
 * output score and to record byte +0x39. */
int gx_feature_neighbor_quality(uint8_t *records,
                                uint32_t record_count,
                                int enabled,
                                const uint8_t *feature_quality,
                                const int8_t *input_scores,
                                int8_t *output_scores);

#ifdef __cplusplus
}
#endif

#endif
