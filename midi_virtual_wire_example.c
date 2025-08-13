#include "midi_virtual_wire.h"
#include "midi.h"
#include <stdio.h>

static uint8_t piano_device_id = 0;
static uint8_t synth_device_id = 0;
static uint8_t drums_device_id = 0;
static uint8_t sequencer_device_id = 0;

static void device_state_changed(uint8_t device_id, midi_vw_device_state_t state)
{
    midi_vw_device_t device_info;
    if (midi_vw_get_device_info(device_id, &device_info) == MIDI_VW_SUCCESS) {
        printf("Device '%s' (ID: %d) state changed to: ", device_info.name, device_id);
        switch (state) {
            case MIDI_VW_DEVICE_STATE_DISCONNECTED:
                printf("DISCONNECTED\n");
                break;
            case MIDI_VW_DEVICE_STATE_CONNECTED:
                printf("CONNECTED\n");
                break;
            case MIDI_VW_DEVICE_STATE_ACTIVE:
                printf("ACTIVE\n");
                break;
            case MIDI_VW_DEVICE_STATE_ERROR:
                printf("ERROR\n");
                break;
        }
    }
}

static void message_received(uint8_t device_id, midi_message_t *message)
{
    midi_vw_device_t device_info;
    if (midi_vw_get_device_info(device_id, &device_info) == MIDI_VW_SUCCESS) {
        printf("Message from '%s': Status=0x%02X", device_info.name, message->status);
        
        uint8_t message_type = message->status & 0xF0;
        uint8_t channel = message->status & 0x0F;
        
        switch (message_type) {
            case MIDI_MSG_NOTE_ON:
                printf(" Note ON Ch%d Note:%d Vel:%d\n", channel, message->data[0], message->data[1]);
                break;
            case MIDI_MSG_NOTE_OFF:
                printf(" Note OFF Ch%d Note:%d Vel:%d\n", channel, message->data[0], message->data[1]);
                break;
            case MIDI_MSG_CONTROL_CHANGE:
                printf(" CC Ch%d Controller:%d Value:%d\n", channel, message->data[0], message->data[1]);
                break;
            case MIDI_MSG_PROGRAM_CHANGE:
                printf(" PC Ch%d Program:%d\n", channel, message->data[0]);
                break;
            default:
                printf(" Length:%d\n", message->length);
                break;
        }
    }
}

static bool custom_filter(uint8_t source_device_id, uint8_t dest_device_id, midi_message_t *message)
{
    if (source_device_id == drums_device_id && dest_device_id == synth_device_id) {
        uint8_t message_type = message->status & 0xF0;
        if (message_type == MIDI_MSG_NOTE_ON || message_type == MIDI_MSG_NOTE_OFF) {
            if (message->data[0] >= 36 && message->data[0] <= 51) {
                return false;
            }
        }
    }
    
    return true;
}

static void setup_virtual_wire_network(void)
{
    printf("Setting up virtual wire MIDI network...\n");

    midi_vw_register_device("Piano Controller", true, false, &piano_device_id);
    midi_vw_register_device("Synthesizer", false, true, &synth_device_id);
    midi_vw_register_device("Drum Machine", true, true, &drums_device_id);
    midi_vw_register_device("Sequencer", true, true, &sequencer_device_id);

    printf("Registered devices:\n");
    printf("  Piano Controller (ID: %d) - Input only\n", piano_device_id);
    printf("  Synthesizer (ID: %d) - Output only\n", synth_device_id);
    printf("  Drum Machine (ID: %d) - Input/Output\n", drums_device_id);
    printf("  Sequencer (ID: %d) - Input/Output\n", sequencer_device_id);

    uint8_t connection_id;
    
    midi_vw_create_connection(piano_device_id, synth_device_id, 0xFF, 0xFF, 
                             MIDI_VW_FILTER_NONE, &connection_id);
    printf("Connected Piano -> Synthesizer (Connection ID: %d)\n", connection_id);
    
    midi_vw_create_connection(piano_device_id, sequencer_device_id, 0xFF, 0xFF, 
                             MIDI_VW_FILTER_NONE, &connection_id);
    printf("Connected Piano -> Sequencer (Connection ID: %d)\n", connection_id);
    
    midi_vw_create_connection(sequencer_device_id, synth_device_id, 0xFF, 0xFF, 
                             MIDI_VW_FILTER_NONE, &connection_id);
    printf("Connected Sequencer -> Synthesizer (Connection ID: %d)\n", connection_id);
    
    midi_vw_create_connection(sequencer_device_id, drums_device_id, 9, 9, 
                             MIDI_VW_FILTER_NONE, &connection_id);
    printf("Connected Sequencer Ch9 -> Drums Ch9 (Connection ID: %d)\n", connection_id);
    
    midi_vw_create_connection(drums_device_id, synth_device_id, 0xFF, 0xFF, 
                             MIDI_VW_FILTER_NONE, &connection_id);
    printf("Connected Drums -> Synthesizer (Connection ID: %d)\n", connection_id);

    printf("Virtual wire network setup complete!\n\n");
}

static void simulate_piano_input(void)
{
    static uint8_t note_sequence[] = {60, 62, 64, 65, 67, 69, 71, 72};
    static uint8_t sequence_index = 0;
    static uint32_t last_note_time = 0;
    static uint32_t current_time = 0;
    
    current_time++;
    
    if (current_time - last_note_time >= 1000) {
        midi_message_t message;
        
        message.status = MIDI_MSG_NOTE_ON;
        message.data[0] = note_sequence[sequence_index];
        message.data[1] = 100;
        message.length = 3;
        message.timestamp = current_time;
        
        printf("Piano playing note %d\n", note_sequence[sequence_index]);
        midi_vw_inject_message(piano_device_id, &message);
        
        sequence_index = (sequence_index + 1) % (sizeof(note_sequence) / sizeof(note_sequence[0]));
        last_note_time = current_time;
    }
}

static void simulate_sequencer_patterns(void)
{
    static uint32_t beat_counter = 0;
    static uint32_t current_time = 0;
    
    current_time++;
    beat_counter++;
    
    if (beat_counter % 500 == 0) {
        midi_message_t message;
        
        message.status = MIDI_MSG_NOTE_ON | 9;
        message.data[0] = 36;
        message.data[1] = 127;
        message.length = 3;
        message.timestamp = current_time;
        
        printf("Sequencer: Kick drum\n");
        midi_vw_inject_message(sequencer_device_id, &message);
    }
    
    if (beat_counter % 250 == 125) {
        midi_message_t message;
        
        message.status = MIDI_MSG_NOTE_ON | 9;
        message.data[0] = 38;
        message.data[1] = 100;
        message.length = 3;
        message.timestamp = current_time;
        
        printf("Sequencer: Snare drum\n");
        midi_vw_inject_message(sequencer_device_id, &message);
    }
}

static void print_network_status(void)
{
    printf("\n=== MIDI Virtual Wire Network Status ===\n");
    
    uint8_t device_count = midi_vw_get_device_count();
    uint8_t connection_count = midi_vw_get_connection_count();
    
    printf("Devices: %d, Connections: %d\n", device_count, connection_count);
    
    uint8_t device_ids[MIDI_VW_MAX_DEVICES];
    uint8_t count;
    
    if (midi_vw_list_devices(device_ids, MIDI_VW_MAX_DEVICES, &count) == MIDI_VW_SUCCESS) {
        for (uint8_t i = 0; i < count; i++) {
            midi_vw_device_t device_info;
            if (midi_vw_get_device_info(device_ids[i], &device_info) == MIDI_VW_SUCCESS) {
                printf("Device %d: '%s' - RX:%u TX:%u Errors:%u\n",
                       device_info.device_id, device_info.name,
                       device_info.messages_received, device_info.messages_sent,
                       device_info.errors);
            }
        }
    }
    
    uint32_t total_messages, total_errors, total_filtered;
    if (midi_vw_get_statistics(&total_messages, &total_errors, &total_filtered) == MIDI_VW_SUCCESS) {
        printf("Total: Messages:%u Errors:%u Filtered:%u\n",
               total_messages, total_errors, total_filtered);
    }
    
    printf("==========================================\n\n");
}

static void demonstrate_connection_management(void)
{
    printf("=== Demonstrating Connection Management ===\n");
    
    uint8_t connection_ids[MIDI_VW_MAX_CONNECTIONS];
    uint8_t count;
    
    if (midi_vw_list_connections(connection_ids, MIDI_VW_MAX_CONNECTIONS, &count) == MIDI_VW_SUCCESS) {
        printf("Active connections:\n");
        for (uint8_t i = 0; i < count; i++) {
            midi_vw_connection_t connection_info;
            if (midi_vw_get_connection_info(connection_ids[i], &connection_info) == MIDI_VW_SUCCESS) {
                printf("  Connection %d: Device %d -> Device %d (Ch %d->%d) Routed:%u Filtered:%u\n",
                       connection_info.connection_id,
                       connection_info.source_device_id, connection_info.dest_device_id,
                       connection_info.source_channel, connection_info.dest_channel,
                       connection_info.messages_routed, connection_info.messages_filtered);
            }
        }
    }
    
    printf("Temporarily disabling Piano -> Synthesizer connection...\n");
    if (count > 0) {
        midi_vw_enable_connection(connection_ids[0], false);
    }
    
    for (int i = 0; i < 5; i++) {
        simulate_piano_input();
        midi_vw_process_messages();
    }
    
    printf("Re-enabling connection...\n");
    if (count > 0) {
        midi_vw_enable_connection(connection_ids[0], true);
    }
    
    printf("===========================================\n\n");
}

int midi_virtual_wire_example_init(void)
{
    printf("Initializing MIDI Virtual Wire Example...\n");

    midi_vw_callbacks_t callbacks = {
        .device_callback = device_state_changed,
        .message_callback = message_received,
        .filter_callback = custom_filter
    };

    midi_vw_status_t status = midi_vw_init(&callbacks);
    if (status != MIDI_VW_SUCCESS) {
        printf("Virtual wire initialization failed: %d\n", status);
        return -1;
    }

    status = midi_vw_start();
    if (status != MIDI_VW_SUCCESS) {
        printf("Virtual wire start failed: %d\n", status);
        midi_vw_deinit();
        return -1;
    }

    setup_virtual_wire_network();
    
    printf("MIDI Virtual Wire system initialized successfully!\n\n");
    return 0;
}

void midi_virtual_wire_example_run(void)
{
    static uint32_t loop_counter = 0;
    
    simulate_piano_input();
    simulate_sequencer_patterns();
    
    midi_vw_process_messages();
    
    loop_counter++;
    if (loop_counter % 5000 == 0) {
        print_network_status();
    }
    
    if (loop_counter == 10000) {
        demonstrate_connection_management();
    }
}

void midi_virtual_wire_example_test_all_to_all(void)
{
    printf("=== Testing All-to-All Connections ===\n");
    
    printf("Disconnecting all current connections...\n");
    midi_vw_disconnect_all();
    
    printf("Creating all-to-all connections...\n");
    midi_vw_connect_all_to_all();
    
    printf("Injecting test messages...\n");
    midi_message_t test_message;
    test_message.status = MIDI_MSG_NOTE_ON;
    test_message.data[0] = 60;
    test_message.data[1] = 100;
    test_message.length = 3;
    test_message.timestamp = 0;
    
    midi_vw_inject_message(piano_device_id, &test_message);
    midi_vw_process_messages();
    
    print_network_status();
    
    printf("Restoring original network configuration...\n");
    midi_vw_disconnect_all();
    setup_virtual_wire_network();
    
    printf("====================================\n\n");
}

void midi_virtual_wire_example_deinit(void)
{
    printf("Shutting down MIDI Virtual Wire system...\n");
    print_network_status();
    midi_vw_deinit();
    printf("MIDI Virtual Wire system shut down.\n");
}