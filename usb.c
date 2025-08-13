#include "usb.h"
#include <string.h>
#include <stddef.h>

static struct {
    bool initialized;
    usb_device_state_t state;
    usb_config_t *config;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t device_address;
    uint8_t current_configuration;
} usb_device;

static usb_status_t usb_hw_init(void);
static usb_status_t usb_hw_deinit(void);
static usb_status_t usb_hw_start(void);
static usb_status_t usb_hw_stop(void);
static usb_status_t usb_hw_endpoint_configure(uint8_t endpoint_num, 
                                             usb_endpoint_type_t type,
                                             usb_direction_t direction,
                                             uint16_t max_packet_size);
static usb_status_t usb_hw_endpoint_enable(uint8_t endpoint_num);
static usb_status_t usb_hw_endpoint_disable(uint8_t endpoint_num);
static usb_status_t usb_hw_endpoint_stall(uint8_t endpoint_num);
static usb_status_t usb_hw_endpoint_clear_stall(uint8_t endpoint_num);
static usb_status_t usb_hw_transmit(uint8_t endpoint_num, uint8_t *data, uint16_t length);
static usb_status_t usb_hw_receive(uint8_t endpoint_num, uint8_t *buffer, uint16_t max_length);

usb_status_t usb_init(usb_config_t *config)
{
    if (config == NULL || config->device_descriptor == NULL) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (usb_device.initialized) {
        return USB_ERROR_BUSY;
    }

    memset(&usb_device, 0, sizeof(usb_device));
    
    usb_device.config = config;
    usb_device.state = USB_DEVICE_STATE_DETACHED;
    usb_device.device_address = 0;
    usb_device.current_configuration = 0;

    for (int i = 0; i < USB_MAX_ENDPOINTS; i++) {
        usb_device.endpoints[i].endpoint_num = i;
        usb_device.endpoints[i].enabled = false;
        usb_device.endpoints[i].transfer_complete = true;
    }

    usb_status_t status = usb_hw_init();
    if (status != USB_SUCCESS) {
        return status;
    }

    usb_device.initialized = true;
    return USB_SUCCESS;
}

usb_status_t usb_deinit(void)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    usb_stop();
    usb_hw_deinit();
    
    memset(&usb_device, 0, sizeof(usb_device));
    return USB_SUCCESS;
}

usb_status_t usb_start(void)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    usb_status_t status = usb_hw_start();
    if (status != USB_SUCCESS) {
        return status;
    }

    usb_device.state = USB_DEVICE_STATE_ATTACHED;
    
    if (usb_device.config->state_callback) {
        usb_device.config->state_callback(usb_device.state);
    }

    return USB_SUCCESS;
}

usb_status_t usb_stop(void)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    usb_hw_stop();
    usb_device.state = USB_DEVICE_STATE_DETACHED;
    
    if (usb_device.config->state_callback) {
        usb_device.config->state_callback(usb_device.state);
    }

    return USB_SUCCESS;
}

usb_device_state_t usb_get_state(void)
{
    return usb_device.state;
}

usb_status_t usb_endpoint_configure(uint8_t endpoint_num, 
                                   usb_endpoint_type_t type,
                                   usb_direction_t direction,
                                   uint16_t max_packet_size)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (max_packet_size > USB_MAX_PACKET_SIZE) {
        return USB_ERROR_INVALID_PARAM;
    }

    usb_endpoint_t *ep = &usb_device.endpoints[endpoint_num];
    ep->type = type;
    ep->direction = direction;
    ep->max_packet_size = max_packet_size;

    return usb_hw_endpoint_configure(endpoint_num, type, direction, max_packet_size);
}

usb_status_t usb_endpoint_enable(uint8_t endpoint_num)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS) {
        return USB_ERROR_INVALID_PARAM;
    }

    usb_device.endpoints[endpoint_num].enabled = true;
    return usb_hw_endpoint_enable(endpoint_num);
}

usb_status_t usb_endpoint_disable(uint8_t endpoint_num)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS) {
        return USB_ERROR_INVALID_PARAM;
    }

    usb_device.endpoints[endpoint_num].enabled = false;
    return usb_hw_endpoint_disable(endpoint_num);
}

usb_status_t usb_endpoint_stall(uint8_t endpoint_num)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS) {
        return USB_ERROR_INVALID_PARAM;
    }

    return usb_hw_endpoint_stall(endpoint_num);
}

usb_status_t usb_endpoint_clear_stall(uint8_t endpoint_num)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS) {
        return USB_ERROR_INVALID_PARAM;
    }

    return usb_hw_endpoint_clear_stall(endpoint_num);
}

usb_status_t usb_transmit(uint8_t endpoint_num, uint8_t *data, uint16_t length)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS || data == NULL) {
        return USB_ERROR_INVALID_PARAM;
    }

    usb_endpoint_t *ep = &usb_device.endpoints[endpoint_num];
    
    if (!ep->enabled) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (ep->direction != USB_DIRECTION_IN && endpoint_num != USB_CONTROL_ENDPOINT) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (!ep->transfer_complete) {
        return USB_ERROR_BUSY;
    }

    if (length > ep->max_packet_size) {
        return USB_ERROR_BUFFER_OVERFLOW;
    }

    ep->transfer_complete = false;
    ep->data_length = length;

    return usb_hw_transmit(endpoint_num, data, length);
}

usb_status_t usb_receive(uint8_t endpoint_num, uint8_t *buffer, uint16_t max_length)
{
    if (!usb_device.initialized) {
        return USB_ERROR_NOT_INITIALIZED;
    }

    if (endpoint_num >= USB_MAX_ENDPOINTS || buffer == NULL) {
        return USB_ERROR_INVALID_PARAM;
    }

    usb_endpoint_t *ep = &usb_device.endpoints[endpoint_num];
    
    if (!ep->enabled) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (ep->direction != USB_DIRECTION_OUT && endpoint_num != USB_CONTROL_ENDPOINT) {
        return USB_ERROR_INVALID_PARAM;
    }

    if (!ep->transfer_complete) {
        return USB_ERROR_BUSY;
    }

    ep->transfer_complete = false;
    ep->buffer = buffer;
    ep->buffer_size = max_length;

    return usb_hw_receive(endpoint_num, buffer, max_length);
}

usb_status_t usb_control_send_status(void)
{
    return usb_transmit(USB_CONTROL_ENDPOINT, NULL, 0);
}

usb_status_t usb_control_send_data(uint8_t *data, uint16_t length)
{
    return usb_transmit(USB_CONTROL_ENDPOINT, data, length);
}

usb_status_t usb_control_receive_data(uint8_t *buffer, uint16_t max_length)
{
    return usb_receive(USB_CONTROL_ENDPOINT, buffer, max_length);
}

static void usb_handle_setup_packet(usb_setup_packet_t *setup)
{
    if (usb_device.config->setup_callback) {
        usb_device.config->setup_callback(setup);
    }
}

static void usb_handle_transfer_complete(uint8_t endpoint_num, usb_status_t status)
{
    if (endpoint_num < USB_MAX_ENDPOINTS) {
        usb_device.endpoints[endpoint_num].transfer_complete = true;
    }

    if (usb_device.config->transfer_callback) {
        usb_device.config->transfer_callback(endpoint_num, status);
    }
}

static void usb_set_state(usb_device_state_t new_state)
{
    if (usb_device.state != new_state) {
        usb_device.state = new_state;
        
        if (usb_device.config->state_callback) {
            usb_device.config->state_callback(new_state);
        }
    }
}

void usb_interrupt_handler(void)
{
    // This function will be implemented by hardware-specific code
    // It should call the appropriate handlers above based on interrupt status
}

void usb_handle_standard_setup(usb_setup_packet_t *setup)
{
    // Simple stub - in a real implementation this would handle standard USB requests
    (void)setup;
    usb_control_send_status();
}

// Hardware abstraction layer - these functions need to be implemented
// for the specific microcontroller being used

static usb_status_t usb_hw_init(void)
{
    // Initialize USB peripheral hardware
    // Configure clocks, pins, etc.
    return USB_SUCCESS;
}

static usb_status_t usb_hw_deinit(void)
{
    // Deinitialize USB peripheral hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_start(void)
{
    // Enable USB peripheral and connect to bus
    return USB_SUCCESS;
}

static usb_status_t usb_hw_stop(void)
{
    // Disable USB peripheral and disconnect from bus
    return USB_SUCCESS;
}

static usb_status_t usb_hw_endpoint_configure(uint8_t endpoint_num, 
                                             usb_endpoint_type_t type,
                                             usb_direction_t direction,
                                             uint16_t max_packet_size)
{
    // Configure endpoint in hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_endpoint_enable(uint8_t endpoint_num)
{
    // Enable endpoint in hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_endpoint_disable(uint8_t endpoint_num)
{
    // Disable endpoint in hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_endpoint_stall(uint8_t endpoint_num)
{
    // Stall endpoint in hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_endpoint_clear_stall(uint8_t endpoint_num)
{
    // Clear endpoint stall in hardware
    return USB_SUCCESS;
}

static usb_status_t usb_hw_transmit(uint8_t endpoint_num, uint8_t *data, uint16_t length)
{
    // Transmit data on endpoint
    return USB_SUCCESS;
}

static usb_status_t usb_hw_receive(uint8_t endpoint_num, uint8_t *buffer, uint16_t max_length)
{
    // Prepare to receive data on endpoint
    return USB_SUCCESS;
}