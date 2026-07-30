#include "gx5125/secret.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <openssl/crypto.h>

static int gx_hex_value(unsigned char value)
{
    if (value >= (unsigned char)'0' && value <= (unsigned char)'9') {
        return (int)(value - (unsigned char)'0');
    }
    if (value >= (unsigned char)'a' && value <= (unsigned char)'f') {
        return 10 + (int)(value - (unsigned char)'a');
    }
    if (value >= (unsigned char)'A' && value <= (unsigned char)'F') {
        return 10 + (int)(value - (unsigned char)'A');
    }
    return -1;
}

void gx_secret_cleanse(void *buffer, size_t length)
{
    if (buffer != NULL && length != 0u) {
        OPENSSL_cleanse(buffer, length);
    }
}

int gx_secret_parse_psk_hex(const char *text,
                            uint8_t output[GX5125_PSK_SIZE])
{
    unsigned char digits[GX5125_PSK_HEX_LENGTH];
    size_t count = 0u;
    size_t index;

    if (text == NULL || output == NULL) {
        return -1;
    }

    memset(digits, 0, sizeof(digits));
    for (index = 0u; text[index] != '\0'; ++index) {
        const unsigned char value = (unsigned char)text[index];

        if (isspace(value) != 0 || value == (unsigned char)':' ||
            value == (unsigned char)'-') {
            continue;
        }
        if (gx_hex_value(value) < 0 || count >= sizeof(digits)) {
            gx_secret_cleanse(digits, sizeof(digits));
            return -1;
        }
        digits[count++] = value;
    }

    if (count != GX5125_PSK_HEX_LENGTH) {
        gx_secret_cleanse(digits, sizeof(digits));
        return -1;
    }

    for (index = 0u; index < GX5125_PSK_SIZE; ++index) {
        const int high = gx_hex_value(digits[index * 2u]);
        const int low = gx_hex_value(digits[index * 2u + 1u]);

        output[index] = (uint8_t)((unsigned int)high << 4u |
                                  (unsigned int)low);
    }

    gx_secret_cleanse(digits, sizeof(digits));
    return 0;
}

int gx_secret_prompt_psk(uint8_t output[GX5125_PSK_SIZE])
{
    struct termios original;
    struct termios hidden;
    char input[256];
    sigset_t blocked_signals;
    sigset_t previous_signals;
    int signals_blocked = 0;
    int have_terminal = 0;
    int result = -1;

    if (output == NULL) {
        return -1;
    }
    memset(output, 0, GX5125_PSK_SIZE);
    memset(input, 0, sizeof(input));

    if (isatty(STDIN_FILENO) == 0) {
        fprintf(stderr, "GOODIX_PSK_INPUT=FAIL reason:stdin-not-a-terminal\n");
        return -1;
    }

    (void)sigemptyset(&blocked_signals);
    (void)sigaddset(&blocked_signals, SIGINT);
    (void)sigaddset(&blocked_signals, SIGTERM);
    (void)sigaddset(&blocked_signals, SIGHUP);
    (void)sigaddset(&blocked_signals, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &blocked_signals, &previous_signals) == 0) {
        signals_blocked = 1;
    }

    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        fprintf(stderr,
                "GOODIX_PSK_INPUT=FAIL reason:tcgetattr errno:%d\n",
                errno);
        if (signals_blocked != 0) {
            (void)sigprocmask(SIG_SETMASK, &previous_signals, NULL);
        }
        return -1;
    }

    hidden = original;
    hidden.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
        fprintf(stderr,
                "GOODIX_PSK_INPUT=FAIL reason:tcsetattr errno:%d\n",
                errno);
        if (signals_blocked != 0) {
            (void)sigprocmask(SIG_SETMASK, &previous_signals, NULL);
        }
        return -1;
    }
    have_terminal = 1;

    fprintf(stderr, "PSK (64 hexadecimal characters, hidden input): ");
    fflush(stderr);
    if (fgets(input, sizeof(input), stdin) != NULL &&
        gx_secret_parse_psk_hex(input, output) == 0) {
        result = 0;
    }

    if (have_terminal != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    }
    if (signals_blocked != 0) {
        (void)sigprocmask(SIG_SETMASK, &previous_signals, NULL);
    }
    fputc('\n', stderr);

    if (result == 0) {
        fprintf(stderr, "GOODIX_PSK_INPUT=ACCEPTED bytes:32\n");
    } else {
        gx_secret_cleanse(output, GX5125_PSK_SIZE);
        fprintf(stderr,
                "GOODIX_PSK_INPUT=FAIL reason:expected-64-hex-characters\n");
    }

    gx_secret_cleanse(input, sizeof(input));
    return result;
}

int gx_secret_read_psk_file(const char *path,
                            uint8_t output[GX5125_PSK_SIZE])
{
    struct stat status;
    char text[256];
    size_t used = 0u;
    int fd;
    int result = -1;

    if (path == NULL || output == NULL) {
        return -1;
    }
    memset(output, 0, GX5125_PSK_SIZE);
    memset(text, 0, sizeof(text));

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        (void)close(fd);
        return -1;
    }

    while (used + 1u < sizeof(text)) {
        ssize_t count = read(fd, text + used, sizeof(text) - used - 1u);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto cleanup;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    if (used + 1u == sizeof(text)) {
        char extra;
        ssize_t count;
        do {
            count = read(fd, &extra, 1u);
        } while (count < 0 && errno == EINTR);
        if (count != 0) {
            goto cleanup;
        }
    }
    text[used] = '\0';
    result = gx_secret_parse_psk_hex(text, output);

cleanup:
    (void)close(fd);
    gx_secret_cleanse(text, sizeof(text));
    if (result != 0) {
        gx_secret_cleanse(output, GX5125_PSK_SIZE);
    }
    return result;
}
