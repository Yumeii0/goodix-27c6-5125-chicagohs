#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gx5125/device.h"
#include "gx5125/secret.h"

static int test_secure_psk_file(void)
{
    static const char psk_text[] =
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f\n";
    char path[] = "/tmp/gx5125-psk-selftest-XXXXXX";
    uint8_t psk[GX5125_PSK_SIZE];
    int fd;
    ssize_t written;
    int result = -1;

    memset(psk, 0, sizeof(psk));
    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        goto cleanup;
    }
    written = write(fd, psk_text, sizeof(psk_text) - 1u);
    if (written != (ssize_t)(sizeof(psk_text) - 1u) || close(fd) != 0) {
        fd = -1;
        goto cleanup;
    }
    fd = -1;
    if (gx_secret_read_psk_file(path, psk) != 0 ||
        psk[0] != 0x00u || psk[31] != 0x1fu) {
        goto cleanup;
    }
    if (chmod(path, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
        goto cleanup;
    }
    memset(psk, 0, sizeof(psk));
    if (gx_secret_read_psk_file(path, psk) == 0) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (fd >= 0) {
        (void)close(fd);
    }
    (void)unlink(path);
    gx_secret_cleanse(psk, sizeof(psk));
    return result;
}

int main(void)
{
    if (gx5125_device_selftest() != 0 || test_secure_psk_file() != 0) {
        puts("GOODIX_BETA_DEVICE_SELFTEST=FAIL");
        return 1;
    }
    puts("GOODIX_BETA_DEVICE_SELFTEST=PASS usb_accessed:0 "
         "tls_offline:1 crc_decode:1 state_machine:1 secure_psk_file:1 "
         "insecure_psk_rejected:1 biometric_input_used:0");
    return 0;
}
