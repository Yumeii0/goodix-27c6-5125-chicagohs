#ifndef GX5125_SENSOR_H
#define GX5125_SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GX5125_OTP_SIZE 64u
#define GX5125_CONFIG_SIZE 224u
#define GX5125_IMAGE_PACKED_SIZE 7680u
#define GX5125_IMAGE_RAW_FRAME_SIZE 7684u
#define GX5125_IMAGE_PIXELS 5120u
#define GX5125_IMAGE_RAW16_SIZE (GX5125_IMAGE_PIXELS * sizeof(uint16_t))
#define GX5125_IMAGE_WIDTH 64u
#define GX5125_IMAGE_HEIGHT 80u

typedef struct gx_otp_info {
    bool cp_crc_ok;
    bool ft_crc_ok;
    bool mt_crc_ok;
    bool dac_valid;
    uint16_t dac[4];
    uint16_t tcode;
    uint8_t fdt_delta;
    uint8_t fdt_offset;
} gx_otp_info;

typedef struct gx_image_stats {
    uint16_t minimum;
    uint16_t maximum;
    double mean;
    double standard_deviation;
} gx_image_stats;

extern const uint8_t gx5125_chicagohu_config_template[GX5125_CONFIG_SIZE];
extern const uint8_t gx5125_goodix_crc8_table[256];

uint8_t gx_goodix_crc8(const uint8_t *data, size_t length);
uint32_t gx_crc32_mpeg2(const uint8_t *data, size_t length);

int gx_otp_parse(const uint8_t otp[GX5125_OTP_SIZE], gx_otp_info *info);
int gx_config_prepare(const gx_otp_info *info,
                      uint8_t config[GX5125_CONFIG_SIZE]);
int gx_config_verify_checksum(const uint8_t config[GX5125_CONFIG_SIZE]);

int gx_chicagohu_decode(const uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
                        uint16_t pixels[GX5125_IMAGE_PIXELS],
                        bool *crc_ok,
                        uint32_t *calculated_crc,
                        uint32_t *stored_crc);
void gx_image_calculate_stats(const uint16_t pixels[GX5125_IMAGE_PIXELS],
                              gx_image_stats *stats);

#endif
