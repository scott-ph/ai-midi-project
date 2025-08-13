#include <stdio.h>
#include "midi_virtual_wire.h"

static void device_state_callback(uint8_t device_id, midi_vw_device_state_t state) {
    printf("Device %d state changed to %d\n", device_id, state);
}

static void message_callback(uint8_t device_id, midi_message_t *message) {
    printf("Message from device %d: status=0x%02X\n", device_id, message->status);
}

static bool filter_callback(uint8_t src, uint8_t dest, midi_message_t *msg) {
    (void)src; (void)dest; (void)msg;
    return true;
}

int main(void) {
    printf("MIDI Virtual Wire Test\n");
    
    midi_vw_callbacks_t callbacks = {
        .device_callback = device_state_callback,
        .message_callback = message_callback,
        .filter_callback = filter_callback
    };
    
    if (midi_vw_init(&callbacks) != MIDI_VW_SUCCESS) {
        printf("Failed to init virtual wire\n");
        return 1;
    }
    
    if (midi_vw_start() != MIDI_VW_SUCCESS) {
        printf("Failed to start virtual wire\n");
        return 1;
    }
    
    printf("Virtual wire system started\n");
    
    uint8_t device1_id, device2_id;
    midi_vw_register_device("Test Device 1", true, false, &device1_id);
    midi_vw_register_device("Test Device 2", false, true, &device2_id);
    
    uint8_t conn_id;
    midi_vw_create_connection(device1_id, device2_id, 0xFF, 0xFF, MIDI_VW_FILTER_NONE, &conn_id);
    
    printf("Created connection from device %d to device %d\n", device1_id, device2_id);
    
    midi_message_t msg = {
        .status = 0x90,  // Note on
        .data = {60, 127, 0},
        .length = 3,
        .timestamp = 0
    };
    
    midi_vw_inject_message(device1_id, &msg);
    midi_vw_process_messages();
    
    printf("Test completed successfully\n");
    
    midi_vw_deinit();
    return 0;
}