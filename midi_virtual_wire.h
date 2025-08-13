#ifndef MIDI_VIRTUAL_WIRE_H
#define MIDI_VIRTUAL_WIRE_H

#include "midi.h"
#include <stdint.h>
#include <stdbool.h>

#define MIDI_VW_MAX_DEVICES 8
#define MIDI_VW_MAX_CONNECTIONS 16
#define MIDI_VW_MESSAGE_BUFFER_SIZE 128
#define MIDI_VW_DEVICE_NAME_LENGTH 32

typedef enum {
    MIDI_VW_SUCCESS = 0,
    MIDI_VW_ERROR_INVALID_PARAM,
    MIDI_VW_ERROR_NOT_INITIALIZED,
    MIDI_VW_ERROR_DEVICE_NOT_FOUND,
    MIDI_VW_ERROR_MAX_DEVICES_REACHED,
    MIDI_VW_ERROR_MAX_CONNECTIONS_REACHED,
    MIDI_VW_ERROR_CONNECTION_EXISTS,
    MIDI_VW_ERROR_CONNECTION_NOT_FOUND,
    MIDI_VW_ERROR_BUFFER_FULL,
    MIDI_VW_ERROR_NO_DATA
} midi_vw_status_t;

typedef enum {
    MIDI_VW_DEVICE_STATE_DISCONNECTED = 0,
    MIDI_VW_DEVICE_STATE_CONNECTED,
    MIDI_VW_DEVICE_STATE_ACTIVE,
    MIDI_VW_DEVICE_STATE_ERROR
} midi_vw_device_state_t;

typedef enum {
    MIDI_VW_FILTER_NONE = 0x00,
    MIDI_VW_FILTER_NOTE = 0x01,
    MIDI_VW_FILTER_CONTROL = 0x02,
    MIDI_VW_FILTER_PROGRAM = 0x04,
    MIDI_VW_FILTER_PITCH_BEND = 0x08,
    MIDI_VW_FILTER_SYSEX = 0x10,
    MIDI_VW_FILTER_REALTIME = 0x20,
    MIDI_VW_FILTER_ALL = 0xFF
} midi_vw_filter_t;

typedef struct {
    uint8_t device_id;
    char name[MIDI_VW_DEVICE_NAME_LENGTH];
    midi_vw_device_state_t state;
    uint32_t last_activity;
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t errors;
    bool is_input;
    bool is_output;
    uint8_t active_channels;
} midi_vw_device_t;

typedef struct {
    uint8_t connection_id;
    uint8_t source_device_id;
    uint8_t dest_device_id;
    uint8_t source_channel;
    uint8_t dest_channel;
    midi_vw_filter_t filter;
    bool enabled;
    uint32_t messages_routed;
    uint32_t messages_filtered;
} midi_vw_connection_t;

typedef struct {
    midi_message_t messages[MIDI_VW_MESSAGE_BUFFER_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t overruns;
} midi_vw_message_buffer_t;

typedef struct {
    midi_vw_device_t device;
    midi_vw_message_buffer_t rx_buffer;
    midi_vw_message_buffer_t tx_buffer;
    bool active;
} midi_vw_port_t;

typedef void (*midi_vw_device_callback_t)(uint8_t device_id, midi_vw_device_state_t state);
typedef void (*midi_vw_message_callback_t)(uint8_t device_id, midi_message_t *message);
typedef bool (*midi_vw_filter_callback_t)(uint8_t source_device_id, uint8_t dest_device_id, midi_message_t *message);

typedef struct {
    midi_vw_device_callback_t device_callback;
    midi_vw_message_callback_t message_callback;
    midi_vw_filter_callback_t filter_callback;
} midi_vw_callbacks_t;

midi_vw_status_t midi_vw_init(midi_vw_callbacks_t *callbacks);
midi_vw_status_t midi_vw_deinit(void);
midi_vw_status_t midi_vw_start(void);
midi_vw_status_t midi_vw_stop(void);

midi_vw_status_t midi_vw_register_device(const char *name, bool is_input, bool is_output, uint8_t *device_id);
midi_vw_status_t midi_vw_unregister_device(uint8_t device_id);
midi_vw_status_t midi_vw_get_device_info(uint8_t device_id, midi_vw_device_t *device_info);
midi_vw_status_t midi_vw_set_device_state(uint8_t device_id, midi_vw_device_state_t state);

midi_vw_status_t midi_vw_create_connection(uint8_t source_device_id, uint8_t dest_device_id, 
                                          uint8_t source_channel, uint8_t dest_channel,
                                          midi_vw_filter_t filter, uint8_t *connection_id);
midi_vw_status_t midi_vw_remove_connection(uint8_t connection_id);
midi_vw_status_t midi_vw_enable_connection(uint8_t connection_id, bool enabled);
midi_vw_status_t midi_vw_get_connection_info(uint8_t connection_id, midi_vw_connection_t *connection_info);

midi_vw_status_t midi_vw_connect_all_to_all(void);
midi_vw_status_t midi_vw_disconnect_all(void);

midi_vw_status_t midi_vw_send_message(uint8_t device_id, midi_message_t *message);
midi_vw_status_t midi_vw_receive_message(uint8_t device_id, midi_message_t *message);
midi_vw_status_t midi_vw_inject_message(uint8_t source_device_id, midi_message_t *message);

bool midi_vw_has_pending_messages(uint8_t device_id);
uint16_t midi_vw_get_pending_count(uint8_t device_id);

midi_vw_status_t midi_vw_process_messages(void);

uint8_t midi_vw_get_device_count(void);
uint8_t midi_vw_get_connection_count(void);
midi_vw_status_t midi_vw_list_devices(uint8_t *device_ids, uint8_t max_devices, uint8_t *count);
midi_vw_status_t midi_vw_list_connections(uint8_t *connection_ids, uint8_t max_connections, uint8_t *count);

midi_vw_status_t midi_vw_get_statistics(uint32_t *total_messages, uint32_t *total_errors, uint32_t *total_filtered);
midi_vw_status_t midi_vw_reset_statistics(void);

#endif