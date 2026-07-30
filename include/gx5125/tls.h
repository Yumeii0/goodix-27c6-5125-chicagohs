#ifndef GX5125_TLS_H
#define GX5125_TLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

#include "gx5125/secret.h"
#include "gx5125/session.h"

typedef struct gx_tls_server {
    SSL_CTX *context;
    SSL *ssl;
    uint8_t psk[GX5125_PSK_SIZE];
    bool identity_valid;
    uint64_t usb_input_bytes;
    uint64_t usb_output_bytes;
    uint64_t usb_output_frames;
    char last_error[256];
} gx_tls_server;

int gx_tls_server_init(gx_tls_server *server,
                       const uint8_t psk[GX5125_PSK_SIZE]);
void gx_tls_server_cleanup(gx_tls_server *server);

int gx_tls_server_handshake_usb(gx_tls_server *server,
                                gx_session *session,
                                unsigned int timeout_ms);

int gx_tls_server_read_application(gx_tls_server *server,
                                   gx_session *session,
                                   uint8_t *output,
                                   size_t output_capacity,
                                   size_t *output_length,
                                   unsigned int timeout_ms);

const char *gx_tls_server_version(const gx_tls_server *server);
const char *gx_tls_server_cipher(const gx_tls_server *server);
const char *gx_tls_server_error(const gx_tls_server *server);

/* Fully offline OpenSSL PSK handshake test. */
int gx_tls_offline_selftest(void);

#endif
