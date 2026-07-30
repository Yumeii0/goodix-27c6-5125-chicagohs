#ifndef GX5125_TEST_FAKE_LIBUSB_H
#define GX5125_TEST_FAKE_LIBUSB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

struct libusb_endpoint_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
    uint8_t bRefresh;
    uint8_t bSynchAddress;
    const unsigned char *extra;
    int extra_length;
};

struct libusb_interface_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
    const struct libusb_endpoint_descriptor *endpoint;
    const unsigned char *extra;
    int extra_length;
};

struct libusb_interface {
    const struct libusb_interface_descriptor *altsetting;
    int num_altsetting;
};

struct libusb_config_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t MaxPower;
    const struct libusb_interface *interface;
    const unsigned char *extra;
    int extra_length;
};

enum {
    LIBUSB_SUCCESS = 0,
    LIBUSB_ERROR_IO = -1,
    LIBUSB_ERROR_INVALID_PARAM = -2,
    LIBUSB_ERROR_ACCESS = -3,
    LIBUSB_ERROR_NO_DEVICE = -4,
    LIBUSB_ERROR_NOT_FOUND = -5,
    LIBUSB_ERROR_BUSY = -6,
    LIBUSB_ERROR_TIMEOUT = -7,
    LIBUSB_ERROR_OVERFLOW = -8,
    LIBUSB_ERROR_PIPE = -9,
    LIBUSB_ERROR_INTERRUPTED = -10,
    LIBUSB_ERROR_NO_MEM = -11,
    LIBUSB_ERROR_NOT_SUPPORTED = -12,
    LIBUSB_ERROR_OTHER = -99
};

enum {
    LIBUSB_TRANSFER_TYPE_CONTROL = 0,
    LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1,
    LIBUSB_TRANSFER_TYPE_BULK = 2,
    LIBUSB_TRANSFER_TYPE_INTERRUPT = 3,
    LIBUSB_TRANSFER_TYPE_BULK_STREAM = 4,
    LIBUSB_TRANSFER_TYPE_MASK = 0x3
};

enum {
    LIBUSB_OPTION_LOG_LEVEL = 0
};

enum {
    LIBUSB_LOG_LEVEL_NONE = 0,
    LIBUSB_LOG_LEVEL_ERROR = 1,
    LIBUSB_LOG_LEVEL_WARNING = 2,
    LIBUSB_LOG_LEVEL_INFO = 3,
    LIBUSB_LOG_LEVEL_DEBUG = 4
};

int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
int libusb_set_option(libusb_context *ctx, int option, ...);
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx,
                                                       uint16_t vendor_id,
                                                       uint16_t product_id);
void libusb_close(libusb_device_handle *dev_handle);
libusb_device *libusb_get_device(libusb_device_handle *dev_handle);
int libusb_get_active_config_descriptor(libusb_device *dev,
                                        struct libusb_config_descriptor **config);
void libusb_free_config_descriptor(struct libusb_config_descriptor *config);
int libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev_handle,
                                         int enable);
int libusb_kernel_driver_active(libusb_device_handle *dev_handle,
                                int interface_number);
int libusb_detach_kernel_driver(libusb_device_handle *dev_handle,
                                int interface_number);
int libusb_attach_kernel_driver(libusb_device_handle *dev_handle,
                                int interface_number);
int libusb_claim_interface(libusb_device_handle *dev_handle,
                           int interface_number);
int libusb_release_interface(libusb_device_handle *dev_handle,
                             int interface_number);
int libusb_bulk_transfer(libusb_device_handle *dev_handle,
                         unsigned char endpoint,
                         unsigned char *data,
                         int length,
                         int *actual_length,
                         unsigned int timeout);
const char *libusb_error_name(int errcode);

#ifdef __cplusplus
}
#endif

#endif
