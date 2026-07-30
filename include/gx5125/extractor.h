#ifndef GX5125_EXTRACTOR_H
#define GX5125_EXTRACTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX5125_EXTRACTOR_COLUMNS 80U
#define GX5125_EXTRACTOR_ROWS 64U
#define GX5125_EXTRACTOR_PIXELS \
    (GX5125_EXTRACTOR_COLUMNS * GX5125_EXTRACTOR_ROWS)
#define GX5125_EXTRACTOR_RECORD_BYTES 0x3cU
#define GX5125_EXTRACTOR_MAX_RECORDS 120U
#define GX5125_EXTRACTOR_AUX_BYTES 6U
#define GX5125_EXTRACTOR_VERSION "0.1.0"

typedef enum gx5125_extract_mode {
    GX5125_EXTRACT_ENROLL = 0,
    GX5125_EXTRACT_IDENTIFY = 1
} gx5125_extract_mode;

typedef enum gx5125_status {
    GX5125_OK = 0,
    GX5125_ERR_ARGUMENT = -1,
    GX5125_ERR_MEMORY = -2,
    GX5125_ERR_DIMENSIONS = -3,
    GX5125_ERR_FILTER = -4,
    GX5125_ERR_MASK = -5,
    GX5125_ERR_OPTIONAL_IMAGE = -6,
    GX5125_ERR_FEATURE_MAP = -7,
    GX5125_ERR_AUX_METADATA = -8,
    GX5125_ERR_GENERATOR = -9,
    GX5125_ERR_LOW_FEATURE_COUNT = -10,
    GX5125_ERR_POSTPROCESS = -11,
    GX5125_ERR_UNSUPPORTED_AUX_OVERLAY = -12,
    GX5125_ERR_INTERNAL = -13
} gx5125_status;

typedef struct gx5125_extractor_config {
    uint32_t map_mode;
    uint32_t optional_image_enabled;
    uint32_t enroll_capacity;
    uint32_t identify_capacity;
} gx5125_extractor_config;

typedef struct gx5125_extract_stats {
    uint32_t record_count;
    uint32_t quality;
    uint32_t coverage;
    uint32_t mask_coverage_q16;
    uint32_t positive_map_percent;
    uint32_t map_state;
    uint32_t nonpositive_record_count;
    int32_t auxiliary_metric;
    uint32_t auxiliary_history_count;
    uint32_t auxiliary_class_one_pixels;
    uint32_t auxiliary_class_two_pixels;
    uint32_t auxiliary_dark_hole_pixels;
    uint32_t auxiliary_anomaly_flag;
} gx5125_extract_stats;

typedef struct gx5125_feature_view {
    uint32_t width;
    uint32_t height;
    uint32_t record_count;
    const uint8_t *records;
    size_t record_bytes;
    const uint8_t *packed_map;
    size_t packed_map_bytes;
    const uint8_t *auxiliary_map;
    size_t auxiliary_map_bytes;
    const uint8_t *mask_map;
    const uint8_t *post_map;
    const uint8_t *gray_map;
    size_t full_map_bytes;
} gx5125_feature_view;

typedef struct gx5125_extractor gx5125_extractor;
typedef struct gx5125_feature gx5125_feature;

void gx5125_extractor_default_config(gx5125_extractor_config *config);

gx5125_extractor *gx5125_extractor_create(
    const gx5125_extractor_config *config);
void gx5125_extractor_destroy(gx5125_extractor *extractor);
void gx5125_extractor_reset_state(gx5125_extractor *extractor);

int gx5125_extractor_extract_gray(
    gx5125_extractor *extractor,
    const uint8_t gray[GX5125_EXTRACTOR_PIXELS],
    uint32_t quality,
    uint32_t coverage,
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_extract_stats *stats);

void gx5125_feature_destroy(gx5125_feature *feature);
int gx5125_feature_get_view(const gx5125_feature *feature,
                            gx5125_feature_view *view);

const char *gx5125_status_string(int status);
int gx5125_extractor_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
