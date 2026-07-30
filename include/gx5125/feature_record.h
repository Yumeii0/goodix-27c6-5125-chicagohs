#ifndef GX5125_FEATURE_RECORD_H
#define GX5125_FEATURE_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_RECORD_BYTES 0x3cU
#define GX_FEATURE_RECORD_X_OFFSET 0x02U
#define GX_FEATURE_RECORD_Y_OFFSET 0x04U
#define GX_FEATURE_RECORD_ANGLE_OFFSET 0x06U
#define GX_FEATURE_RECORD_SIGNED_METRIC_OFFSET 0x08U
#define GX_FEATURE_RECORD_CLASS_OFFSET 0x0cU
#define GX_FEATURE_RECORD_DESCRIPTOR_OFFSET 0x10U
#define GX_FEATURE_RECORD_FLAGS_OFFSET 0x38U

/* Exact native reimplementation of AlgoChicago Milan_v_3.02.00.15
 * FUN_1800164d0. Input must be the unnormalized/raw 0x3c-byte record
 * produced by the common feature generator before its final mode pass.
 * identify_mode == 0 creates enrollment representation.
 * identify_mode != 0 creates identify representation. */
void gx_feature_record_apply_mode(uint8_t record[GX_FEATURE_RECORD_BYTES],
                                  int identify_mode);

uint32_t gx_feature_record_bitreverse32(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
