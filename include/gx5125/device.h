#ifndef GX5125_DEVICE_H
#define GX5125_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX5125_DEVICE_VERSION "0.3.1"
#define GX5125_SUPPORTED_SENSOR_FAMILY "ChicagoHS"
#define GX5125_SUPPORTED_CHIP_ID UINT16_C(0x2504)
#define GX5125_SUPPORTED_SENSOR_PROFILE UINT8_C(0x0c)
#define GX5125_DEVICE_IMAGE_WIDTH 64U
#define GX5125_DEVICE_IMAGE_HEIGHT 80U
#define GX5125_DEVICE_IMAGE_PIXELS \
    (GX5125_DEVICE_IMAGE_WIDTH * GX5125_DEVICE_IMAGE_HEIGHT)
#define GX5125_DEVICE_RAW16_BYTES \
    (GX5125_DEVICE_IMAGE_PIXELS * sizeof(uint16_t))
#define GX5125_DEFAULT_PSK_PATH "/etc/goodix-27c6-5125/psk.hex"

typedef enum gx5125_device_state {
    GX5125_DEVICE_CLOSED = 0,
    GX5125_DEVICE_USB_OPEN,
    GX5125_DEVICE_INTERFACES_CLAIMED,
    GX5125_DEVICE_RECEIVER_RUNNING,
    GX5125_DEVICE_SENSOR_READY,
    GX5125_DEVICE_TLS_READY,
    GX5125_DEVICE_ACTIVE,
    GX5125_DEVICE_CAPTURING,
    GX5125_DEVICE_ERROR
} gx5125_device_state;

typedef enum gx5125_device_status {
    GX5125_DEVICE_OK = 0,
    GX5125_DEVICE_ERR_ARGUMENT = -1000,
    GX5125_DEVICE_ERR_STATE = -1001,
    GX5125_DEVICE_ERR_MEMORY = -1002,
    GX5125_DEVICE_ERR_PSK = -1003,
    GX5125_DEVICE_ERR_USB_OPEN = -1004,
    GX5125_DEVICE_ERR_USB_LAYOUT = -1005,
    GX5125_DEVICE_ERR_USB_CLAIM = -1006,
    GX5125_DEVICE_ERR_RECEIVER = -1007,
    GX5125_DEVICE_ERR_PREFLIGHT = -1008,
    GX5125_DEVICE_ERR_SENSOR_INIT = -1009,
    GX5125_DEVICE_ERR_TLS = -1010,
    GX5125_DEVICE_ERR_CAPTURE = -1011,
    GX5125_DEVICE_ERR_CRC = -1012,
    GX5125_DEVICE_ERR_CANCELLED = -1013
} gx5125_device_status;

typedef struct gx5125_device_config {
    const char *psk_path;
    unsigned int command_timeout_ms;
    unsigned int tls_timeout_ms;
    unsigned int capture_timeout_ms;
    unsigned int capture_attempts;
    unsigned int retry_delay_ms;
    int libusb_log_level;
} gx5125_device_config;

typedef struct gx5125_device_info {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t chip_id;
    uint8_t sensor_profile;
    uint32_t width;
    uint32_t height;
    uint32_t raw16_bytes;
    uint8_t otp_cp_crc_ok;
    uint8_t otp_ft_crc_ok;
    uint8_t otp_mt_crc_ok;
    uint8_t otp_dac_valid;
    uint8_t tls_identity_valid;
    uint8_t mcu_state_flags;
} gx5125_device_info;

typedef struct gx5125_capture_metadata {
    unsigned int attempt;
    unsigned int requests_sent;
    size_t tls_plaintext_bytes;
    uint8_t encrypted_ack_received;
    uint8_t encrypted_ack_flags;
    uint8_t response_cmd0;
    uint8_t response_cmd1;
    uint8_t crc_ok;
    uint32_t calculated_crc;
    uint32_t stored_crc;
    uint16_t minimum;
    uint16_t maximum;
    double mean;
    double standard_deviation;
} gx5125_capture_metadata;

typedef struct gx5125_device gx5125_device;

void gx5125_device_default_config(gx5125_device_config *config);

gx5125_device *gx5125_device_create(const gx5125_device_config *config);
void gx5125_device_destroy(gx5125_device *device);

int gx5125_device_open(gx5125_device *device);
int gx5125_device_capture_raw16(
    gx5125_device *device,
    uint16_t pixels[GX5125_DEVICE_IMAGE_PIXELS],
    gx5125_capture_metadata *metadata);
void gx5125_device_request_cancel(gx5125_device *device);
void gx5125_device_clear_cancel(gx5125_device *device);
void gx5125_device_close(gx5125_device *device);

gx5125_device_state gx5125_device_get_state(const gx5125_device *device);
int gx5125_device_get_info(const gx5125_device *device,
                           gx5125_device_info *info);
const char *gx5125_device_state_string(gx5125_device_state state);
const char *gx5125_device_status_string(int status);
int gx5125_device_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
