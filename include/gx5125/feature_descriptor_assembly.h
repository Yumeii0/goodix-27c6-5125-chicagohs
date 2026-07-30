#ifndef GX5125_FEATURE_DESCRIPTOR_ASSEMBLY_H
#define GX5125_FEATURE_DESCRIPTOR_ASSEMBLY_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_descriptor_sample.h"
#include "gx5125/feature_orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_DESCRIPTOR_PROFILE_WORDS 12U

/* Exact target-profile equivalent of AlgoChicago FUN_180056a30.
 * compressed contains 128 signed int16 values and rotation is the native
 * 128 x 128 table from FUN_180017b70. The function writes record bytes
 * 0x10..0x27. */
void gx_feature_descriptor_project_hadamard(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    const int16_t compressed[GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES],
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE]);

/* Exact target-profile equivalent of FUN_1800179e0 for non-9/non-18
 * profiles. mode 0 builds the primary projection and mode 1 builds the
 * correlation projection. Returns 0 on success and -2 for the special
 * profile 9/18 branch, which is deliberately outside this target-specific
 * implementation. */
int gx_feature_descriptor_finalize(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE],
    const int32_t *values,
    int32_t value_count,
    int32_t mode,
    int32_t profile,
    int32_t tail_rows,
    int32_t tail_channels);

/* Exact target-profile equivalent of FUN_180015a80. profile_words maps to
 * the vendor config object. Only indexes 0 and 6..11 are consumed here.
 * Profiles 9 and 18 return -2 because their special projection path is not
 * part of the ChicagoHS profile-0x0c target. */
int gx_feature_record_descriptor_assemble(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE],
    const int32_t profile_words[GX_FEATURE_DESCRIPTOR_PROFILE_WORDS]);

#ifdef __cplusplus
}
#endif

#endif
