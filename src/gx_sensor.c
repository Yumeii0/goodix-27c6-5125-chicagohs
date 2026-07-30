#include "gx5125/sensor.h"

#include <math.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

static uint16_t gx_read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static void gx_write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & UINT16_C(0x00ff));
    data[1] = (uint8_t)(value >> 8);
}

uint8_t gx_goodix_crc8(const uint8_t *data, size_t length)
{
    uint8_t state = 0u;
    size_t index;

    if (data == NULL && length != 0u) {
        return 0u;
    }
    for (index = 0u; index < length; ++index) {
        state = gx5125_goodix_crc8_table[(uint8_t)(state ^ data[index])];
    }
    return (uint8_t)~state;
}

uint32_t gx_crc32_mpeg2(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    unsigned int bit;

    if (data == NULL && length != 0u) {
        return 0u;
    }
    for (index = 0u; index < length; ++index) {
        crc ^= (uint32_t)data[index] << 24;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & UINT32_C(0x80000000)) != 0u
                      ? (crc << 1) ^ UINT32_C(0x04c11db7)
                      : crc << 1;
        }
    }
    return crc;
}

static bool gx_otp_domain_crc(const uint8_t *otp,
                              const uint8_t *ranges,
                              size_t range_count,
                              uint8_t expected)
{
    uint8_t buffer[32];
    size_t output = 0u;
    size_t index;

    for (index = 0u; index < range_count; ++index) {
        const size_t start = ranges[index * 2u];
        const size_t length = ranges[index * 2u + 1u];
        if (output + length > sizeof(buffer)) {
            return false;
        }
        memcpy(buffer + output, otp + start, length);
        output += length;
    }
    return gx_goodix_crc8(buffer, output) == expected;
}

static bool gx_nonzero4(const uint8_t *data)
{
    return data[0] != 0u && data[1] != 0u &&
           data[2] != 0u && data[3] != 0u;
}

static bool gx_select_dac(const uint8_t otp[GX5125_OTP_SIZE],
                          bool ft_domain_ok,
                          bool mt_domain_ok,
                          uint16_t dac[4])
{
    const uint8_t *mt = otp + 0x2eu;
    const uint8_t *ft = otp + 0x32u;
    const bool ft_subcrc = gx_nonzero4(ft) &&
                           gx_goodix_crc8(ft, 4u) == otp[0x3eu];
    const bool mt_subcrc = gx_nonzero4(mt) &&
                           gx_goodix_crc8(mt, 4u) == otp[0x16u];
    size_t index;
    unsigned int matching = 0u;

    if (ft_domain_ok || ft_subcrc) {
        for (index = 0u; index < 4u; ++index) {
            dac[index] = ft[index];
        }
        return true;
    }
    if (mt_domain_ok || mt_subcrc) {
        for (index = 0u; index < 4u; ++index) {
            dac[index] = mt[index];
        }
        return true;
    }

    for (index = 0u; index < 4u; ++index) {
        if (mt[index] == ft[index] && ft[index] != 0u) {
            matching += 1u;
        }
    }
    if (matching < 3u) {
        return false;
    }
    if (matching == 4u) {
        for (index = 0u; index < 4u; ++index) {
            dac[index] = ft[index];
        }
        return true;
    }

    for (index = 0u; index < 4u; ++index) {
        if (mt[index] == ft[index]) {
            dac[index] = ft[index];
        } else {
            dac[index] = (uint16_t)(((unsigned int)mt[(index + 1u) & 3u] +
                                     (unsigned int)mt[(index + 2u) & 3u] +
                                     (unsigned int)mt[(index + 3u) & 3u]) / 3u);
        }
    }
    return true;
}

static uint8_t gx_select_tcode_source(const uint8_t otp[GX5125_OTP_SIZE])
{
    const uint8_t first = otp[0x2au];
    const uint8_t complement = otp[0x2bu];
    const uint8_t second = otp[0x2du];

    if (first != 0u && first == (uint8_t)~complement) {
        return first;
    }
    if (second != 0u && second == (uint8_t)~complement) {
        return second;
    }
    if (first != 0u && first == second) {
        return first;
    }
    return 0u;
}

static uint8_t gx_select_fdt_offset(uint8_t value)
{
    const uint8_t first = (uint8_t)(value & UINT8_C(0x03));
    const uint8_t second = (uint8_t)((value >> 4) & UINT8_C(0x03));
    const uint8_t third = (uint8_t)(((uint8_t)~value >> 2) & UINT8_C(0x03));

    if (first == second || first == third) {
        return first;
    }
    if (second == third) {
        return second;
    }
    return 0u;
}

int gx_otp_parse(const uint8_t otp[GX5125_OTP_SIZE], gx_otp_info *info)
{
    static const uint8_t cp_ranges[] = {0x00, 0x0b, 0x24, 0x04};
    static const uint8_t ft_ranges[] = {
        0x0b, 0x09, 0x1c, 0x01, 0x32, 0x04, 0x38, 0x04, 0x3e, 0x01
    };
    static const uint8_t mt_ranges[] = {
        0x14, 0x08, 0x1d, 0x07, 0x28, 0x0a, 0x36, 0x02
    };
    uint8_t source;

    if (otp == NULL || info == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));

    info->cp_crc_ok = gx_otp_domain_crc(otp, cp_ranges, 2u, otp[0x3cu]);
    info->ft_crc_ok = gx_otp_domain_crc(otp, ft_ranges, 5u, otp[0x3du]);
    info->mt_crc_ok = gx_otp_domain_crc(otp, mt_ranges, 4u, otp[0x3fu]);
    info->dac_valid = gx_select_dac(otp,
                                    info->ft_crc_ok,
                                    info->mt_crc_ok,
                                    info->dac);
    if (!info->dac_valid) {
        return LIBUSB_ERROR_IO;
    }

    info->tcode = UINT16_C(0x0080);
    info->fdt_delta = UINT8_C(0x15);
    source = gx_select_tcode_source(otp);
    if (source != 0u) {
        unsigned int interim;
        info->tcode = (uint16_t)(((unsigned int)(source >> 4) + 1u) * 0x10u + 0x40u);
        interim = ((unsigned int)(source & UINT8_C(0x0f)) + 2u) * 100u;
        info->fdt_delta = (uint8_t)(((((interim * 0x100u) /
                                       (unsigned int)info->tcode) / 3u) >> 4) & 0xffu);
    }
    info->fdt_offset = gx_select_fdt_offset(otp[0x1bu]);
    return LIBUSB_SUCCESS;
}

static int gx_config_modify(uint8_t config[GX5125_CONFIG_SIZE],
                            unsigned int section,
                            uint16_t address,
                            uint16_t value,
                            unsigned int mode)
{
    size_t start;
    size_t length;
    size_t offset;

    if (section >= 8u || mode > 2u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    start = config[1u + section * 2u];
    length = config[2u + section * 2u];
    if (start + length > GX5125_CONFIG_SIZE - 2u || (length % 4u) != 0u) {
        return LIBUSB_ERROR_IO;
    }

    for (offset = start; offset < start + length; offset += 4u) {
        if (gx_read_le16(config + offset) == address) {
            uint16_t current = gx_read_le16(config + offset + 2u);
            if (mode == 0u) {
                current = value;
            } else if (mode == 1u) {
                current = (uint16_t)((current & UINT16_C(0xff00)) |
                                     (value & UINT16_C(0x00ff)));
            } else {
                current = (uint16_t)((current & UINT16_C(0x00ff)) |
                                     (value & UINT16_C(0xff00)));
            }
            gx_write_le16(config + offset + 2u, current);
            return LIBUSB_SUCCESS;
        }
    }
    return LIBUSB_ERROR_NOT_FOUND;
}

static void gx_config_update_checksum(uint8_t config[GX5125_CONFIG_SIZE])
{
    uint16_t sum = UINT16_C(0xa5a5);
    size_t index;

    for (index = 0u; index < 0x6fu; ++index) {
        sum = (uint16_t)(sum + gx_read_le16(config + index * 2u));
    }
    gx_write_le16(config + 0xdeu, (uint16_t)(0u - sum));
}

int gx_config_verify_checksum(const uint8_t config[GX5125_CONFIG_SIZE])
{
    uint16_t sum = UINT16_C(0xa5a5);
    size_t index;

    if (config == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    for (index = 0u; index < 0x70u; ++index) {
        sum = (uint16_t)(sum + gx_read_le16(config + index * 2u));
    }
    return sum == 0u ? LIBUSB_SUCCESS : LIBUSB_ERROR_IO;
}

int gx_config_prepare(const gx_otp_info *info,
                      uint8_t config[GX5125_CONFIG_SIZE])
{
    int rc;

    if (info == NULL || config == NULL || !info->dac_valid) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    memcpy(config, gx5125_chicagohu_config_template, GX5125_CONFIG_SIZE);

    rc = gx_config_modify(config, 0u, UINT16_C(0x0220),
                          (uint16_t)(info->dac[0] << 4) | UINT16_C(0x0008), 0u);
    if (rc < 0) return rc;
    rc = gx_config_modify(config, 0u, UINT16_C(0x0236), info->dac[1], 0u);
    if (rc < 0) return rc;
    rc = gx_config_modify(config, 0u, UINT16_C(0x0238), info->dac[2], 0u);
    if (rc < 0) return rc;
    rc = gx_config_modify(config, 0u, UINT16_C(0x023a), info->dac[3], 0u);
    if (rc < 0) return rc;
    rc = gx_config_modify(config, 0u, UINT16_C(0x005c), info->tcode, 0u);
    if (rc < 0) return rc;
    rc = gx_config_modify(config, 2u, UINT16_C(0x0082),
                          (uint16_t)info->fdt_delta << 8, 2u);
    if (rc < 0) return rc;
    if (info->fdt_offset != 0u) {
        rc = gx_config_modify(config, 2u, UINT16_C(0x0056),
                              (uint16_t)(info->fdt_offset + 4u), 1u);
        if (rc < 0) return rc;
    }

    gx_config_update_checksum(config);
    return gx_config_verify_checksum(config);
}

int gx_chicagohu_decode(const uint8_t raw_frame[GX5125_IMAGE_RAW_FRAME_SIZE],
                        uint16_t pixels[GX5125_IMAGE_PIXELS],
                        bool *crc_ok,
                        uint32_t *calculated_crc,
                        uint32_t *stored_crc)
{
    uint16_t unpacked[GX5125_IMAGE_PIXELS];
    uint32_t calculated;
    uint32_t stored;
    size_t input;
    size_t output = 0u;
    size_t index;

    if (raw_frame == NULL || pixels == NULL || crc_ok == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    calculated = gx_crc32_mpeg2(raw_frame, GX5125_IMAGE_PACKED_SIZE);
    stored = ((uint32_t)raw_frame[GX5125_IMAGE_PACKED_SIZE + 2u] << 24) |
             ((uint32_t)raw_frame[GX5125_IMAGE_PACKED_SIZE + 3u] << 16) |
             ((uint32_t)raw_frame[GX5125_IMAGE_PACKED_SIZE] << 8) |
             (uint32_t)raw_frame[GX5125_IMAGE_PACKED_SIZE + 1u];
    *crc_ok = calculated == stored;
    if (calculated_crc != NULL) {
        *calculated_crc = calculated;
    }
    if (stored_crc != NULL) {
        *stored_crc = stored;
    }
    if (!*crc_ok) {
        return LIBUSB_ERROR_IO;
    }

    for (input = 0u; input < GX5125_IMAGE_PACKED_SIZE; input += 6u) {
        const uint8_t b0 = raw_frame[input];
        const uint8_t b1 = raw_frame[input + 1u];
        const uint8_t b2 = raw_frame[input + 2u];
        const uint8_t b3 = raw_frame[input + 3u];
        const uint8_t b4 = raw_frame[input + 4u];
        const uint8_t b5 = raw_frame[input + 5u];

        unpacked[output++] = (uint16_t)(((uint16_t)(b0 & UINT8_C(0x0f)) << 8) | b1);
        unpacked[output++] = (uint16_t)(((uint16_t)b3 << 4) | (b0 >> 4));
        unpacked[output++] = (uint16_t)(((uint16_t)(b5 & UINT8_C(0x0f)) << 8) | b2);
        unpacked[output++] = (uint16_t)(((uint16_t)b4 << 4) | (b5 >> 4));
    }
    if (output != GX5125_IMAGE_PIXELS) {
        return LIBUSB_ERROR_IO;
    }

    for (index = 0u; index < GX5125_IMAGE_PIXELS; ++index) {
        pixels[(index % 64u) * 80u + index / 64u] = unpacked[index];
    }
    memset(unpacked, 0, sizeof(unpacked));
    return LIBUSB_SUCCESS;
}

void gx_image_calculate_stats(const uint16_t pixels[GX5125_IMAGE_PIXELS],
                              gx_image_stats *stats)
{
    uint64_t sum = 0u;
    double variance = 0.0;
    size_t index;

    if (pixels == NULL || stats == NULL) {
        return;
    }
    stats->minimum = UINT16_MAX;
    stats->maximum = 0u;
    for (index = 0u; index < GX5125_IMAGE_PIXELS; ++index) {
        const uint16_t value = pixels[index];
        if (value < stats->minimum) stats->minimum = value;
        if (value > stats->maximum) stats->maximum = value;
        sum += value;
    }
    stats->mean = (double)sum / (double)GX5125_IMAGE_PIXELS;
    for (index = 0u; index < GX5125_IMAGE_PIXELS; ++index) {
        const double difference = (double)pixels[index] - stats->mean;
        variance += difference * difference;
    }
    stats->standard_deviation = sqrt(variance / (double)GX5125_IMAGE_PIXELS);
}
