#ifndef GX5125_SECRET_H
#define GX5125_SECRET_H

#include <stddef.h>
#include <stdint.h>

#define GX5125_PSK_SIZE 32u
#define GX5125_PSK_HEX_LENGTH 64u

int gx_secret_parse_psk_hex(const char *text,
                            uint8_t output[GX5125_PSK_SIZE]);
int gx_secret_prompt_psk(uint8_t output[GX5125_PSK_SIZE]);
int gx_secret_read_psk_file(const char *path,
                            uint8_t output[GX5125_PSK_SIZE]);
void gx_secret_cleanse(void *buffer, size_t length);

#endif
