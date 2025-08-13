#include "usb.h"
#include <stdio.h>
#include <string.h>

void usb_handle_standard_setup(usb_setup_packet_t *setup);

static usb_device_descriptor_t device_descriptor = {
    .bLength = sizeof(usb_device_descriptor_t),
    .bDescriptorType = 0x01,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1234,
    .idProduct = 0x5678,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1
};

static uint8_t config_descriptor_data[] = {
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};

static usb_config_descriptor_t *config_descriptor = (usb_config_descriptor_t*)config_descriptor_data;

static char* string_descriptors[] = {
    "Example Manufacturer",
    "USB Example Device",
    "123456789"
};

static uint8_t rx_buffer[64];
static uint8_t tx_buffer[64];

static void setup_callback(usb_setup_packet_t *setup)
{
    if ((setup->bmRequestType & 0x60) == 0x00) {
        usb_handle_standard_setup(setup);
    } else {
        usb_endpoint_stall(USB_CONTROL_ENDPOINT);
    }
}

static void transfer_callback(uint8_t endpoint, usb_status_t status)
{
    if (status != USB_SUCCESS) {
        printf("Transfer error on endpoint %d: %d\n", endpoint, status);
        return;
    }

    switch (endpoint) {
        case 1:
            printf("Data transmitted on endpoint 1\n");
            break;
            
        case 2:
            printf("Data received on endpoint 2\n");
            strcpy((char*)tx_buffer, "Echo: ");
            strncat((char*)tx_buffer, (char*)rx_buffer, 58);
            usb_transmit(1, tx_buffer, strlen((char*)tx_buffer));
            usb_receive(2, rx_buffer, sizeof(rx_buffer));
            break;
            
        default:
            break;
    }
}

static void state_callback(usb_device_state_t state)
{
    switch (state) {
        case USB_DEVICE_STATE_DETACHED:
            printf("USB: Detached\n");
            break;
        case USB_DEVICE_STATE_ATTACHED:
            printf("USB: Attached\n");
            break;
        case USB_DEVICE_STATE_POWERED:
            printf("USB: Powered\n");
            break;
        case USB_DEVICE_STATE_DEFAULT:
            printf("USB: Default\n");
            break;
        case USB_DEVICE_STATE_ADDRESS:
            printf("USB: Address assigned\n");
            break;
        case USB_DEVICE_STATE_CONFIGURED:
            printf("USB: Configured\n");
            usb_receive(2, rx_buffer, sizeof(rx_buffer));
            break;
        case USB_DEVICE_STATE_SUSPENDED:
            printf("USB: Suspended\n");
            break;
    }
}

int usb_example_init(void)
{
    usb_config_t usb_config = {
        .device_descriptor = &device_descriptor,
        .config_descriptor = config_descriptor,
        .string_descriptors = string_descriptors,
        .num_string_descriptors = 3,
        .setup_callback = setup_callback,
        .transfer_callback = transfer_callback,
        .state_callback = state_callback
    };

    usb_status_t status = usb_init(&usb_config);
    if (status != USB_SUCCESS) {
        printf("USB initialization failed: %d\n", status);
        return -1;
    }

    usb_endpoint_configure(0, USB_ENDPOINT_TYPE_CONTROL, USB_DIRECTION_IN, 64);
    usb_endpoint_enable(0);

    status = usb_start();
    if (status != USB_SUCCESS) {
        printf("USB start failed: %d\n", status);
        usb_deinit();
        return -1;
    }

    printf("USB device initialized and started\n");
    return 0;
}

void usb_example_deinit(void)
{
    usb_deinit();
    printf("USB device deinitialized\n");
}