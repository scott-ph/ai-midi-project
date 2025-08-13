#include "usb.h"
#include <string.h>

#define USB_DESCRIPTOR_TYPE_DEVICE 0x01
#define USB_DESCRIPTOR_TYPE_CONFIGURATION 0x02
#define USB_DESCRIPTOR_TYPE_STRING 0x03
#define USB_DESCRIPTOR_TYPE_INTERFACE 0x04
#define USB_DESCRIPTOR_TYPE_ENDPOINT 0x05

#define USB_REQUEST_GET_DESCRIPTOR 0x06
#define USB_REQUEST_SET_ADDRESS 0x05
#define USB_REQUEST_SET_CONFIGURATION 0x09
#define USB_REQUEST_GET_STATUS 0x00
#define USB_REQUEST_CLEAR_FEATURE 0x01
#define USB_REQUEST_SET_FEATURE 0x03

extern struct {
    bool initialized;
    usb_device_state_t state;
    usb_config_t *config;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t device_address;
    uint8_t current_configuration;
} usb_device;

static usb_status_t usb_handle_get_descriptor(usb_setup_packet_t *setup);
static usb_status_t usb_handle_set_address(usb_setup_packet_t *setup);
static usb_status_t usb_handle_set_configuration(usb_setup_packet_t *setup);
static usb_status_t usb_handle_get_status(usb_setup_packet_t *setup);
static usb_status_t usb_handle_clear_feature(usb_setup_packet_t *setup);
static usb_status_t usb_handle_set_feature(usb_setup_packet_t *setup);

void usb_handle_standard_setup(usb_setup_packet_t *setup)
{
    usb_status_t status = USB_ERROR_STALL;

    switch (setup->bRequest) {
        case USB_REQUEST_GET_DESCRIPTOR:
            status = usb_handle_get_descriptor(setup);
            break;
            
        case USB_REQUEST_SET_ADDRESS:
            status = usb_handle_set_address(setup);
            break;
            
        case USB_REQUEST_SET_CONFIGURATION:
            status = usb_handle_set_configuration(setup);
            break;
            
        case USB_REQUEST_GET_STATUS:
            status = usb_handle_get_status(setup);
            break;
            
        case USB_REQUEST_CLEAR_FEATURE:
            status = usb_handle_clear_feature(setup);
            break;
            
        case USB_REQUEST_SET_FEATURE:
            status = usb_handle_set_feature(setup);
            break;
            
        default:
            break;
    }

    if (status == USB_ERROR_STALL) {
        usb_endpoint_stall(USB_CONTROL_ENDPOINT);
    }
}

static usb_status_t usb_handle_get_descriptor(usb_setup_packet_t *setup)
{
    uint8_t descriptor_type = (setup->wValue >> 8) & 0xFF;
    uint8_t descriptor_index = setup->wValue & 0xFF;
    uint16_t length = setup->wLength;

    switch (descriptor_type) {
        case USB_DESCRIPTOR_TYPE_DEVICE:
            if (usb_device.config->device_descriptor) {
                uint16_t desc_length = sizeof(usb_device_descriptor_t);
                if (length > desc_length) {
                    length = desc_length;
                }
                return usb_control_send_data((uint8_t*)usb_device.config->device_descriptor, length);
            }
            break;
            
        case USB_DESCRIPTOR_TYPE_CONFIGURATION:
            if (usb_device.config->config_descriptor) {
                uint16_t desc_length = usb_device.config->config_descriptor->wTotalLength;
                if (length > desc_length) {
                    length = desc_length;
                }
                return usb_control_send_data((uint8_t*)usb_device.config->config_descriptor, length);
            }
            break;
            
        case USB_DESCRIPTOR_TYPE_STRING:
            if (descriptor_index == 0) {
                static uint8_t lang_id_descriptor[] = {4, USB_DESCRIPTOR_TYPE_STRING, 0x09, 0x04};
                uint16_t desc_length = sizeof(lang_id_descriptor);
                if (length > desc_length) {
                    length = desc_length;
                }
                return usb_control_send_data(lang_id_descriptor, length);
            } else if (descriptor_index <= usb_device.config->num_string_descriptors &&
                       usb_device.config->string_descriptors[descriptor_index - 1]) {
                char *string = usb_device.config->string_descriptors[descriptor_index - 1];
                uint8_t string_length = strlen(string);
                static uint8_t string_descriptor[64];
                
                string_descriptor[0] = 2 + (string_length * 2);
                string_descriptor[1] = USB_DESCRIPTOR_TYPE_STRING;
                
                for (int i = 0; i < string_length; i++) {
                    string_descriptor[2 + (i * 2)] = string[i];
                    string_descriptor[3 + (i * 2)] = 0;
                }
                
                uint16_t desc_length = string_descriptor[0];
                if (length > desc_length) {
                    length = desc_length;
                }
                return usb_control_send_data(string_descriptor, length);
            }
            break;
    }

    return USB_ERROR_STALL;
}

static usb_status_t usb_handle_set_address(usb_setup_packet_t *setup)
{
    uint8_t address = setup->wValue & 0x7F;
    
    usb_device.device_address = address;
    
    usb_status_t status = usb_control_send_status();
    if (status == USB_SUCCESS) {
        if (address == 0) {
            usb_device.state = USB_DEVICE_STATE_DEFAULT;
        } else {
            usb_device.state = USB_DEVICE_STATE_ADDRESS;
        }
        
        if (usb_device.config->state_callback) {
            usb_device.config->state_callback(usb_device.state);
        }
    }
    
    return status;
}

static usb_status_t usb_handle_set_configuration(usb_setup_packet_t *setup)
{
    uint8_t configuration = setup->wValue & 0xFF;
    
    if (configuration == 0) {
        usb_device.current_configuration = 0;
        usb_device.state = USB_DEVICE_STATE_ADDRESS;
    } else if (configuration == 1) {
        usb_device.current_configuration = configuration;
        usb_device.state = USB_DEVICE_STATE_CONFIGURED;
        
        usb_endpoint_configure(1, USB_ENDPOINT_TYPE_BULK, USB_DIRECTION_IN, 64);
        usb_endpoint_enable(1);
        usb_endpoint_configure(2, USB_ENDPOINT_TYPE_BULK, USB_DIRECTION_OUT, 64);
        usb_endpoint_enable(2);
    } else {
        return USB_ERROR_STALL;
    }
    
    if (usb_device.config->state_callback) {
        usb_device.config->state_callback(usb_device.state);
    }
    
    return usb_control_send_status();
}

static usb_status_t usb_handle_get_status(usb_setup_packet_t *setup)
{
    static uint8_t status_response[2] = {0, 0};
    
    switch (setup->bmRequestType & 0x1F) {
        case 0x00:
            status_response[0] = 0x01;
            break;
        case 0x01:
            status_response[0] = 0x00;
            break;
        case 0x02:
            status_response[0] = 0x00;
            break;
        default:
            return USB_ERROR_STALL;
    }
    
    return usb_control_send_data(status_response, 2);
}

static usb_status_t usb_handle_clear_feature(usb_setup_packet_t *setup)
{
    uint8_t recipient = setup->bmRequestType & 0x1F;
    uint16_t feature = setup->wValue;
    
    if (recipient == 0x02 && feature == 0x00) {
        uint8_t endpoint = setup->wIndex & 0x0F;
        usb_endpoint_clear_stall(endpoint);
        return usb_control_send_status();
    }
    
    return USB_ERROR_STALL;
}

static usb_status_t usb_handle_set_feature(usb_setup_packet_t *setup)
{
    uint8_t recipient = setup->bmRequestType & 0x1F;
    uint16_t feature = setup->wValue;
    
    if (recipient == 0x02 && feature == 0x00) {
        uint8_t endpoint = setup->wIndex & 0x0F;
        usb_endpoint_stall(endpoint);
        return usb_control_send_status();
    }
    
    return USB_ERROR_STALL;
}