#include "midi.h"
#include "usb.h"
#include <string.h>
#include <stddef.h>

extern usb_device_descriptor_t midi_device_descriptor;
extern usb_config_descriptor_t *midi_config_descriptor;
extern char* midi_string_descriptors[];
#define MIDI_NUM_STRING_DESCRIPTORS 3

#define MIDI_ENDPOINT_OUT 0x01
#define MIDI_ENDPOINT_IN 0x81

typedef struct {
    midi_message_t messages[MIDI_BUFFER_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} midi_buffer_t;

static struct {
    bool initialized;
    bool started;
    midi_callbacks_t callbacks;
    midi_buffer_t rx_buffer;
    midi_buffer_t tx_buffer;
    uint8_t usb_rx_buffer[64];
    uint8_t usb_tx_buffer[64];
    uint8_t sysex_buffer[256];
    uint16_t sysex_length;
    bool in_sysex;
} midi_device;

static void midi_setup_callback(usb_setup_packet_t *setup);
static void midi_transfer_callback(uint8_t endpoint, usb_status_t status);
static void midi_state_callback(usb_device_state_t state);
static void midi_process_usb_packet(uint8_t *data, uint16_t length);
static void midi_process_midi_event(usb_midi_event_t *event);
static uint8_t midi_get_message_length(uint8_t status);
static uint8_t midi_get_code_index(uint8_t status);
static midi_status_t midi_buffer_put(midi_buffer_t *buffer, midi_message_t *message);
static midi_status_t midi_buffer_get(midi_buffer_t *buffer, midi_message_t *message);
static bool midi_buffer_is_empty(midi_buffer_t *buffer);
static bool midi_buffer_is_full(midi_buffer_t *buffer);

void usb_handle_standard_setup(usb_setup_packet_t *setup);

midi_status_t midi_init(midi_callbacks_t *callbacks)
{
    if (midi_device.initialized) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    memset(&midi_device, 0, sizeof(midi_device));
    
    if (callbacks) {
        midi_device.callbacks = *callbacks;
    }

    usb_config_t usb_config = {
        .device_descriptor = &midi_device_descriptor,
        .config_descriptor = midi_config_descriptor,
        .string_descriptors = midi_string_descriptors,
        .num_string_descriptors = MIDI_NUM_STRING_DESCRIPTORS,
        .setup_callback = midi_setup_callback,
        .transfer_callback = midi_transfer_callback,
        .state_callback = midi_state_callback
    };

    usb_status_t status = usb_init(&usb_config);
    if (status != USB_SUCCESS) {
        return MIDI_ERROR_USB_ERROR;
    }

    usb_endpoint_configure(0, USB_ENDPOINT_TYPE_CONTROL, USB_DIRECTION_IN, 64);
    usb_endpoint_enable(0);

    midi_device.initialized = true;
    return MIDI_SUCCESS;
}

midi_status_t midi_deinit(void)
{
    if (!midi_device.initialized) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    midi_stop();
    usb_deinit();
    
    memset(&midi_device, 0, sizeof(midi_device));
    return MIDI_SUCCESS;
}

midi_status_t midi_start(void)
{
    if (!midi_device.initialized) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    if (midi_device.started) {
        return MIDI_SUCCESS;
    }

    usb_status_t status = usb_start();
    if (status != USB_SUCCESS) {
        return MIDI_ERROR_USB_ERROR;
    }

    midi_device.started = true;
    return MIDI_SUCCESS;
}

midi_status_t midi_stop(void)
{
    if (!midi_device.initialized) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    usb_stop();
    midi_device.started = false;
    return MIDI_SUCCESS;
}

midi_status_t midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_message_t message = {
        .status = MIDI_MSG_NOTE_ON | (channel & 0x0F),
        .data = {note, velocity, 0},
        .length = 3,
        .timestamp = 0
    };
    
    return midi_send_message(&message);
}

midi_status_t midi_send_note_off(uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_message_t message = {
        .status = MIDI_MSG_NOTE_OFF | (channel & 0x0F),
        .data = {note, velocity, 0},
        .length = 3,
        .timestamp = 0
    };
    
    return midi_send_message(&message);
}

midi_status_t midi_send_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    midi_message_t message = {
        .status = MIDI_MSG_CONTROL_CHANGE | (channel & 0x0F),
        .data = {controller, value, 0},
        .length = 3,
        .timestamp = 0
    };
    
    return midi_send_message(&message);
}

midi_status_t midi_send_program_change(uint8_t channel, uint8_t program)
{
    midi_message_t message = {
        .status = MIDI_MSG_PROGRAM_CHANGE | (channel & 0x0F),
        .data = {program, 0, 0},
        .length = 2,
        .timestamp = 0
    };
    
    return midi_send_message(&message);
}

midi_status_t midi_send_pitch_bend(uint8_t channel, uint16_t bend)
{
    midi_message_t message = {
        .status = MIDI_MSG_PITCH_BEND | (channel & 0x0F),
        .data = {bend & 0x7F, (bend >> 7) & 0x7F, 0},
        .length = 3,
        .timestamp = 0
    };
    
    return midi_send_message(&message);
}

midi_status_t midi_send_sysex(uint8_t *data, uint16_t length)
{
    if (!data || length == 0) {
        return MIDI_ERROR_INVALID_PARAM;
    }

    usb_midi_event_t events[64];
    uint16_t event_count = 0;
    uint16_t data_index = 0;

    events[event_count].cable_number = 0;
    events[event_count].code_index = 0x04;
    events[event_count].midi_data[0] = MIDI_MSG_SYSTEM_EXCLUSIVE;
    
    if (length == 1) {
        events[event_count].midi_data[1] = data[0];
        events[event_count].midi_data[2] = MIDI_MSG_END_SYSEX;
        event_count++;
    } else {
        events[event_count].midi_data[1] = data[0];
        events[event_count].midi_data[2] = data[1];
        event_count++;
        data_index = 2;

        while (data_index < length - 1) {
            events[event_count].cable_number = 0;
            events[event_count].code_index = 0x04;
            
            uint8_t remaining = length - data_index;
            if (remaining >= 3) {
                events[event_count].midi_data[0] = data[data_index++];
                events[event_count].midi_data[1] = data[data_index++];
                events[event_count].midi_data[2] = data[data_index++];
            } else if (remaining == 2) {
                events[event_count].midi_data[0] = data[data_index++];
                events[event_count].midi_data[1] = data[data_index++];
                events[event_count].midi_data[2] = MIDI_MSG_END_SYSEX;
            } else {
                events[event_count].midi_data[0] = data[data_index++];
                events[event_count].midi_data[1] = MIDI_MSG_END_SYSEX;
                events[event_count].midi_data[2] = 0;
            }
            event_count++;
        }

        if (data_index == length - 1) {
            events[event_count].cable_number = 0;
            events[event_count].code_index = 0x05;
            events[event_count].midi_data[0] = data[data_index];
            events[event_count].midi_data[1] = MIDI_MSG_END_SYSEX;
            events[event_count].midi_data[2] = 0;
            event_count++;
        }
    }

    usb_status_t status = usb_transmit(MIDI_ENDPOINT_IN, (uint8_t*)events, event_count * 4);
    return (status == USB_SUCCESS) ? MIDI_SUCCESS : MIDI_ERROR_USB_ERROR;
}

midi_status_t midi_send_message(midi_message_t *message)
{
    if (!message) {
        return MIDI_ERROR_INVALID_PARAM;
    }

    if (!midi_device.initialized || !midi_device.started) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    usb_midi_event_t event;
    event.cable_number = 0;
    event.code_index = midi_get_code_index(message->status);
    event.midi_data[0] = message->status;
    event.midi_data[1] = (message->length > 1) ? message->data[0] : 0;
    event.midi_data[2] = (message->length > 2) ? message->data[1] : 0;

    usb_status_t status = usb_transmit(MIDI_ENDPOINT_IN, (uint8_t*)&event, 4);
    return (status == USB_SUCCESS) ? MIDI_SUCCESS : MIDI_ERROR_USB_ERROR;
}

midi_status_t midi_receive_message(midi_message_t *message)
{
    if (!message) {
        return MIDI_ERROR_INVALID_PARAM;
    }

    if (!midi_device.initialized) {
        return MIDI_ERROR_NOT_INITIALIZED;
    }

    return midi_buffer_get(&midi_device.rx_buffer, message);
}

bool midi_has_pending_messages(void)
{
    return !midi_buffer_is_empty(&midi_device.rx_buffer);
}

uint16_t midi_get_pending_count(void)
{
    return midi_device.rx_buffer.count;
}

static void midi_setup_callback(usb_setup_packet_t *setup)
{
    if ((setup->bmRequestType & 0x60) == 0x00) {
        usb_handle_standard_setup(setup);
    } else {
        usb_endpoint_stall(USB_CONTROL_ENDPOINT);
    }
}

static void midi_transfer_callback(uint8_t endpoint, usb_status_t status)
{
    if (status != USB_SUCCESS) {
        return;
    }

    if (endpoint == MIDI_ENDPOINT_OUT) {
        usb_receive(MIDI_ENDPOINT_OUT, midi_device.usb_rx_buffer, sizeof(midi_device.usb_rx_buffer));
    }
}

static void midi_state_callback(usb_device_state_t state)
{
    if (state == USB_DEVICE_STATE_CONFIGURED) {
        usb_endpoint_configure(MIDI_ENDPOINT_OUT, USB_ENDPOINT_TYPE_BULK, USB_DIRECTION_OUT, 64);
        usb_endpoint_enable(MIDI_ENDPOINT_OUT);
        usb_endpoint_configure(MIDI_ENDPOINT_IN & 0x7F, USB_ENDPOINT_TYPE_BULK, USB_DIRECTION_IN, 64);
        usb_endpoint_enable(MIDI_ENDPOINT_IN & 0x7F);
        
        usb_receive(MIDI_ENDPOINT_OUT, midi_device.usb_rx_buffer, sizeof(midi_device.usb_rx_buffer));
    }
}

static void midi_process_usb_packet(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i += 4) {
        usb_midi_event_t *event = (usb_midi_event_t*)(data + i);
        midi_process_midi_event(event);
    }
}

static void midi_process_midi_event(usb_midi_event_t *event)
{
    if (event->code_index == 0 || event->code_index > 15) {
        return;
    }

    uint8_t status = event->midi_data[0];
    uint8_t channel = status & 0x0F;
    uint8_t message_type = status & 0xF0;

    midi_message_t message = {
        .status = status,
        .length = midi_get_message_length(status),
        .timestamp = 0
    };

    if (message.length > 1) message.data[0] = event->midi_data[1];
    if (message.length > 2) message.data[1] = event->midi_data[2];

    midi_buffer_put(&midi_device.rx_buffer, &message);

    switch (message_type) {
        case MIDI_MSG_NOTE_ON:
            if (midi_device.callbacks.note_on_callback) {
                midi_device.callbacks.note_on_callback(channel, event->midi_data[1], event->midi_data[2]);
            }
            break;
            
        case MIDI_MSG_NOTE_OFF:
            if (midi_device.callbacks.note_off_callback) {
                midi_device.callbacks.note_off_callback(channel, event->midi_data[1], event->midi_data[2]);
            }
            break;
            
        case MIDI_MSG_CONTROL_CHANGE:
            if (midi_device.callbacks.control_change_callback) {
                midi_device.callbacks.control_change_callback(channel, event->midi_data[1], event->midi_data[2]);
            }
            break;
            
        case MIDI_MSG_PROGRAM_CHANGE:
            if (midi_device.callbacks.program_change_callback) {
                midi_device.callbacks.program_change_callback(channel, event->midi_data[1]);
            }
            break;
            
        case MIDI_MSG_PITCH_BEND:
            if (midi_device.callbacks.pitch_bend_callback) {
                uint16_t bend = event->midi_data[1] | (event->midi_data[2] << 7);
                midi_device.callbacks.pitch_bend_callback(channel, bend);
            }
            break;
            
        case MIDI_MSG_SYSTEM_EXCLUSIVE:
            if (!midi_device.in_sysex) {
                midi_device.in_sysex = true;
                midi_device.sysex_length = 0;
            }
            
            for (int i = 0; i < 3; i++) {
                if (event->midi_data[i] == MIDI_MSG_END_SYSEX) {
                    midi_device.in_sysex = false;
                    if (midi_device.callbacks.sysex_callback) {
                        midi_device.callbacks.sysex_callback(midi_device.sysex_buffer, midi_device.sysex_length);
                    }
                    break;
                } else if (midi_device.sysex_length < sizeof(midi_device.sysex_buffer)) {
                    midi_device.sysex_buffer[midi_device.sysex_length++] = event->midi_data[i];
                }
            }
            break;
    }
}

static uint8_t midi_get_message_length(uint8_t status)
{
    switch (status & 0xF0) {
        case MIDI_MSG_NOTE_OFF:
        case MIDI_MSG_NOTE_ON:
        case MIDI_MSG_POLY_PRESSURE:
        case MIDI_MSG_CONTROL_CHANGE:
        case MIDI_MSG_PITCH_BEND:
            return 3;
        case MIDI_MSG_PROGRAM_CHANGE:
        case MIDI_MSG_CHANNEL_PRESSURE:
            return 2;
        default:
            if (status >= 0xF0) {
                switch (status) {
                    case MIDI_MSG_SONG_POSITION:
                        return 3;
                    case MIDI_MSG_TIME_CODE:
                    case MIDI_MSG_SONG_SELECT:
                        return 2;
                    default:
                        return 1;
                }
            }
            return 1;
    }
}

static uint8_t midi_get_code_index(uint8_t status)
{
    switch (status & 0xF0) {
        case MIDI_MSG_NOTE_OFF:
            return 0x08;
        case MIDI_MSG_NOTE_ON:
            return 0x09;
        case MIDI_MSG_POLY_PRESSURE:
            return 0x0A;
        case MIDI_MSG_CONTROL_CHANGE:
            return 0x0B;
        case MIDI_MSG_PROGRAM_CHANGE:
            return 0x0C;
        case MIDI_MSG_CHANNEL_PRESSURE:
            return 0x0D;
        case MIDI_MSG_PITCH_BEND:
            return 0x0E;
        case MIDI_MSG_SYSTEM_EXCLUSIVE:
            return 0x04;
        default:
            return 0x0F;
    }
}

static midi_status_t midi_buffer_put(midi_buffer_t *buffer, midi_message_t *message)
{
    if (midi_buffer_is_full(buffer)) {
        return MIDI_ERROR_BUFFER_FULL;
    }

    buffer->messages[buffer->head] = *message;
    buffer->head = (buffer->head + 1) % MIDI_BUFFER_SIZE;
    buffer->count++;
    
    return MIDI_SUCCESS;
}

static midi_status_t midi_buffer_get(midi_buffer_t *buffer, midi_message_t *message)
{
    if (midi_buffer_is_empty(buffer)) {
        return MIDI_ERROR_NO_DATA;
    }

    *message = buffer->messages[buffer->tail];
    buffer->tail = (buffer->tail + 1) % MIDI_BUFFER_SIZE;
    buffer->count--;
    
    return MIDI_SUCCESS;
}

static bool midi_buffer_is_empty(midi_buffer_t *buffer)
{
    return buffer->count == 0;
}

static bool midi_buffer_is_full(midi_buffer_t *buffer)
{
    return buffer->count >= MIDI_BUFFER_SIZE;
}