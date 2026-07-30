#include "gx5125/usb.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int gx_detach_if_needed(gx_usb_device *device,
                               int interface_number,
                               bool *manually_detached)
{
    int rc = libusb_kernel_driver_active(device->handle, interface_number);

    if (rc == 0 || rc == LIBUSB_ERROR_NOT_SUPPORTED) {
        return LIBUSB_SUCCESS;
    }
    if (rc < 0) {
        return rc;
    }

    rc = libusb_detach_kernel_driver(device->handle, interface_number);
    if (rc == LIBUSB_SUCCESS) {
        *manually_detached = true;
    }
    return rc;
}

static void gx_reattach_if_needed(gx_usb_device *device,
                                  int interface_number,
                                  bool *manually_detached)
{
    if (*manually_detached && device->handle != NULL) {
        (void)libusb_attach_kernel_driver(device->handle, interface_number);
        *manually_detached = false;
    }
}

int gx_usb_open(gx_usb_device *device, int log_level)
{
    int rc;

    if (device == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    memset(device, 0, sizeof(*device));

    rc = libusb_init(&device->context);
    if (rc < 0) {
        return rc;
    }
    if (log_level >= 0) {
        (void)libusb_set_option(device->context,
                                LIBUSB_OPTION_LOG_LEVEL,
                                log_level);
    }

    device->handle = libusb_open_device_with_vid_pid(device->context,
                                                      GX5125_USB_VID,
                                                      GX5125_USB_PID);
    if (device->handle == NULL) {
        libusb_exit(device->context);
        device->context = NULL;
        return LIBUSB_ERROR_NO_DEVICE;
    }
    return LIBUSB_SUCCESS;
}

int gx_usb_validate_layout(const gx_usb_device *device)
{
    libusb_device *raw_device;
    struct libusb_config_descriptor *config = NULL;
    bool bulk_out = false;
    bool bulk_in = false;
    bool interrupt_in = false;
    int rc;
    uint8_t interface_index;

    if (device == NULL || device->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    raw_device = libusb_get_device(device->handle);
    rc = libusb_get_active_config_descriptor(raw_device, &config);
    if (rc < 0) {
        return rc;
    }

    for (interface_index = 0u; interface_index < config->bNumInterfaces;
         ++interface_index) {
        const struct libusb_interface *interface = &config->interface[interface_index];
        int alt_index;

        for (alt_index = 0; alt_index < interface->num_altsetting; ++alt_index) {
            const struct libusb_interface_descriptor *alt =
                &interface->altsetting[alt_index];
            uint8_t endpoint_index;

            for (endpoint_index = 0u; endpoint_index < alt->bNumEndpoints;
                 ++endpoint_index) {
                const struct libusb_endpoint_descriptor *endpoint =
                    &alt->endpoint[endpoint_index];
                const uint8_t transfer_type =
                    (uint8_t)(endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK);

                if (endpoint->bEndpointAddress == GX5125_ENDPOINT_BULK_OUT &&
                    transfer_type == LIBUSB_TRANSFER_TYPE_BULK) {
                    bulk_out = true;
                } else if (endpoint->bEndpointAddress == GX5125_ENDPOINT_BULK_IN &&
                           transfer_type == LIBUSB_TRANSFER_TYPE_BULK) {
                    bulk_in = true;
                } else if (endpoint->bEndpointAddress == GX5125_ENDPOINT_INTR_IN &&
                           transfer_type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
                    interrupt_in = true;
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    if (!bulk_out || !bulk_in || !interrupt_in) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    return LIBUSB_SUCCESS;
}

int gx_usb_claim_interfaces(gx_usb_device *device)
{
    int rc;
    bool auto_detach = false;

    if (device == NULL || device->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    rc = libusb_set_auto_detach_kernel_driver(device->handle, 1);
    if (rc == LIBUSB_SUCCESS) {
        auto_detach = true;
    } else if (rc != LIBUSB_ERROR_NOT_SUPPORTED) {
        return rc;
    }

    if (!auto_detach) {
        rc = gx_detach_if_needed(device,
                                 GX5125_INTERFACE_CONTROL,
                                 &device->manually_detached_control);
        if (rc < 0) {
            return rc;
        }
    }
    rc = libusb_claim_interface(device->handle, GX5125_INTERFACE_CONTROL);
    if (rc < 0) {
        return rc;
    }
    device->claimed_control = true;

    if (!auto_detach) {
        rc = gx_detach_if_needed(device,
                                 GX5125_INTERFACE_DATA,
                                 &device->manually_detached_data);
        if (rc < 0) {
            gx_usb_release_interfaces(device);
            return rc;
        }
    }
    rc = libusb_claim_interface(device->handle, GX5125_INTERFACE_DATA);
    if (rc < 0) {
        gx_usb_release_interfaces(device);
        return rc;
    }
    device->claimed_data = true;
    return LIBUSB_SUCCESS;
}

void gx_usb_release_interfaces(gx_usb_device *device)
{
    if (device == NULL || device->handle == NULL) {
        return;
    }

    if (device->claimed_data) {
        (void)libusb_release_interface(device->handle,
                                       GX5125_INTERFACE_DATA);
        device->claimed_data = false;
    }
    if (device->claimed_control) {
        (void)libusb_release_interface(device->handle,
                                       GX5125_INTERFACE_CONTROL);
        device->claimed_control = false;
    }

    gx_reattach_if_needed(device,
                          GX5125_INTERFACE_DATA,
                          &device->manually_detached_data);
    gx_reattach_if_needed(device,
                          GX5125_INTERFACE_CONTROL,
                          &device->manually_detached_control);
}

void gx_usb_close(gx_usb_device *device)
{
    if (device == NULL) {
        return;
    }
    gx_usb_release_interfaces(device);
    if (device->handle != NULL) {
        libusb_close(device->handle);
        device->handle = NULL;
    }
    if (device->context != NULL) {
        libusb_exit(device->context);
        device->context = NULL;
    }
}

int gx_usb_bulk_write_64(gx_usb_device *device,
                         const uint8_t *buffer,
                         size_t wire_length,
                         unsigned int timeout_ms)
{
    size_t offset;

    if (device == NULL || device->handle == NULL || !device->claimed_data ||
        buffer == NULL || wire_length == 0u ||
        wire_length % GX5125_USB_BLOCK_SIZE != 0u) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    for (offset = 0u; offset < wire_length; offset += GX5125_USB_BLOCK_SIZE) {
        int transferred = 0;
        int rc = libusb_bulk_transfer(device->handle,
                                      GX5125_ENDPOINT_BULK_OUT,
                                      (unsigned char *)(uintptr_t)(buffer + offset),
                                      (int)GX5125_USB_BLOCK_SIZE,
                                      &transferred,
                                      timeout_ms);
        if (rc < 0) {
            return rc;
        }
        if (transferred != (int)GX5125_USB_BLOCK_SIZE) {
            return LIBUSB_ERROR_IO;
        }
    }
    return LIBUSB_SUCCESS;
}

int gx_usb_bulk_read(gx_usb_device *device,
                     uint8_t *buffer,
                     size_t capacity,
                     size_t *received,
                     unsigned int timeout_ms)
{
    int transferred = 0;
    int rc;

    if (received != NULL) {
        *received = 0u;
    }
    if (device == NULL || device->handle == NULL || !device->claimed_data ||
        buffer == NULL || received == NULL || capacity == 0u ||
        capacity > (size_t)INT_MAX) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    rc = libusb_bulk_transfer(device->handle,
                              GX5125_ENDPOINT_BULK_IN,
                              buffer,
                              (int)capacity,
                              &transferred,
                              timeout_ms);
    if (rc < 0) {
        return rc;
    }
    *received = (size_t)transferred;
    return LIBUSB_SUCCESS;
}
