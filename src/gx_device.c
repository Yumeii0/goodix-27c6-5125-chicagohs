#include "gx5125/device.h"

#include <limits.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libusb-1.0/libusb.h>

#include "gx5125/capture.h"
#include "gx5125/protocol.h"
#include "gx5125/secret.h"
#include "gx5125/sensor.h"
#include "gx5125/session.h"
#include "gx5125/tls.h"
#include "gx5125/usb.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


struct gx5125_device {
    gx5125_device_config config;
    char psk_path[PATH_MAX];
    gx_usb_device usb;
    gx_session session;
    gx_tls_server tls;
    gx_otp_info otp;
    gx5125_device_info info;
    atomic_bool cancel_requested;
    gx5125_device_state state;
    bool session_started;
    bool tls_initialized;
};

static void sleep_ms(unsigned int milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        continue;
    }
}

static uint16_t minute_timestamp(void)
{
    struct timespec now;
    struct tm local;
    time_t seconds;
    unsigned int milliseconds;

    (void)clock_gettime(CLOCK_REALTIME, &now);
    seconds = now.tv_sec;
    (void)localtime_r(&seconds, &local);
    milliseconds = (unsigned int)(now.tv_nsec / 1000000L);
    return (uint16_t)((unsigned int)local.tm_sec * 1000u + milliseconds);
}

static int get_mcu_state(gx5125_device *device, uint8_t *flags)
{
    gx_command_result result;
    uint8_t payload[5] = {0x55, 0x00, 0x00, 0x00, 0x00};
    const uint16_t timestamp = minute_timestamp();
    int rc;

    payload[1] = (uint8_t)(timestamp & UINT16_C(0x00ff));
    payload[2] = (uint8_t)(timestamp >> 8);
    rc = gx_session_command(&device->session,
                            0x0a, 0x07,
                            payload, sizeof(payload),
                            0u, true,
                            device->config.command_timeout_ms,
                            3u, &result);
    if (rc < 0 || !result.response_received || result.response_length < 2u) {
        return rc < 0 ? rc : LIBUSB_ERROR_IO;
    }
    *flags = result.response[1];
    return LIBUSB_SUCCESS;
}

static int control_preflight(gx5125_device *device)
{
    static const uint8_t zero2[2] = {0x00, 0x00};
    static const uint8_t idle_payload[2] = {0x14, 0x00};
    static const uint8_t chip_payload[5] = {0x00, 0x00, 0x00, 0x04, 0x00};
    gx_command_result result;
    uint16_t chip_id;
    int rc;

    rc = gx_session_command(&device->session, 0x0a, 0x04,
                            zero2, sizeof(zero2),
                            device->config.command_timeout_ms,
                            true, device->config.command_timeout_ms,
                            3u, &result);
    if (rc < 0 || !result.response_received) {
        return rc < 0 ? rc : LIBUSB_ERROR_IO;
    }

    rc = gx_session_command(&device->session, 0x07, 0x00,
                            idle_payload, sizeof(idle_payload),
                            0u, false, 0u, 1u, &result);
    if (rc < 0) {
        return rc;
    }
    sleep_ms(20u);

    rc = gx_session_command(&device->session, 0x08, 0x01,
                            chip_payload, sizeof(chip_payload),
                            device->config.command_timeout_ms,
                            true, device->config.command_timeout_ms,
                            3u, &result);
    if (rc < 0 || !result.response_received || result.response_length < 3u) {
        return rc < 0 ? rc : LIBUSB_ERROR_IO;
    }
    chip_id = (uint16_t)result.response[1] |
              (uint16_t)((uint16_t)result.response[2] << 8u);
    if (chip_id != GX5125_SUPPORTED_CHIP_ID) {
        return LIBUSB_ERROR_NOT_SUPPORTED;
    }
    device->info.chip_id = chip_id;
    return LIBUSB_SUCCESS;
}

static int sensor_initialize(gx5125_device *device)
{
    static const uint8_t zero2[2] = {0x00, 0x00};
    gx_command_result result;
    uint8_t otp_bytes[GX5125_OTP_SIZE];
    uint8_t config[GX5125_CONFIG_SIZE];
    int rc;

    memset(otp_bytes, 0, sizeof(otp_bytes));
    memset(config, 0, sizeof(config));
    memset(&device->otp, 0, sizeof(device->otp));

    rc = gx_session_command(&device->session, 0x0a, 0x03,
                            zero2, sizeof(zero2),
                            device->config.command_timeout_ms,
                            true, device->config.command_timeout_ms + 500u,
                            3u, &result);
    if (rc < 0 || !result.response_received ||
        result.response_length < GX5125_OTP_SIZE) {
        rc = rc < 0 ? rc : LIBUSB_ERROR_IO;
        goto cleanup;
    }
    memcpy(otp_bytes, result.response, GX5125_OTP_SIZE);
    rc = gx_otp_parse(otp_bytes, &device->otp);
    if (rc < 0) {
        goto cleanup;
    }
    rc = gx_config_prepare(&device->otp, config);
    if (rc < 0) {
        goto cleanup;
    }
    rc = gx_session_command(&device->session, 0x09, 0x00,
                            config, sizeof(config),
                            device->config.command_timeout_ms,
                            true, device->config.command_timeout_ms + 500u,
                            3u, &result);
    if (rc < 0) {
        goto cleanup;
    }
    if (result.response_received && result.response_length >= 1u &&
        result.response[0] != 1u) {
        rc = LIBUSB_ERROR_IO;
        goto cleanup;
    }

    device->info.otp_cp_crc_ok = device->otp.cp_crc_ok ? 1u : 0u;
    device->info.otp_ft_crc_ok = device->otp.ft_crc_ok ? 1u : 0u;
    device->info.otp_mt_crc_ok = device->otp.mt_crc_ok ? 1u : 0u;
    device->info.otp_dac_valid = device->otp.dac_valid ? 1u : 0u;
    rc = LIBUSB_SUCCESS;

cleanup:
    gx_secret_cleanse(otp_bytes, sizeof(otp_bytes));
    gx_secret_cleanse(config, sizeof(config));
    return rc;
}

static int tls_connect(gx5125_device *device)
{
    static const uint8_t zero2[2] = {0x00, 0x00};
    gx_command_result result;
    unsigned int attempt;
    int rc;

    rc = gx_tls_server_handshake_usb(&device->tls,
                                     &device->session,
                                     device->config.tls_timeout_ms);
    if (rc < 0) {
        return rc;
    }

    rc = gx_session_command(&device->session, 0x0d, 0x02,
                            zero2, sizeof(zero2),
                            200u, false, 0u, 2u, &result);
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
        return rc;
    }

    for (attempt = 1u; attempt <= 3u; ++attempt) {
        uint8_t flags = 0u;
        sleep_ms(20u);
        rc = get_mcu_state(device, &flags);
        if (rc < 0) {
            continue;
        }
        if (((flags >> 1u) & 1u) != 0u && ((flags >> 3u) & 1u) == 0u) {
            device->info.mcu_state_flags = flags;
            device->info.tls_identity_valid = device->tls.identity_valid ? 1u : 0u;
            return LIBUSB_SUCCESS;
        }
    }
    return LIBUSB_ERROR_ACCESS;
}

static void best_effort_idle(gx5125_device *device)
{
    static const uint8_t payload[2] = {0x14, 0x00};
    gx_command_result result;

    if (!device->session_started) {
        return;
    }
    memset(&result, 0, sizeof(result));
    (void)gx_session_command(&device->session, 0x07, 0x00,
                             payload, sizeof(payload),
                             0u, false, 0u, 1u, &result);
}

void gx5125_device_default_config(gx5125_device_config *config)
{
    if (config == NULL) {
        return;
    }
    config->psk_path = GX5125_DEFAULT_PSK_PATH;
    config->command_timeout_ms = 1500u;
    config->tls_timeout_ms = 8000u;
    config->capture_timeout_ms = 4000u;
    config->capture_attempts = 3u;
    config->retry_delay_ms = 150u;
    config->libusb_log_level = LIBUSB_LOG_LEVEL_WARNING;
}

static bool config_valid(const gx5125_device_config *config)
{
    return config != NULL && config->psk_path != NULL &&
           config->psk_path[0] != '\0' &&
           config->command_timeout_ms != 0u &&
           config->tls_timeout_ms != 0u &&
           config->capture_timeout_ms != 0u &&
           config->capture_attempts != 0u &&
           strlen(config->psk_path) < PATH_MAX;
}

gx5125_device *gx5125_device_create(const gx5125_device_config *config)
{
    gx5125_device_config resolved;
    gx5125_device *device;

    if (config == NULL) {
        gx5125_device_default_config(&resolved);
        config = &resolved;
    }
    if (!config_valid(config)) {
        return NULL;
    }
    device = calloc(1u, sizeof(*device));
    if (device == NULL) {
        return NULL;
    }
    device->config = *config;
    memcpy(device->psk_path, config->psk_path, strlen(config->psk_path) + 1u);
    device->config.psk_path = device->psk_path;
    atomic_init(&device->cancel_requested, false);
    device->state = GX5125_DEVICE_CLOSED;
    device->info.vendor_id = GX5125_USB_VID;
    device->info.product_id = GX5125_USB_PID;
    device->info.sensor_profile = GX5125_SUPPORTED_SENSOR_PROFILE;
    device->info.width = GX5125_IMAGE_WIDTH;
    device->info.height = GX5125_IMAGE_HEIGHT;
    device->info.raw16_bytes = GX5125_IMAGE_RAW16_SIZE;
    return device;
}

void gx5125_device_destroy(gx5125_device *device)
{
    if (device == NULL) {
        return;
    }
    gx5125_device_close(device);
    gx_secret_cleanse(device, sizeof(*device));
    free(device);
}

int gx5125_device_open(gx5125_device *device)
{
    uint8_t psk[GX5125_PSK_SIZE];
    int rc;
    int result = GX5125_DEVICE_ERR_STATE;

    if (device == NULL) {
        return GX5125_DEVICE_ERR_ARGUMENT;
    }
    if (device->state != GX5125_DEVICE_CLOSED) {
        return GX5125_DEVICE_ERR_STATE;
    }
    memset(psk, 0, sizeof(psk));
    atomic_store_explicit(&device->cancel_requested, false,
                          memory_order_relaxed);

    if (gx_secret_read_psk_file(device->psk_path, psk) != 0) {
        result = GX5125_DEVICE_ERR_PSK;
        goto fail;
    }
    if (gx_tls_server_init(&device->tls, psk) != 0) {
        result = GX5125_DEVICE_ERR_TLS;
        goto fail;
    }
    device->tls_initialized = true;
    gx_secret_cleanse(psk, sizeof(psk));

    rc = gx_usb_open(&device->usb, device->config.libusb_log_level);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_USB_OPEN;
        goto fail;
    }
    device->state = GX5125_DEVICE_USB_OPEN;

    rc = gx_usb_validate_layout(&device->usb);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_USB_LAYOUT;
        goto fail;
    }
    rc = gx_usb_claim_interfaces(&device->usb);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_USB_CLAIM;
        goto fail;
    }
    device->state = GX5125_DEVICE_INTERFACES_CLAIMED;

    rc = gx_session_start(&device->session, &device->usb);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_RECEIVER;
        goto fail;
    }
    device->session_started = true;
    device->state = GX5125_DEVICE_RECEIVER_RUNNING;

    rc = control_preflight(device);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_PREFLIGHT;
        goto fail;
    }
    rc = sensor_initialize(device);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_SENSOR_INIT;
        goto fail;
    }
    device->state = GX5125_DEVICE_SENSOR_READY;

    rc = tls_connect(device);
    if (rc < 0) {
        result = GX5125_DEVICE_ERR_TLS;
        goto fail;
    }
    device->state = GX5125_DEVICE_TLS_READY;
    device->state = GX5125_DEVICE_ACTIVE;
    return GX5125_DEVICE_OK;

fail:
    gx_secret_cleanse(psk, sizeof(psk));
    device->state = GX5125_DEVICE_ERROR;
    gx5125_device_close(device);
    return result;
}

int gx5125_device_capture_raw16(
    gx5125_device *device,
    uint16_t pixels[GX5125_IMAGE_PIXELS],
    gx5125_capture_metadata *metadata)
{
    uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE];
    gx_capture_result capture_result;
    gx_image_stats image_stats;
    bool crc_ok = false;
    uint32_t calculated_crc = 0u;
    uint32_t stored_crc = 0u;
    unsigned int attempt;
    int rc;

    if (device == NULL || pixels == NULL || metadata == NULL) {
        return GX5125_DEVICE_ERR_ARGUMENT;
    }
    if (device->state != GX5125_DEVICE_ACTIVE) {
        return GX5125_DEVICE_ERR_STATE;
    }
    memset(metadata, 0, sizeof(*metadata));
    memset(pixels, 0, GX5125_IMAGE_RAW16_SIZE);
    memset(raw_frame, 0, sizeof(raw_frame));
    memset(&capture_result, 0, sizeof(capture_result));
    memset(&image_stats, 0, sizeof(image_stats));
    device->state = GX5125_DEVICE_CAPTURING;

    for (attempt = 1u; attempt <= device->config.capture_attempts; ++attempt) {
        if (atomic_load_explicit(&device->cancel_requested,
                                 memory_order_relaxed)) {
            device->state = GX5125_DEVICE_ACTIVE;
            return GX5125_DEVICE_ERR_CANCELLED;
        }
        memset(raw_frame, 0, sizeof(raw_frame));
        memset(&capture_result, 0, sizeof(capture_result));
        rc = gx_capture_image_cancelable(&device->session,
                                         &device->tls,
                                         raw_frame,
                                         device->config.capture_timeout_ms,
                                         1u,
                                         &device->cancel_requested,
                                         &capture_result);
        if (rc == LIBUSB_ERROR_INTERRUPTED) {
            gx_secret_cleanse(raw_frame, sizeof(raw_frame));
            device->state = GX5125_DEVICE_ACTIVE;
            return GX5125_DEVICE_ERR_CANCELLED;
        }
        if (rc < 0) {
            if (attempt < device->config.capture_attempts) {
                sleep_ms(device->config.retry_delay_ms);
                continue;
            }
            gx_secret_cleanse(raw_frame, sizeof(raw_frame));
            device->state = GX5125_DEVICE_ACTIVE;
            return GX5125_DEVICE_ERR_CAPTURE;
        }

        rc = gx_chicagohu_decode(raw_frame, pixels, &crc_ok,
                                 &calculated_crc, &stored_crc);
        if (rc == LIBUSB_SUCCESS && crc_ok) {
            gx_image_calculate_stats(pixels, &image_stats);
            metadata->attempt = attempt;
            metadata->requests_sent = capture_result.requests_sent;
            metadata->tls_plaintext_bytes = capture_result.plaintext_bytes;
            metadata->encrypted_ack_received =
                capture_result.encrypted_ack_received ? 1u : 0u;
            metadata->encrypted_ack_flags = capture_result.encrypted_ack_flags;
            metadata->response_cmd0 = capture_result.response_cmd0;
            metadata->response_cmd1 = capture_result.response_cmd1;
            metadata->crc_ok = 1u;
            metadata->calculated_crc = calculated_crc;
            metadata->stored_crc = stored_crc;
            metadata->minimum = image_stats.minimum;
            metadata->maximum = image_stats.maximum;
            metadata->mean = image_stats.mean;
            metadata->standard_deviation = image_stats.standard_deviation;
            gx_secret_cleanse(raw_frame, sizeof(raw_frame));
            device->state = GX5125_DEVICE_ACTIVE;
            return GX5125_DEVICE_OK;
        }
        memset(pixels, 0, GX5125_IMAGE_RAW16_SIZE);
        if (attempt < device->config.capture_attempts) {
            sleep_ms(device->config.retry_delay_ms);
        }
    }

    metadata->attempt = device->config.capture_attempts;
    metadata->crc_ok = 0u;
    metadata->calculated_crc = calculated_crc;
    metadata->stored_crc = stored_crc;
    gx_secret_cleanse(raw_frame, sizeof(raw_frame));
    device->state = GX5125_DEVICE_ACTIVE;
    return GX5125_DEVICE_ERR_CRC;
}

void gx5125_device_request_cancel(gx5125_device *device)
{
    if (device != NULL) {
        atomic_store_explicit(&device->cancel_requested, true,
                              memory_order_relaxed);
    }
}

void gx5125_device_clear_cancel(gx5125_device *device)
{
    if (device != NULL) {
        atomic_store_explicit(&device->cancel_requested, false,
                              memory_order_relaxed);
    }
}

void gx5125_device_close(gx5125_device *device)
{
    if (device == NULL) {
        return;
    }
    atomic_store_explicit(&device->cancel_requested, true,
                          memory_order_relaxed);
    best_effort_idle(device);
    if (device->session_started) {
        gx_session_stop(&device->session);
        device->session_started = false;
    }
    gx_usb_close(&device->usb);
    if (device->tls_initialized) {
        gx_tls_server_cleanup(&device->tls);
        device->tls_initialized = false;
    }
    memset(&device->session, 0, sizeof(device->session));
    memset(&device->usb, 0, sizeof(device->usb));
    memset(&device->tls, 0, sizeof(device->tls));
    memset(&device->otp, 0, sizeof(device->otp));
    device->info.chip_id = 0u;
    device->info.otp_cp_crc_ok = 0u;
    device->info.otp_ft_crc_ok = 0u;
    device->info.otp_mt_crc_ok = 0u;
    device->info.otp_dac_valid = 0u;
    device->info.tls_identity_valid = 0u;
    device->info.mcu_state_flags = 0u;
    device->state = GX5125_DEVICE_CLOSED;
}

gx5125_device_state gx5125_device_get_state(const gx5125_device *device)
{
    return device == NULL ? GX5125_DEVICE_ERROR : device->state;
}

int gx5125_device_get_info(const gx5125_device *device,
                           gx5125_device_info *info)
{
    if (device == NULL || info == NULL) {
        return GX5125_DEVICE_ERR_ARGUMENT;
    }
    *info = device->info;
    return GX5125_DEVICE_OK;
}

const char *gx5125_device_state_string(gx5125_device_state state)
{
    switch (state) {
    case GX5125_DEVICE_CLOSED: return "closed";
    case GX5125_DEVICE_USB_OPEN: return "usb-open";
    case GX5125_DEVICE_INTERFACES_CLAIMED: return "interfaces-claimed";
    case GX5125_DEVICE_RECEIVER_RUNNING: return "receiver-running";
    case GX5125_DEVICE_SENSOR_READY: return "sensor-ready";
    case GX5125_DEVICE_TLS_READY: return "tls-ready";
    case GX5125_DEVICE_ACTIVE: return "active";
    case GX5125_DEVICE_CAPTURING: return "capturing";
    case GX5125_DEVICE_ERROR: return "error";
    default: return "unknown";
    }
}

const char *gx5125_device_status_string(int status)
{
    switch (status) {
    case GX5125_DEVICE_OK: return "ok";
    case GX5125_DEVICE_ERR_ARGUMENT: return "invalid argument";
    case GX5125_DEVICE_ERR_STATE: return "invalid state";
    case GX5125_DEVICE_ERR_MEMORY: return "memory allocation failed";
    case GX5125_DEVICE_ERR_PSK: return "PSK file rejected";
    case GX5125_DEVICE_ERR_USB_OPEN: return "USB open failed";
    case GX5125_DEVICE_ERR_USB_LAYOUT: return "USB endpoint layout rejected";
    case GX5125_DEVICE_ERR_USB_CLAIM: return "USB interface claim failed";
    case GX5125_DEVICE_ERR_RECEIVER: return "USB receiver failed";
    case GX5125_DEVICE_ERR_PREFLIGHT: return "device preflight failed";
    case GX5125_DEVICE_ERR_SENSOR_INIT: return "sensor initialization failed";
    case GX5125_DEVICE_ERR_TLS: return "TLS initialization failed";
    case GX5125_DEVICE_ERR_CAPTURE: return "capture timed out or failed";
    case GX5125_DEVICE_ERR_CRC: return "captured frame CRC failed";
    case GX5125_DEVICE_ERR_CANCELLED: return "operation cancelled";
    default: return "unknown device error";
    }
}

static void store_crc_wire(uint8_t *target, uint32_t crc)
{
    target[0] = (uint8_t)((crc >> 8u) & 0xffu);
    target[1] = (uint8_t)(crc & 0xffu);
    target[2] = (uint8_t)(crc >> 24u);
    target[3] = (uint8_t)((crc >> 16u) & 0xffu);
}

int gx5125_device_selftest(void)
{
    gx5125_device_config config;
    gx5125_device *device;
    uint8_t raw[GX5125_IMAGE_RAW_FRAME_SIZE];
    uint16_t pixels[GX5125_IMAGE_PIXELS];
    bool crc_ok = false;
    uint32_t calculated = 0u;
    uint32_t stored = 0u;
    uint32_t crc;
    int rc;

    gx5125_device_default_config(&config);
    if (!config_valid(&config) ||
        strcmp(gx5125_device_state_string(GX5125_DEVICE_ACTIVE), "active") != 0 ||
        strcmp(gx5125_device_status_string(GX5125_DEVICE_ERR_CRC),
               "captured frame CRC failed") != 0) {
        return -1;
    }
    device = gx5125_device_create(&config);
    if (device == NULL || gx5125_device_get_state(device) != GX5125_DEVICE_CLOSED) {
        gx5125_device_destroy(device);
        return -1;
    }
    gx5125_device_request_cancel(device);
    gx5125_device_clear_cancel(device);
    gx5125_device_destroy(device);

    memset(raw, 0, sizeof(raw));
    memset(pixels, 0xff, sizeof(pixels));
    crc = gx_crc32_mpeg2(raw, GX5125_IMAGE_PACKED_SIZE);
    store_crc_wire(raw + GX5125_IMAGE_PACKED_SIZE, crc);
    rc = gx_chicagohu_decode(raw, pixels, &crc_ok, &calculated, &stored);
    if (rc != LIBUSB_SUCCESS || !crc_ok || calculated != stored || pixels[0] != 0u ||
        pixels[GX5125_IMAGE_PIXELS - 1u] != 0u) {
        return -1;
    }
    if (gx_tls_offline_selftest() != 0) {
        return -1;
    }
    return 0;
}
