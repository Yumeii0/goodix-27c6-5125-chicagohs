#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx5125/device.h"

#define EXPECTED_VID UINT16_C(0x27c6)
#define EXPECTED_PID UINT16_C(0x5125)
#define EXPECTED_CHIP_ID GX5125_SUPPORTED_CHIP_ID
#define EXPECTED_SENSOR_PROFILE GX5125_SUPPORTED_SENSOR_PROFILE
#define EXPECTED_WIDTH UINT32_C(64)
#define EXPECTED_HEIGHT UINT32_C(80)
#define EXPECTED_RAW16_BYTES UINT32_C(10240)

static int info_is_chicagohs_compatible(const gx5125_device_info *info)
{
    if (info == NULL) {
        return 0;
    }
    return info->vendor_id == EXPECTED_VID &&
           info->product_id == EXPECTED_PID &&
           info->chip_id == EXPECTED_CHIP_ID &&
           info->sensor_profile == EXPECTED_SENSOR_PROFILE &&
           info->width == EXPECTED_WIDTH &&
           info->height == EXPECTED_HEIGHT &&
           info->raw16_bytes == EXPECTED_RAW16_BYTES &&
           info->otp_dac_valid == 1u &&
           info->tls_identity_valid == 1u;
}

static int run_selftest(void)
{
    gx5125_device_info info;

    memset(&info, 0, sizeof(info));
    info.vendor_id = EXPECTED_VID;
    info.product_id = EXPECTED_PID;
    info.chip_id = EXPECTED_CHIP_ID;
    info.sensor_profile = EXPECTED_SENSOR_PROFILE;
    info.width = EXPECTED_WIDTH;
    info.height = EXPECTED_HEIGHT;
    info.raw16_bytes = EXPECTED_RAW16_BYTES;
    info.otp_dac_valid = 1u;
    info.tls_identity_valid = 1u;

    if (!info_is_chicagohs_compatible(&info)) {
        return 1;
    }
    info.sensor_profile = UINT8_C(0x00);
    if (info_is_chicagohs_compatible(&info)) {
        return 1;
    }
    info.sensor_profile = EXPECTED_SENSOR_PROFILE;
    info.chip_id = UINT16_C(0x0000);
    if (info_is_chicagohs_compatible(&info)) {
        return 1;
    }
    printf("GOODIX_BETA_CHICAGOHS_PROBE_SELFTEST=PASS "
           "vid:0x%04x pid:0x%04x chip:0x%04x profile:0x%02x "
           "geometry:%ux%u raw16_bytes:%u\n",
           (unsigned int)EXPECTED_VID,
           (unsigned int)EXPECTED_PID,
           (unsigned int)EXPECTED_CHIP_ID,
           (unsigned int)EXPECTED_SENSOR_PROFILE,
           (unsigned int)EXPECTED_WIDTH,
           (unsigned int)EXPECTED_HEIGHT,
           (unsigned int)EXPECTED_RAW16_BYTES);
    return 0;
}

int main(int argc, char **argv)
{
    gx5125_device_config config;
    gx5125_device_info info;
    gx5125_device *device = NULL;
    int status;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--selftest") == 0) {
        return run_selftest();
    }
    if (argc != 2 || argv[1][0] == '\0') {
        fprintf(stderr, "usage: %s <psk-path>\n", argv[0]);
        return 2;
    }

    gx5125_device_default_config(&config);
    config.psk_path = argv[1];
    config.capture_attempts = 1u;
    device = gx5125_device_create(&config);
    if (device == NULL) {
        fprintf(stderr, "GOODIX_BETA_CHICAGOHS_PROBE=FAIL stage:create-device\n");
        return 1;
    }

    status = gx5125_device_open(device);
    if (status != GX5125_DEVICE_OK) {
        fprintf(stderr,
                "GOODIX_BETA_CHICAGOHS_PROBE=FAIL stage:open status:%d detail:%s\n",
                status, gx5125_device_status_string(status));
        goto cleanup;
    }
    status = gx5125_device_get_info(device, &info);
    if (status != GX5125_DEVICE_OK) {
        fprintf(stderr,
                "GOODIX_BETA_CHICAGOHS_PROBE=FAIL stage:device-info status:%d\n",
                status);
        goto cleanup;
    }
    if (!info_is_chicagohs_compatible(&info)) {
        fprintf(stderr,
                "GOODIX_BETA_CHICAGOHS_PROBE=FAIL stage:unsupported-profile "
                "vid:0x%04x pid:0x%04x chip:0x%04x profile:0x%02x "
                "geometry:%ux%u raw16_bytes:%u otp_dac:%u tls_identity:%u\n",
                (unsigned int)info.vendor_id,
                (unsigned int)info.product_id,
                (unsigned int)info.chip_id,
                (unsigned int)info.sensor_profile,
                (unsigned int)info.width,
                (unsigned int)info.height,
                (unsigned int)info.raw16_bytes,
                (unsigned int)info.otp_dac_valid,
                (unsigned int)info.tls_identity_valid);
        goto cleanup;
    }

    printf("GOODIX_BETA_CHICAGOHS_PROBE=PASS family:ChicagoHS "
           "vid:0x%04x pid:0x%04x chip:0x%04x profile:0x%02x "
           "geometry:%ux%u raw16_bytes:%u otp_dac:%u tls_identity:%u "
           "biometric_capture:0 psk_printed:0\n",
           (unsigned int)info.vendor_id,
           (unsigned int)info.product_id,
           (unsigned int)info.chip_id,
           (unsigned int)info.sensor_profile,
           (unsigned int)info.width,
           (unsigned int)info.height,
           (unsigned int)info.raw16_bytes,
           (unsigned int)info.otp_dac_valid,
           (unsigned int)info.tls_identity_valid);
    result = 0;

cleanup:
    gx5125_device_destroy(device);
    return result;
}
