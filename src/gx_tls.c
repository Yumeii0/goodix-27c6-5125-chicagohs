#include "gx5125/tls.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libusb-1.0/libusb.h>
#include <openssl/err.h>

#include "gx5125/transport.h"

#define GX_TLS_CIPHER "PSK-AES128-GCM-SHA256"
#define GX_TLS_IDENTITY "Client_identity"
#define GX_TLS_IO_BUFFER 16384u

static void gx_tls_set_error(gx_tls_server *server, const char *message)
{
    if (server == NULL) {
        return;
    }
    if (message == NULL) {
        server->last_error[0] = '\0';
        return;
    }
    (void)snprintf(server->last_error,
                   sizeof(server->last_error),
                   "%s",
                   message);
}

static void gx_tls_set_openssl_error(gx_tls_server *server,
                                     const char *prefix)
{
    unsigned long code = ERR_peek_last_error();
    char detail[160];

    if (code == 0u) {
        gx_tls_set_error(server, prefix);
        return;
    }
    ERR_error_string_n(code, detail, sizeof(detail));
    (void)snprintf(server->last_error,
                   sizeof(server->last_error),
                   "%s: %s",
                   prefix,
                   detail);
}

static unsigned int gx_tls_psk_server_callback(SSL *ssl,
                                                const char *identity,
                                                unsigned char *psk,
                                                unsigned int max_psk_length)
{
    gx_tls_server *server = SSL_get_app_data(ssl);

    if (server == NULL || identity == NULL || psk == NULL ||
        strcmp(identity, GX_TLS_IDENTITY) != 0 ||
        max_psk_length < GX5125_PSK_SIZE) {
        if (server != NULL) {
            server->identity_valid = false;
        }
        return 0u;
    }

    memcpy(psk, server->psk, GX5125_PSK_SIZE);
    server->identity_valid = true;
    return GX5125_PSK_SIZE;
}

static int gx_tls_configure_context(SSL_CTX *context)
{
    if (SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_cipher_list(context, GX_TLS_CIPHER) != 1 ||
        SSL_CTX_use_psk_identity_hint(context, GX_TLS_IDENTITY) != 1) {
        return -1;
    }

    (void)SSL_CTX_set_options(context,
                              SSL_OP_NO_COMPRESSION |
                              SSL_OP_NO_TICKET |
                              SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_psk_server_callback(context,
                                    gx_tls_psk_server_callback);
    return 0;
}

int gx_tls_server_init(gx_tls_server *server,
                       const uint8_t psk[GX5125_PSK_SIZE])
{
    BIO *read_bio = NULL;
    BIO *write_bio = NULL;

    if (server == NULL || psk == NULL) {
        return -1;
    }
    memset(server, 0, sizeof(*server));
    memcpy(server->psk, psk, GX5125_PSK_SIZE);

    if (OPENSSL_init_ssl(0u, NULL) != 1) {
        gx_tls_set_openssl_error(server, "OPENSSL_init_ssl failed");
        goto fail;
    }

    server->context = SSL_CTX_new(TLS_server_method());
    if (server->context == NULL) {
        gx_tls_set_openssl_error(server, "SSL_CTX_new failed");
        goto fail;
    }
    if (gx_tls_configure_context(server->context) != 0) {
        gx_tls_set_openssl_error(server, "TLS 1.2 PSK configuration failed");
        goto fail;
    }

    server->ssl = SSL_new(server->context);
    if (server->ssl == NULL) {
        gx_tls_set_openssl_error(server, "SSL_new failed");
        goto fail;
    }

    read_bio = BIO_new(BIO_s_mem());
    write_bio = BIO_new(BIO_s_mem());
    if (read_bio == NULL || write_bio == NULL) {
        gx_tls_set_openssl_error(server, "BIO_new failed");
        goto fail;
    }
    BIO_set_mem_eof_return(read_bio, -1);
    BIO_set_mem_eof_return(write_bio, -1);

    SSL_set_bio(server->ssl, read_bio, write_bio);
    read_bio = NULL;
    write_bio = NULL;
    SSL_set_app_data(server->ssl, server);
    SSL_set_accept_state(server->ssl);
    gx_tls_set_error(server, "none");
    return 0;

fail:
    BIO_free(read_bio);
    BIO_free(write_bio);
    gx_tls_server_cleanup(server);
    return -1;
}

void gx_tls_server_cleanup(gx_tls_server *server)
{
    if (server == NULL) {
        return;
    }
    if (server->ssl != NULL) {
        SSL_free(server->ssl);
        server->ssl = NULL;
    }
    if (server->context != NULL) {
        SSL_CTX_free(server->context);
        server->context = NULL;
    }
    gx_secret_cleanse(server->psk, sizeof(server->psk));
}

static int gx_tls_feed(gx_tls_server *server,
                       const uint8_t *input,
                       size_t input_length)
{
    size_t offset = 0u;
    BIO *bio;

    if (server == NULL || server->ssl == NULL ||
        (input == NULL && input_length != 0u)) {
        return -1;
    }
    bio = SSL_get_rbio(server->ssl);

    while (offset < input_length) {
        size_t written = 0u;
        if (BIO_write_ex(bio,
                         input + offset,
                         input_length - offset,
                         &written) != 1 || written == 0u) {
            gx_tls_set_openssl_error(server, "BIO_write_ex failed");
            return -1;
        }
        offset += written;
    }
    server->usb_input_bytes += input_length;
    return 0;
}

static int gx_tls_flush_usb(gx_tls_server *server, gx_session *session)
{
    uint8_t output[GX_TLS_IO_BUFFER];
    BIO *bio;

    if (server == NULL || server->ssl == NULL || session == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    bio = SSL_get_wbio(server->ssl);

    while (BIO_ctrl_pending(bio) != 0u) {
        size_t read_length = 0u;
        int rc;

        if (BIO_read_ex(bio,
                        output,
                        sizeof(output),
                        &read_length) != 1 || read_length == 0u) {
            gx_tls_set_openssl_error(server, "BIO_read_ex failed");
            gx_secret_cleanse(output, sizeof(output));
            return LIBUSB_ERROR_OTHER;
        }

        rc = gx_transport_send_outer(session->usb,
                                     GX5125_OUTER_TLS,
                                     output,
                                     read_length,
                                     1000u);
        gx_secret_cleanse(output, read_length);
        if (rc < 0) {
            gx_tls_set_error(server, libusb_error_name(rc));
            return rc;
        }
        server->usb_output_bytes += read_length;
        server->usb_output_frames += 1u;
    }

    return LIBUSB_SUCCESS;
}

static uint64_t gx_monotonic_ms(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

int gx_tls_server_handshake_usb(gx_tls_server *server,
                                gx_session *session,
                                unsigned int timeout_ms)
{
    static const uint8_t zero2[2] = {0x00, 0x00};
    uint8_t input[GX_TLS_IO_BUFFER];
    gx_command_result command_result;
    const uint64_t deadline = gx_monotonic_ms() + timeout_ms;
    int rc;

    if (server == NULL || server->ssl == NULL || session == NULL ||
        timeout_ms == 0u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    gx_session_tls_clear(session);
    ERR_clear_error();

    /* D/0 is sent with ACK timeout zero in gfusb.dll, therefore no-ACK. */
    rc = gx_session_command(session,
                            0x0d, 0x00,
                            zero2, sizeof(zero2),
                            0u, false, 0u, 1u,
                            &command_result);
    if (rc < 0) {
        gx_tls_set_error(server, "D/0 TLS_INIT send failed");
        return rc;
    }

    while (gx_monotonic_ms() < deadline) {
        int handshake_result = SSL_do_handshake(server->ssl);
        int ssl_error;

        rc = gx_tls_flush_usb(server, session);
        if (rc < 0) {
            return rc;
        }

        if (handshake_result == 1) {
            if (!server->identity_valid) {
                gx_tls_set_error(server, "PSK identity was not validated");
                return LIBUSB_ERROR_ACCESS;
            }
            gx_secret_cleanse(input, sizeof(input));
            return LIBUSB_SUCCESS;
        }

        ssl_error = SSL_get_error(server->ssl, handshake_result);
        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (ssl_error != SSL_ERROR_WANT_READ) {
            gx_tls_set_openssl_error(server, "SSL_do_handshake failed");
            gx_secret_cleanse(input, sizeof(input));
            return LIBUSB_ERROR_IO;
        }

        {
            const uint64_t now = gx_monotonic_ms();
            unsigned int wait_ms;
            size_t input_length = 0u;

            if (now >= deadline) {
                break;
            }
            wait_ms = (unsigned int)(deadline - now);
            if (wait_ms > 500u) {
                wait_ms = 500u;
            }

            rc = gx_session_tls_read(session,
                                     input,
                                     sizeof(input),
                                     &input_length,
                                     wait_ms);
            if (rc == LIBUSB_ERROR_TIMEOUT) {
                continue;
            }
            if (rc < 0) {
                gx_tls_set_error(server, libusb_error_name(rc));
                gx_secret_cleanse(input, sizeof(input));
                return rc;
            }
            if (gx_tls_feed(server, input, input_length) != 0) {
                gx_secret_cleanse(input, sizeof(input));
                return LIBUSB_ERROR_IO;
            }
            gx_secret_cleanse(input, input_length);
        }
    }

    gx_secret_cleanse(input, sizeof(input));
    gx_tls_set_error(server, "TLS handshake timeout");
    return LIBUSB_ERROR_TIMEOUT;
}

const char *gx_tls_server_version(const gx_tls_server *server)
{
    if (server == NULL || server->ssl == NULL ||
        SSL_is_init_finished(server->ssl) != 1) {
        return "unavailable";
    }
    return SSL_get_version(server->ssl);
}

const char *gx_tls_server_cipher(const gx_tls_server *server)
{
    if (server == NULL || server->ssl == NULL ||
        SSL_is_init_finished(server->ssl) != 1) {
        return "unavailable";
    }
    return SSL_get_cipher_name(server->ssl);
}

const char *gx_tls_server_error(const gx_tls_server *server)
{
    if (server == NULL || server->last_error[0] == '\0') {
        return "unknown";
    }
    return server->last_error;
}

/* ----- offline OpenSSL client/server PSK test ----- */

typedef struct gx_tls_test_client {
    uint8_t psk[GX5125_PSK_SIZE];
} gx_tls_test_client;

static unsigned int gx_tls_psk_client_callback(SSL *ssl,
                                                const char *hint,
                                                char *identity,
                                                unsigned int max_identity_length,
                                                unsigned char *psk,
                                                unsigned int max_psk_length)
{
    gx_tls_test_client *client = SSL_get_app_data(ssl);
    const size_t identity_length = strlen(GX_TLS_IDENTITY);

    (void)hint;
    if (client == NULL || identity == NULL || psk == NULL ||
        max_identity_length <= identity_length ||
        max_psk_length < GX5125_PSK_SIZE) {
        return 0u;
    }

    memcpy(identity, GX_TLS_IDENTITY, identity_length + 1u);
    memcpy(psk, client->psk, GX5125_PSK_SIZE);
    return GX5125_PSK_SIZE;
}

static int gx_tls_transfer_bio(BIO *source, BIO *destination)
{
    uint8_t buffer[4096];

    while (BIO_ctrl_pending(source) != 0u) {
        size_t read_length = 0u;
        size_t written = 0u;

        if (BIO_read_ex(source,
                        buffer,
                        sizeof(buffer),
                        &read_length) != 1 || read_length == 0u) {
            gx_secret_cleanse(buffer, sizeof(buffer));
            return -1;
        }
        if (BIO_write_ex(destination,
                         buffer,
                         read_length,
                         &written) != 1 || written != read_length) {
            gx_secret_cleanse(buffer, sizeof(buffer));
            return -1;
        }
        gx_secret_cleanse(buffer, read_length);
    }
    return 0;
}

int gx_tls_offline_selftest(void)
{
    gx_tls_server server;
    gx_tls_test_client client_data;
    SSL_CTX *client_context = NULL;
    SSL *client = NULL;
    BIO *client_read = NULL;
    BIO *client_write = NULL;
    unsigned int index;
    int result = -1;

    memset(&server, 0, sizeof(server));
    memset(&client_data, 0, sizeof(client_data));
    for (index = 0u; index < GX5125_PSK_SIZE; ++index) {
        client_data.psk[index] = (uint8_t)(index + 1u);
    }

    if (gx_tls_server_init(&server, client_data.psk) != 0) {
        goto cleanup;
    }

    client_context = SSL_CTX_new(TLS_client_method());
    if (client_context == NULL ||
        SSL_CTX_set_min_proto_version(client_context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(client_context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_cipher_list(client_context, GX_TLS_CIPHER) != 1) {
        goto cleanup;
    }
    (void)SSL_CTX_set_options(client_context,
                              SSL_OP_NO_COMPRESSION |
                              SSL_OP_NO_TICKET |
                              SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_psk_client_callback(client_context,
                                    gx_tls_psk_client_callback);

    client = SSL_new(client_context);
    client_read = BIO_new(BIO_s_mem());
    client_write = BIO_new(BIO_s_mem());
    if (client == NULL || client_read == NULL || client_write == NULL) {
        goto cleanup;
    }
    BIO_set_mem_eof_return(client_read, -1);
    BIO_set_mem_eof_return(client_write, -1);
    SSL_set_bio(client, client_read, client_write);
    client_read = NULL;
    client_write = NULL;
    SSL_set_app_data(client, &client_data);
    SSL_set_connect_state(client);

    for (index = 0u; index < 128u; ++index) {
        int client_rc;
        int server_rc;

        if (SSL_is_init_finished(client) != 1) {
            client_rc = SSL_do_handshake(client);
            if (client_rc != 1) {
                const int error = SSL_get_error(client, client_rc);
                if (error != SSL_ERROR_WANT_READ &&
                    error != SSL_ERROR_WANT_WRITE) {
                    goto cleanup;
                }
            }
        }
        if (gx_tls_transfer_bio(SSL_get_wbio(client),
                                SSL_get_rbio(server.ssl)) != 0) {
            goto cleanup;
        }

        if (SSL_is_init_finished(server.ssl) != 1) {
            server_rc = SSL_do_handshake(server.ssl);
            if (server_rc != 1) {
                const int error = SSL_get_error(server.ssl, server_rc);
                if (error != SSL_ERROR_WANT_READ &&
                    error != SSL_ERROR_WANT_WRITE) {
                    goto cleanup;
                }
            }
        }
        if (gx_tls_transfer_bio(SSL_get_wbio(server.ssl),
                                SSL_get_rbio(client)) != 0) {
            goto cleanup;
        }

        if (SSL_is_init_finished(client) == 1 &&
            SSL_is_init_finished(server.ssl) == 1) {
            break;
        }
    }

    if (SSL_is_init_finished(client) != 1 ||
        SSL_is_init_finished(server.ssl) != 1 ||
        !server.identity_valid ||
        strcmp(SSL_get_version(server.ssl), "TLSv1.2") != 0 ||
        strcmp(SSL_get_cipher_name(server.ssl), GX_TLS_CIPHER) != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    BIO_free(client_read);
    BIO_free(client_write);
    SSL_free(client);
    SSL_CTX_free(client_context);
    gx_tls_server_cleanup(&server);
    gx_secret_cleanse(&client_data, sizeof(client_data));
    return result;
}

int gx_tls_server_read_application(gx_tls_server *server,
                                   gx_session *session,
                                   uint8_t *output,
                                   size_t output_capacity,
                                   size_t *output_length,
                                   unsigned int timeout_ms)
{
    uint8_t input[GX_TLS_IO_BUFFER];
    const uint64_t deadline = gx_monotonic_ms() + timeout_ms;

    if (output_length != NULL) {
        *output_length = 0u;
    }
    if (server == NULL || server->ssl == NULL || session == NULL ||
        output == NULL || output_capacity == 0u || output_length == NULL ||
        timeout_ms == 0u || SSL_is_init_finished(server->ssl) != 1) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    while (gx_monotonic_ms() < deadline) {
        size_t read_length = 0u;
        int ssl_result;
        int ssl_error;
        int rc;

        ERR_clear_error();
        ssl_result = SSL_read_ex(server->ssl,
                                 output,
                                 output_capacity,
                                 &read_length);
        rc = gx_tls_flush_usb(server, session);
        if (rc < 0) {
            gx_secret_cleanse(input, sizeof(input));
            return rc;
        }
        if (ssl_result == 1 && read_length != 0u) {
            *output_length = read_length;
            gx_secret_cleanse(input, sizeof(input));
            return LIBUSB_SUCCESS;
        }

        ssl_error = SSL_get_error(server->ssl, ssl_result);
        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            gx_tls_set_error(server, "TLS peer closed the connection");
            gx_secret_cleanse(input, sizeof(input));
            return LIBUSB_ERROR_NO_DEVICE;
        }
        if (ssl_error != SSL_ERROR_WANT_READ) {
            gx_tls_set_openssl_error(server, "SSL_read_ex failed");
            gx_secret_cleanse(input, sizeof(input));
            return LIBUSB_ERROR_IO;
        }

        {
            const uint64_t now = gx_monotonic_ms();
            unsigned int wait_ms;
            size_t input_length = 0u;

            if (now >= deadline) {
                break;
            }
            wait_ms = (unsigned int)(deadline - now);
            if (wait_ms > 500u) {
                wait_ms = 500u;
            }
            rc = gx_session_tls_read(session,
                                     input,
                                     sizeof(input),
                                     &input_length,
                                     wait_ms);
            if (rc == LIBUSB_ERROR_TIMEOUT) {
                continue;
            }
            if (rc < 0) {
                gx_tls_set_error(server, libusb_error_name(rc));
                gx_secret_cleanse(input, sizeof(input));
                return rc;
            }
            if (gx_tls_feed(server, input, input_length) != 0) {
                gx_secret_cleanse(input, sizeof(input));
                return LIBUSB_ERROR_IO;
            }
            gx_secret_cleanse(input, input_length);
        }
    }

    gx_secret_cleanse(input, sizeof(input));
    gx_tls_set_error(server, "TLS application read timeout");
    return LIBUSB_ERROR_TIMEOUT;
}
