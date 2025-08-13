#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>

#define USB_MAX_ENDPOINTS 16
#define USB_MAX_PACKET_SIZE 64
#define USB_CONTROL_ENDPOINT 0

typedef enum {
    USB_SUCCESS = 0,
    USB_ERROR_INVALID_PARAM,
    USB_ERROR_NOT_INITIALIZED,
    USB_ERROR_BUSY,
    USB_ERROR_TIMEOUT,
    USB_ERROR_STALL,
    USB_ERROR_BUFFER_OVERFLOW
} usb_status_t;

typedef enum {
    USB_ENDPOINT_TYPE_CONTROL = 0,
    USB_ENDPOINT_TYPE_ISOCHRONOUS,
    USB_ENDPOINT_TYPE_BULK,
    USB_ENDPOINT_TYPE_INTERRUPT
} usb_endpoint_type_t;

typedef enum {
    USB_DIRECTION_OUT = 0,
    USB_DIRECTION_IN = 1
} usb_direction_t;

typedef enum {
    USB_DEVICE_STATE_DETACHED = 0,
    USB_DEVICE_STATE_ATTACHED,
    USB_DEVICE_STATE_POWERED,
    USB_DEVICE_STATE_DEFAULT,
    USB_DEVICE_STATE_ADDRESS,
    USB_DEVICE_STATE_CONFIGURED,
    USB_DEVICE_STATE_SUSPENDED
} usb_device_state_t;

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct {
    uint8_t endpoint_num;
    usb_endpoint_type_t type;
    usb_direction_t direction;
    uint16_t max_packet_size;
    bool enabled;
    uint8_t *buffer;
    uint16_t buffer_size;
    uint16_t data_length;
    bool transfer_complete;
} usb_endpoint_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_config_descriptor_t;

typedef void (*usb_setup_callback_t)(usb_setup_packet_t *setup);
typedef void (*usb_transfer_callback_t)(uint8_t endpoint, usb_status_t status);
typedef void (*usb_state_callback_t)(usb_device_state_t state);

typedef struct {
    usb_device_descriptor_t *device_descriptor;
    usb_config_descriptor_t *config_descriptor;
    char **string_descriptors;
    uint8_t num_string_descriptors;
    usb_setup_callback_t setup_callback;
    usb_transfer_callback_t transfer_callback;
    usb_state_callback_t state_callback;
} usb_config_t;

usb_status_t usb_init(usb_config_t *config);
usb_status_t usb_deinit(void);
usb_status_t usb_start(void);
usb_status_t usb_stop(void);
usb_device_state_t usb_get_state(void);

usb_status_t usb_endpoint_configure(uint8_t endpoint_num, 
                                   usb_endpoint_type_t type,
                                   usb_direction_t direction,
                                   uint16_t max_packet_size);
usb_status_t usb_endpoint_enable(uint8_t endpoint_num);
usb_status_t usb_endpoint_disable(uint8_t endpoint_num);
usb_status_t usb_endpoint_stall(uint8_t endpoint_num);
usb_status_t usb_endpoint_clear_stall(uint8_t endpoint_num);

usb_status_t usb_transmit(uint8_t endpoint_num, uint8_t *data, uint16_t length);
usb_status_t usb_receive(uint8_t endpoint_num, uint8_t *buffer, uint16_t max_length);
usb_status_t usb_control_send_status(void);
usb_status_t usb_control_send_data(uint8_t *data, uint16_t length);
usb_status_t usb_control_receive_data(uint8_t *buffer, uint16_t max_length);

void usb_interrupt_handler(void);

#endif