#include "midi_virtual_wire.h"
#include <string.h>
#include <stddef.h>

static struct {
    bool initialized;
    bool running;
    midi_vw_callbacks_t callbacks;
    midi_vw_port_t ports[MIDI_VW_MAX_DEVICES];
    midi_vw_connection_t connections[MIDI_VW_MAX_CONNECTIONS];
    uint8_t device_count;
    uint8_t connection_count;
    uint8_t next_device_id;
    uint8_t next_connection_id;
    uint32_t total_messages;
    uint32_t total_errors;
    uint32_t total_filtered;
    uint32_t system_time;
} midi_vw_system;

static midi_vw_status_t midi_vw_buffer_put(midi_vw_message_buffer_t *buffer, midi_message_t *message);
static midi_vw_status_t midi_vw_buffer_get(midi_vw_message_buffer_t *buffer, midi_message_t *message);
static bool midi_vw_buffer_is_empty(midi_vw_message_buffer_t *buffer);
static bool midi_vw_buffer_is_full(midi_vw_message_buffer_t *buffer);
static uint8_t midi_vw_find_device(uint8_t device_id);
static uint8_t midi_vw_find_connection(uint8_t connection_id);
static bool midi_vw_should_filter_message(midi_vw_connection_t *connection, midi_message_t *message);
static void midi_vw_route_message(uint8_t source_device_id, midi_message_t *message);
static uint32_t midi_vw_get_time(void);

midi_vw_status_t midi_vw_init(midi_vw_callbacks_t *callbacks)
{
    if (midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    memset(&midi_vw_system, 0, sizeof(midi_vw_system));
    
    if (callbacks) {
        midi_vw_system.callbacks = *callbacks;
    }

    midi_vw_system.next_device_id = 1;
    midi_vw_system.next_connection_id = 1;
    midi_vw_system.initialized = true;

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_deinit(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_stop();
    memset(&midi_vw_system, 0, sizeof(midi_vw_system));
    
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_start(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_system.running = true;
    midi_vw_system.system_time = 0;
    
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_stop(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_system.running = false;
    
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_register_device(const char *name, bool is_input, bool is_output, uint8_t *device_id)
{
    if (!midi_vw_system.initialized || !name || !device_id) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    if (midi_vw_system.device_count >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_MAX_DEVICES_REACHED;
    }

    uint8_t slot = midi_vw_system.device_count;
    midi_vw_port_t *port = &midi_vw_system.ports[slot];
    
    memset(port, 0, sizeof(midi_vw_port_t));
    
    port->device.device_id = midi_vw_system.next_device_id++;
    strncpy(port->device.name, name, MIDI_VW_DEVICE_NAME_LENGTH - 1);
    port->device.name[MIDI_VW_DEVICE_NAME_LENGTH - 1] = '\0';
    port->device.state = MIDI_VW_DEVICE_STATE_CONNECTED;
    port->device.is_input = is_input;
    port->device.is_output = is_output;
    port->device.last_activity = midi_vw_get_time();
    port->active = true;
    
    *device_id = port->device.device_id;
    midi_vw_system.device_count++;

    if (midi_vw_system.callbacks.device_callback) {
        midi_vw_system.callbacks.device_callback(port->device.device_id, MIDI_VW_DEVICE_STATE_CONNECTED);
    }

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_unregister_device(uint8_t device_id)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    midi_vw_port_t *port = &midi_vw_system.ports[slot];
    
    for (uint8_t i = 0; i < midi_vw_system.connection_count; i++) {
        if (midi_vw_system.connections[i].source_device_id == device_id ||
            midi_vw_system.connections[i].dest_device_id == device_id) {
            midi_vw_remove_connection(midi_vw_system.connections[i].connection_id);
        }
    }

    if (midi_vw_system.callbacks.device_callback) {
        midi_vw_system.callbacks.device_callback(device_id, MIDI_VW_DEVICE_STATE_DISCONNECTED);
    }

    for (uint8_t i = slot; i < midi_vw_system.device_count - 1; i++) {
        midi_vw_system.ports[i] = midi_vw_system.ports[i + 1];
    }
    
    midi_vw_system.device_count--;
    memset(&midi_vw_system.ports[midi_vw_system.device_count], 0, sizeof(midi_vw_port_t));

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_get_device_info(uint8_t device_id, midi_vw_device_t *device_info)
{
    if (!midi_vw_system.initialized || !device_info) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    *device_info = midi_vw_system.ports[slot].device;
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_set_device_state(uint8_t device_id, midi_vw_device_state_t state)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    midi_vw_port_t *port = &midi_vw_system.ports[slot];
    port->device.state = state;

    if (midi_vw_system.callbacks.device_callback) {
        midi_vw_system.callbacks.device_callback(device_id, state);
    }

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_create_connection(uint8_t source_device_id, uint8_t dest_device_id, 
                                          uint8_t source_channel, uint8_t dest_channel,
                                          midi_vw_filter_t filter, uint8_t *connection_id)
{
    if (!midi_vw_system.initialized || !connection_id) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    if (midi_vw_system.connection_count >= MIDI_VW_MAX_CONNECTIONS) {
        return MIDI_VW_ERROR_MAX_CONNECTIONS_REACHED;
    }

    if (midi_vw_find_device(source_device_id) >= MIDI_VW_MAX_DEVICES ||
        midi_vw_find_device(dest_device_id) >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    for (uint8_t i = 0; i < midi_vw_system.connection_count; i++) {
        if (midi_vw_system.connections[i].source_device_id == source_device_id &&
            midi_vw_system.connections[i].dest_device_id == dest_device_id &&
            midi_vw_system.connections[i].source_channel == source_channel &&
            midi_vw_system.connections[i].dest_channel == dest_channel) {
            return MIDI_VW_ERROR_CONNECTION_EXISTS;
        }
    }

    midi_vw_connection_t *connection = &midi_vw_system.connections[midi_vw_system.connection_count];
    memset(connection, 0, sizeof(midi_vw_connection_t));
    
    connection->connection_id = midi_vw_system.next_connection_id++;
    connection->source_device_id = source_device_id;
    connection->dest_device_id = dest_device_id;
    connection->source_channel = source_channel;
    connection->dest_channel = dest_channel;
    connection->filter = filter;
    connection->enabled = true;

    *connection_id = connection->connection_id;
    midi_vw_system.connection_count++;

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_remove_connection(uint8_t connection_id)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    uint8_t slot = midi_vw_find_connection(connection_id);
    if (slot >= MIDI_VW_MAX_CONNECTIONS) {
        return MIDI_VW_ERROR_CONNECTION_NOT_FOUND;
    }

    for (uint8_t i = slot; i < midi_vw_system.connection_count - 1; i++) {
        midi_vw_system.connections[i] = midi_vw_system.connections[i + 1];
    }
    
    midi_vw_system.connection_count--;
    memset(&midi_vw_system.connections[midi_vw_system.connection_count], 0, sizeof(midi_vw_connection_t));

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_enable_connection(uint8_t connection_id, bool enabled)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    uint8_t slot = midi_vw_find_connection(connection_id);
    if (slot >= MIDI_VW_MAX_CONNECTIONS) {
        return MIDI_VW_ERROR_CONNECTION_NOT_FOUND;
    }

    midi_vw_system.connections[slot].enabled = enabled;
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_get_connection_info(uint8_t connection_id, midi_vw_connection_t *connection_info)
{
    if (!midi_vw_system.initialized || !connection_info) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    uint8_t slot = midi_vw_find_connection(connection_id);
    if (slot >= MIDI_VW_MAX_CONNECTIONS) {
        return MIDI_VW_ERROR_CONNECTION_NOT_FOUND;
    }

    *connection_info = midi_vw_system.connections[slot];
    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_connect_all_to_all(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    for (uint8_t i = 0; i < midi_vw_system.device_count; i++) {
        for (uint8_t j = 0; j < midi_vw_system.device_count; j++) {
            if (i != j) {
                uint8_t source_id = midi_vw_system.ports[i].device.device_id;
                uint8_t dest_id = midi_vw_system.ports[j].device.device_id;
                
                if (midi_vw_system.ports[i].device.is_input && 
                    midi_vw_system.ports[j].device.is_output) {
                    uint8_t connection_id;
                    midi_vw_create_connection(source_id, dest_id, 0xFF, 0xFF, 
                                            MIDI_VW_FILTER_NONE, &connection_id);
                }
            }
        }
    }

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_disconnect_all(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_system.connection_count = 0;
    memset(midi_vw_system.connections, 0, sizeof(midi_vw_system.connections));

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_send_message(uint8_t device_id, midi_message_t *message)
{
    if (!midi_vw_system.initialized || !message) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    midi_vw_port_t *port = &midi_vw_system.ports[slot];
    if (!port->device.is_output) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    midi_vw_status_t status = midi_vw_buffer_put(&port->tx_buffer, message);
    if (status == MIDI_VW_SUCCESS) {
        port->device.messages_sent++;
        port->device.last_activity = midi_vw_get_time();
    }

    return status;
}

midi_vw_status_t midi_vw_receive_message(uint8_t device_id, midi_message_t *message)
{
    if (!midi_vw_system.initialized || !message) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return MIDI_VW_ERROR_DEVICE_NOT_FOUND;
    }

    return midi_vw_buffer_get(&midi_vw_system.ports[slot].rx_buffer, message);
}

midi_vw_status_t midi_vw_inject_message(uint8_t source_device_id, midi_message_t *message)
{
    if (!midi_vw_system.initialized || !message) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    if (!midi_vw_system.running) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    message->timestamp = midi_vw_get_time();
    midi_vw_route_message(source_device_id, message);

    return MIDI_VW_SUCCESS;
}

bool midi_vw_has_pending_messages(uint8_t device_id)
{
    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return false;
    }

    return !midi_vw_buffer_is_empty(&midi_vw_system.ports[slot].rx_buffer);
}

uint16_t midi_vw_get_pending_count(uint8_t device_id)
{
    uint8_t slot = midi_vw_find_device(device_id);
    if (slot >= MIDI_VW_MAX_DEVICES) {
        return 0;
    }

    return midi_vw_system.ports[slot].rx_buffer.count;
}

midi_vw_status_t midi_vw_process_messages(void)
{
    if (!midi_vw_system.initialized || !midi_vw_system.running) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_system.system_time++;

    for (uint8_t i = 0; i < midi_vw_system.device_count; i++) {
        midi_vw_port_t *port = &midi_vw_system.ports[i];
        
        if (!port->active || !port->device.is_input) {
            continue;
        }

        midi_message_t message;
        while (midi_vw_buffer_get(&port->rx_buffer, &message) == MIDI_VW_SUCCESS) {
            port->device.messages_received++;
            port->device.last_activity = midi_vw_get_time();
            
            if (midi_vw_system.callbacks.message_callback) {
                midi_vw_system.callbacks.message_callback(port->device.device_id, &message);
            }

            midi_vw_route_message(port->device.device_id, &message);
        }
    }

    return MIDI_VW_SUCCESS;
}

uint8_t midi_vw_get_device_count(void)
{
    return midi_vw_system.device_count;
}

uint8_t midi_vw_get_connection_count(void)
{
    return midi_vw_system.connection_count;
}

midi_vw_status_t midi_vw_list_devices(uint8_t *device_ids, uint8_t max_devices, uint8_t *count)
{
    if (!midi_vw_system.initialized || !device_ids || !count) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    *count = 0;
    for (uint8_t i = 0; i < midi_vw_system.device_count && *count < max_devices; i++) {
        device_ids[*count] = midi_vw_system.ports[i].device.device_id;
        (*count)++;
    }

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_list_connections(uint8_t *connection_ids, uint8_t max_connections, uint8_t *count)
{
    if (!midi_vw_system.initialized || !connection_ids || !count) {
        return MIDI_VW_ERROR_INVALID_PARAM;
    }

    *count = 0;
    for (uint8_t i = 0; i < midi_vw_system.connection_count && *count < max_connections; i++) {
        connection_ids[*count] = midi_vw_system.connections[i].connection_id;
        (*count)++;
    }

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_get_statistics(uint32_t *total_messages, uint32_t *total_errors, uint32_t *total_filtered)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    if (total_messages) *total_messages = midi_vw_system.total_messages;
    if (total_errors) *total_errors = midi_vw_system.total_errors;
    if (total_filtered) *total_filtered = midi_vw_system.total_filtered;

    return MIDI_VW_SUCCESS;
}

midi_vw_status_t midi_vw_reset_statistics(void)
{
    if (!midi_vw_system.initialized) {
        return MIDI_VW_ERROR_NOT_INITIALIZED;
    }

    midi_vw_system.total_messages = 0;
    midi_vw_system.total_errors = 0;
    midi_vw_system.total_filtered = 0;

    for (uint8_t i = 0; i < midi_vw_system.device_count; i++) {
        midi_vw_system.ports[i].device.messages_received = 0;
        midi_vw_system.ports[i].device.messages_sent = 0;
        midi_vw_system.ports[i].device.errors = 0;
        midi_vw_system.ports[i].rx_buffer.overruns = 0;
        midi_vw_system.ports[i].tx_buffer.overruns = 0;
    }

    for (uint8_t i = 0; i < midi_vw_system.connection_count; i++) {
        midi_vw_system.connections[i].messages_routed = 0;
        midi_vw_system.connections[i].messages_filtered = 0;
    }

    return MIDI_VW_SUCCESS;
}

static midi_vw_status_t midi_vw_buffer_put(midi_vw_message_buffer_t *buffer, midi_message_t *message)
{
    if (midi_vw_buffer_is_full(buffer)) {
        buffer->overruns++;
        return MIDI_VW_ERROR_BUFFER_FULL;
    }

    buffer->messages[buffer->head] = *message;
    buffer->head = (buffer->head + 1) % MIDI_VW_MESSAGE_BUFFER_SIZE;
    buffer->count++;
    
    return MIDI_VW_SUCCESS;
}

static midi_vw_status_t midi_vw_buffer_get(midi_vw_message_buffer_t *buffer, midi_message_t *message)
{
    if (midi_vw_buffer_is_empty(buffer)) {
        return MIDI_VW_ERROR_NO_DATA;
    }

    *message = buffer->messages[buffer->tail];
    buffer->tail = (buffer->tail + 1) % MIDI_VW_MESSAGE_BUFFER_SIZE;
    buffer->count--;
    
    return MIDI_VW_SUCCESS;
}

static bool midi_vw_buffer_is_empty(midi_vw_message_buffer_t *buffer)
{
    return buffer->count == 0;
}

static bool midi_vw_buffer_is_full(midi_vw_message_buffer_t *buffer)
{
    return buffer->count >= MIDI_VW_MESSAGE_BUFFER_SIZE;
}

static uint8_t midi_vw_find_device(uint8_t device_id)
{
    for (uint8_t i = 0; i < midi_vw_system.device_count; i++) {
        if (midi_vw_system.ports[i].device.device_id == device_id) {
            return i;
        }
    }
    return MIDI_VW_MAX_DEVICES;
}

static uint8_t midi_vw_find_connection(uint8_t connection_id)
{
    for (uint8_t i = 0; i < midi_vw_system.connection_count; i++) {
        if (midi_vw_system.connections[i].connection_id == connection_id) {
            return i;
        }
    }
    return MIDI_VW_MAX_CONNECTIONS;
}

static bool midi_vw_should_filter_message(midi_vw_connection_t *connection, midi_message_t *message)
{
    if (connection->filter == MIDI_VW_FILTER_NONE) {
        return false;
    }

    uint8_t message_type = message->status & 0xF0;
    uint8_t channel = message->status & 0x0F;

    if (connection->source_channel != 0xFF && channel != connection->source_channel) {
        return true;
    }

    switch (message_type) {
        case MIDI_MSG_NOTE_OFF:
        case MIDI_MSG_NOTE_ON:
            return (connection->filter & MIDI_VW_FILTER_NOTE) != 0;
        case MIDI_MSG_CONTROL_CHANGE:
            return (connection->filter & MIDI_VW_FILTER_CONTROL) != 0;
        case MIDI_MSG_PROGRAM_CHANGE:
            return (connection->filter & MIDI_VW_FILTER_PROGRAM) != 0;
        case MIDI_MSG_PITCH_BEND:
            return (connection->filter & MIDI_VW_FILTER_PITCH_BEND) != 0;
        case MIDI_MSG_SYSTEM_EXCLUSIVE:
            return (connection->filter & MIDI_VW_FILTER_SYSEX) != 0;
        default:
            if (message->status >= 0xF8) {
                return (connection->filter & MIDI_VW_FILTER_REALTIME) != 0;
            }
            return false;
    }
}

static void midi_vw_route_message(uint8_t source_device_id, midi_message_t *message)
{
    midi_vw_system.total_messages++;

    for (uint8_t i = 0; i < midi_vw_system.connection_count; i++) {
        midi_vw_connection_t *connection = &midi_vw_system.connections[i];
        
        if (!connection->enabled || connection->source_device_id != source_device_id) {
            continue;
        }

        if (midi_vw_should_filter_message(connection, message)) {
            connection->messages_filtered++;
            midi_vw_system.total_filtered++;
            continue;
        }

        if (midi_vw_system.callbacks.filter_callback) {
            if (!midi_vw_system.callbacks.filter_callback(source_device_id, 
                                                         connection->dest_device_id, message)) {
                connection->messages_filtered++;
                midi_vw_system.total_filtered++;
                continue;
            }
        }

        uint8_t dest_slot = midi_vw_find_device(connection->dest_device_id);
        if (dest_slot >= MIDI_VW_MAX_DEVICES) {
            midi_vw_system.total_errors++;
            continue;
        }

        midi_vw_port_t *dest_port = &midi_vw_system.ports[dest_slot];
        if (!dest_port->active || !dest_port->device.is_output) {
            midi_vw_system.total_errors++;
            continue;
        }

        midi_message_t routed_message = *message;
        
        if (connection->dest_channel != 0xFF && 
            (routed_message.status & 0xF0) != 0xF0) {
            routed_message.status = (routed_message.status & 0xF0) | (connection->dest_channel & 0x0F);
        }

        if (midi_vw_send_message(connection->dest_device_id, &routed_message) == MIDI_VW_SUCCESS) {
            connection->messages_routed++;
        } else {
            midi_vw_system.total_errors++;
        }
    }
}

static uint32_t midi_vw_get_time(void)
{
    return midi_vw_system.system_time;
}