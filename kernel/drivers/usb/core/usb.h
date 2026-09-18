/*
 * DevOS — USB core type definitions (usb.h)
 * Shared between xHCI host controller and USB class drivers.
 */
#pragma once
#include "../../../include/types.h"

/* USB descriptor types */
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIG         0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HID            0x21

/* USB class codes */
#define USB_CLASS_HID           0x03
#define USB_CLASS_MSC           0x08   /* Mass Storage */
#define USB_CLASS_HUB           0x09
#define USB_CLASS_VENDOR        0xFF

/* USB setup packet */
typedef struct {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} PACKED usb_setup_t;

/* Standard device descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  desc_type;
    uint16_t bcd_usb;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t  idx_manufacturer;
    uint8_t  idx_product;
    uint8_t  idx_serial;
    uint8_t  num_configurations;
} PACKED usb_device_desc_t;

/* USB endpoint descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  desc_type;
    uint8_t  endpoint_address;  /* bit7: direction (0=out,1=in) */
    uint8_t  attributes;        /* bits 1:0: transfer type */
    uint16_t max_packet_size;
    uint8_t  interval;
} PACKED usb_endpoint_desc_t;

/* USB transfer types */
#define USB_XFER_CONTROL    0
#define USB_XFER_ISO        1
#define USB_XFER_BULK       2
#define USB_XFER_INTERRUPT  3

/* USB device state (opaque to class drivers) */
typedef struct usb_device usb_device_t;

#define USB_MAX_DEVICES     16

/* Class driver registration */
typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    int (*probe)(usb_device_t *dev);
    void (*disconnect)(usb_device_t *dev);
} usb_class_driver_t;

void usb_register_class(usb_class_driver_t *drv);
int  usb_control_transfer(usb_device_t *dev, usb_setup_t *setup,
                           void *data, size_t len);
int  usb_bulk_transfer(usb_device_t *dev, uint8_t ep,
                        void *data, size_t len, bool in);
int  usb_interrupt_transfer(usb_device_t *dev, uint8_t ep,
                             void *data, size_t len);
