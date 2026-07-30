#ifndef GX5125_PIPELINE_H
#define GX5125_PIPELINE_H

#include <stdint.h>

#include "gx5125/device.h"
#include "gx5125/extractor.h"
#include "gx5125/preprocess.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX5125_PIPELINE_VERSION "0.4.0"

typedef enum gx5125_pipeline_state {
    GX5125_PIPELINE_CLOSED = 0,
    GX5125_PIPELINE_READY,
    GX5125_PIPELINE_CAPTURING,
    GX5125_PIPELINE_ERROR
} gx5125_pipeline_state;

typedef enum gx5125_pipeline_status {
    GX5125_PIPELINE_OK = 0,
    GX5125_PIPELINE_ERR_ARGUMENT = -2000,
    GX5125_PIPELINE_ERR_MEMORY = -2001,
    GX5125_PIPELINE_ERR_STATE = -2002,
    GX5125_PIPELINE_ERR_DEVICE = -2003,
    GX5125_PIPELINE_ERR_BASE_CAPTURE = -2004,
    GX5125_PIPELINE_ERR_BASE_INVALID = -2005,
    GX5125_PIPELINE_ERR_FINGER_CAPTURE = -2006,
    GX5125_PIPELINE_ERR_PREPROCESS = -2007,
    GX5125_PIPELINE_ERR_LOW_QUALITY = -2008,
    GX5125_PIPELINE_ERR_EXTRACT = -2009,
    GX5125_PIPELINE_ERR_CANCELLED = -2010
} gx5125_pipeline_status;

typedef struct gx5125_pipeline_config {
    gx5125_device_config device;
    gx_preproc_params preprocessor;
    gx5125_extractor_config extractor;
    uint8_t minimum_quality;
    uint8_t minimum_coverage;
} gx5125_pipeline_config;

typedef struct gx5125_pipeline_result {
    gx5125_capture_metadata capture;
    gx_preproc_metrics preprocess;
    gx5125_extract_stats extract;
} gx5125_pipeline_result;

typedef enum gx5125_finger_state {
    GX5125_FINGER_UNKNOWN = 0,
    GX5125_FINGER_ABSENT,
    GX5125_FINGER_PRESENT
} gx5125_finger_state;

typedef struct gx5125_presence_result {
    gx5125_capture_metadata capture;
    gx_preproc_metrics preprocess;
    gx5125_finger_state state;
} gx5125_presence_result;

typedef struct gx5125_pipeline gx5125_pipeline;

void gx5125_pipeline_default_config(gx5125_pipeline_config *config);

gx5125_pipeline *gx5125_pipeline_create(
    const gx5125_pipeline_config *config);
void gx5125_pipeline_destroy(gx5125_pipeline *pipeline);

int gx5125_pipeline_open(gx5125_pipeline *pipeline);
void gx5125_pipeline_close(gx5125_pipeline *pipeline);

int gx5125_pipeline_capture_base(
    gx5125_pipeline *pipeline,
    gx5125_capture_metadata *metadata);
int gx5125_pipeline_capture_feature(
    gx5125_pipeline *pipeline,
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_pipeline_result *result);

int gx5125_pipeline_set_base_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t base[GX5125_DEVICE_IMAGE_PIXELS]);
int gx5125_pipeline_process_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t current[GX5125_DEVICE_IMAGE_PIXELS],
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_pipeline_result *result);

int gx5125_pipeline_process_presence_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t current[GX5125_DEVICE_IMAGE_PIXELS],
    gx5125_presence_result *result);
int gx5125_pipeline_capture_presence(
    gx5125_pipeline *pipeline,
    gx5125_presence_result *result);

int gx5125_pipeline_reset_processing(gx5125_pipeline *pipeline);
int gx5125_pipeline_reset_extractor_state(gx5125_pipeline *pipeline);
void gx5125_pipeline_request_cancel(gx5125_pipeline *pipeline);
void gx5125_pipeline_clear_cancel(gx5125_pipeline *pipeline);

gx5125_pipeline_state gx5125_pipeline_get_state(
    const gx5125_pipeline *pipeline);
int gx5125_pipeline_has_base(const gx5125_pipeline *pipeline);
int gx5125_pipeline_get_device_info(const gx5125_pipeline *pipeline,
                                    gx5125_device_info *info);
const char *gx5125_pipeline_state_string(gx5125_pipeline_state state);
const char *gx5125_pipeline_status_string(int status);
int gx5125_pipeline_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
