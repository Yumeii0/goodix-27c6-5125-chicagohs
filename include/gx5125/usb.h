#ifndef GX5125_USB_H
#define GX5125_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libusb-1.0/libusb.h>

#define GX5125_USB_VID UINT16_C(0x27c6)
#define GX5125_USB_PID UINT16_C(0x5125)

#define GX5125_INTERFACE_CONTROL 0
#define GX5125_INTERFACE_DATA 1

#define GX5125_ENDPOINT_BULK_OUT UINT8_C(0x01)
#define GX5125_ENDPOINT_BULK_IN  UINT8_C(0x81)
#define GX5125_ENDPOINT_INTR_IN  UINT8_C(0x82)

#define GX5125_USB_BLOCK_SIZE 64u

typedef struct gx_usb_device {
    libusb_context *context;
    libusb_device_handle *handle;
    bool claimed_control;
    bool claimed_data;
    bool manually_detached_control;
    bool manually_detached_data;
} gx_usb_device;

int gx_usb_open(gx_usb_device *device, int log_level);
int gx_usb_claim_interfaces(gx_usb_device *device);
int gx_usb_validate_layout(const gx_usb_device *device);
void gx_usb_release_interfaces(gx_usb_device *device);
void gx_usb_close(gx_usb_device *device);

int gx_usb_bulk_write_64(gx_usb_device *device,
                         const uint8_t *buffer,
                         size_t wire_length,
                         unsigned int timeout_ms);
int gx_usb_bulk_read(gx_usb_device *device,
                     uint8_t *buffer,
                     size_t capacity,
                     size_t *received,
                     unsigned int timeout_ms);

#endif
