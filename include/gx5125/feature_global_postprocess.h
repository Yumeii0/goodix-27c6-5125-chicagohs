#ifndef GX5125_FEATURE_GLOBAL_POSTPROCESS_H
#define GX5125_FEATURE_GLOBAL_POSTPROCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_OBJECT_BYTES 0x230U
#define GX_FEATURE_RECORD_BYTES 0x3cU
#define GX_FEATURE_MAX_RECORDS 120U

/* Runtime-domain exact equivalent of Milan_v_3.02.00.15 FUN_180012250.
 *
 * The feature object is the native 0x230-byte extractor object. Relevant
 * fields are:
 *   +0x000 u32 width
 *   +0x004 u32 height
 *   +0x0f0 u32 record_count
 *   +0x0f8 pointer to 0x3c-byte records
 *   +0x158 u32 positive-map percentage (output)
 *   +0x15c u32 map state 0/1/2 (output)
 *   +0x160 u32 non-positive record-flag count (output)
 *   +0x164 signed per-record quality bytes
 *   +0x220 pointer to width*height output map
 *
 * Returns 1 when at least one record enters the global postprocess path,
 * otherwise 0. The target runtime domain uses at most 120 records.
 */
uint64_t gx_feature_global_postprocess(uint8_t *feature_object,
                                       int32_t mode,
                                       int32_t internal_profile);

#ifdef __cplusplus
}
#endif

#endif
