#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "midi.h"
#include "midi_virtual_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_USB_MIDI_DEVICES CONFIG_MAX_USB_MIDI_DEVICES
#define USB_SCAN_INTERVAL_MS CONFIG_USB_SCAN_INTERVAL_MS
#define MAIN_LOOP_DELAY_MS CONFIG_MAIN_LOOP_DELAY_MS

typedef struct {
    uint8_t usb_device_id;
    uint8_t vw_device_id;
    char device_name[64];
    bool is_connected;
    bool is_midi_device;
    uint16_t vendor_id;
    uint16_t product_id;
    midi_callbacks_t midi_callbacks;
} usb_midi_device_t;

static struct {
    bool running;
    bool initialized;
    usb_midi_device_t devices[MAX_USB_MIDI_DEVICES];
    uint8_t device_count;
    uint32_t scan_counter;
    uint32_t loop_counter;
} main_app;

static volatile bool shutdown_requested = false;

static void signal_handler(int signal);
static int initialize_system(void);
static void cleanup_system(void);
static void scan_for_usb_devices(void);
static void handle_usb_device_connected(uint8_t usb_device_id, uint16_t vid, uint16_t pid);
static void handle_usb_device_disconnected(uint8_t usb_device_id);
static bool is_midi_device(uint16_t vendor_id, uint16_t product_id);
static void get_device_name(uint16_t vendor_id, uint16_t product_id, char *name);
static void midi_note_on_handler(uint8_t channel, uint8_t note, uint8_t velocity);
static void midi_note_off_handler(uint8_t channel, uint8_t note, uint8_t velocity);
static void midi_control_change_handler(uint8_t channel, uint8_t controller, uint8_t value);
static void midi_program_change_handler(uint8_t channel, uint8_t program);
static void midi_pitch_bend_handler(uint8_t channel, uint16_t bend);
static void midi_sysex_handler(uint8_t *data, uint16_t length);
static void vw_device_state_callback(uint8_t device_id, midi_vw_device_state_t state);
static void vw_message_callback(uint8_t device_id, midi_message_t *message);
static bool vw_filter_callback(uint8_t source_device_id, uint8_t dest_device_id, midi_message_t *message);
static void process_midi_messages(void);
static void print_status(void);
static usb_midi_device_t* find_device_by_usb_id(uint8_t usb_device_id);
static usb_midi_device_t* find_device_by_vw_id(uint8_t vw_device_id);

int main(void)
{
    printf("%s v%s Starting...\n", MIDI_HUB_NAME, MIDI_HUB_VERSION);
    printf("=====================================\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (initialize_system() != 0) {
        printf("Failed to initialize system\n");
        return EXIT_FAILURE;
    }

    printf("System initialized successfully\n");
    printf("Scanning for USB MIDI devices...\n");
    printf("Press Ctrl+C to exit\n\n");

    main_app.running = true;

    while (main_app.running && !shutdown_requested) {
        main_app.loop_counter++;

        if (main_app.loop_counter % (USB_SCAN_INTERVAL_MS / MAIN_LOOP_DELAY_MS) == 0) {
            scan_for_usb_devices();
        }

        process_midi_messages();
        midi_vw_process_messages();

        if (main_app.loop_counter % (5000 / MAIN_LOOP_DELAY_MS) == 0) {
            print_status();
        }

        usleep(MAIN_LOOP_DELAY_MS * 1000);
    }

    printf("\nShutdown requested, cleaning up...\n");
    cleanup_system();
    printf("MIDI Virtual Wire USB Hub stopped.\n");
    
    return EXIT_SUCCESS;
}

static void signal_handler(int signal)
{
    (void)signal;
    printf("\nReceived shutdown signal\n");
    shutdown_requested = true;
}

static int initialize_system(void)
{
    memset(&main_app, 0, sizeof(main_app));

    midi_vw_callbacks_t vw_callbacks = {
        .device_callback = vw_device_state_callback,
        .message_callback = vw_message_callback,
        .filter_callback = vw_filter_callback
    };

    if (midi_vw_init(&vw_callbacks) != MIDI_VW_SUCCESS) {
        printf("Failed to initialize MIDI virtual wire system\n");
        return -1;
    }

    if (midi_vw_start() != MIDI_VW_SUCCESS) {
        printf("Failed to start MIDI virtual wire system\n");
        midi_vw_deinit();
        return -1;
    }

    main_app.initialized = true;
    return 0;
}

static void cleanup_system(void)
{
    if (!main_app.initialized) {
        return;
    }

    main_app.running = false;

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected) {
            handle_usb_device_disconnected(main_app.devices[i].usb_device_id);
        }
    }

    midi_vw_deinit();
    main_app.initialized = false;
}

static void scan_for_usb_devices(void)
{
    static uint8_t simulated_devices = 0;
    
    if (simulated_devices < 3) {
        static uint16_t sim_vids[] = {0x1234, 0x5678, 0x9ABC};
        static uint16_t sim_pids[] = {0x0001, 0x0002, 0x0003};
        static char* sim_names[] = {"USB Piano", "USB Synth", "USB Drums"};
        
        INFO_PRINTF("Detected USB device: %s (VID:0x%04X PID:0x%04X)\n", 
               sim_names[simulated_devices], sim_vids[simulated_devices], sim_pids[simulated_devices]);
        
        handle_usb_device_connected(simulated_devices + 1, sim_vids[simulated_devices], sim_pids[simulated_devices]);
        simulated_devices++;
    }
}

static void handle_usb_device_connected(uint8_t usb_device_id, uint16_t vid, uint16_t pid)
{
    if (main_app.device_count >= MAX_USB_MIDI_DEVICES) {
        printf("Maximum number of USB devices reached\n");
        return;
    }

    usb_midi_device_t *device = &main_app.devices[main_app.device_count];
    memset(device, 0, sizeof(usb_midi_device_t));

    device->usb_device_id = usb_device_id;
    device->vendor_id = vid;
    device->product_id = pid;
    device->is_midi_device = is_midi_device(vid, pid);
    device->is_connected = true;

    get_device_name(vid, pid, device->device_name);

    if (device->is_midi_device) {
        device->midi_callbacks.note_on_callback = midi_note_on_handler;
        device->midi_callbacks.note_off_callback = midi_note_off_handler;
        device->midi_callbacks.control_change_callback = midi_control_change_handler;
        device->midi_callbacks.program_change_callback = midi_program_change_handler;
        device->midi_callbacks.pitch_bend_callback = midi_pitch_bend_handler;
        device->midi_callbacks.sysex_callback = midi_sysex_handler;

        if (midi_init(&device->midi_callbacks) == MIDI_SUCCESS) {
            if (midi_start() == MIDI_SUCCESS) {
                if (midi_vw_register_device(device->device_name, true, true, &device->vw_device_id) == MIDI_VW_SUCCESS) {
                    printf("✓ MIDI device '%s' connected and registered (VW ID: %d)\n", 
                           device->device_name, device->vw_device_id);
                    
                    for (uint8_t i = 0; i < main_app.device_count; i++) {
                        if (i != main_app.device_count && main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
                            uint8_t connection_id;
                            midi_vw_create_connection(device->vw_device_id, main_app.devices[i].vw_device_id, 
                                                    0xFF, 0xFF, MIDI_VW_FILTER_NONE, &connection_id);
                            midi_vw_create_connection(main_app.devices[i].vw_device_id, device->vw_device_id, 
                                                    0xFF, 0xFF, MIDI_VW_FILTER_NONE, &connection_id);
                            printf("  ↔ Created bidirectional connection with '%s'\n", main_app.devices[i].device_name);
                        }
                    }
                    
                    main_app.device_count++;
                } else {
                    printf("✗ Failed to register MIDI device in virtual wire system\n");
                    midi_stop();
                    midi_deinit();
                }
            } else {
                printf("✗ Failed to start MIDI device\n");
                midi_deinit();
            }
        } else {
            printf("✗ Failed to initialize MIDI device\n");
        }
    } else {
        printf("- Non-MIDI USB device '%s' detected (not connecting to virtual wire)\n", device->device_name);
        main_app.device_count++;
    }
}

static void handle_usb_device_disconnected(uint8_t usb_device_id)
{
    usb_midi_device_t *device = find_device_by_usb_id(usb_device_id);
    if (!device || !device->is_connected) {
        return;
    }

    printf("✗ USB device '%s' disconnected\n", device->device_name);

    if (device->is_midi_device) {
        midi_vw_unregister_device(device->vw_device_id);
        midi_stop();
        midi_deinit();
    }

    device->is_connected = false;

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (&main_app.devices[i] == device) {
            for (uint8_t j = i; j < main_app.device_count - 1; j++) {
                main_app.devices[j] = main_app.devices[j + 1];
            }
            main_app.device_count--;
            break;
        }
    }
}

static bool is_midi_device(uint16_t vendor_id, uint16_t product_id)
{
    static struct {
        uint16_t vid;
        uint16_t pid;
    } midi_devices[] = {
        {0x1234, 0x0001},
        {0x5678, 0x0002}, 
        {0x9ABC, 0x0003},
        {0x0499, 0x1000},
        {0x0582, 0x0000},
        {0x06F8, 0x0000}
    };

    for (size_t i = 0; i < sizeof(midi_devices) / sizeof(midi_devices[0]); i++) {
        if ((midi_devices[i].vid == vendor_id) && 
            (midi_devices[i].pid == 0x0000 || midi_devices[i].pid == product_id)) {
            return true;
        }
    }

    return false;
}

static void get_device_name(uint16_t vendor_id, uint16_t product_id, char *name)
{
    switch (vendor_id) {
        case 0x1234:
            switch (product_id) {
                case 0x0001: strcpy(name, "USB MIDI Piano"); break;
                default: sprintf(name, "Device 0x%04X:0x%04X", vendor_id, product_id); break;
            }
            break;
        case 0x5678:
            switch (product_id) {
                case 0x0002: strcpy(name, "USB MIDI Synthesizer"); break;
                default: sprintf(name, "Device 0x%04X:0x%04X", vendor_id, product_id); break;
            }
            break;
        case 0x9ABC:
            switch (product_id) {
                case 0x0003: strcpy(name, "USB MIDI Drum Machine"); break;
                default: sprintf(name, "Device 0x%04X:0x%04X", vendor_id, product_id); break;
            }
            break;
        case 0x0499:
            strcpy(name, "Yamaha MIDI Device");
            break;
        case 0x0582:
            strcpy(name, "Roland MIDI Device");
            break;
        case 0x06F8:
            strcpy(name, "Hercules MIDI Device");
            break;
        default:
            sprintf(name, "Unknown Device 0x%04X:0x%04X", vendor_id, product_id);
            break;
    }
}

static void midi_note_on_handler(uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_message_t message = {
        .status = MIDI_MSG_NOTE_ON | channel,
        .data = {note, velocity, 0},
        .length = 3,
        .timestamp = 0
    };

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
            midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            break;
        }
    }
}

static void midi_note_off_handler(uint8_t channel, uint8_t note, uint8_t velocity)
{
    midi_message_t message = {
        .status = MIDI_MSG_NOTE_OFF | channel,
        .data = {note, velocity, 0},
        .length = 3,
        .timestamp = 0
    };

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
            midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            break;
        }
    }
}

static void midi_control_change_handler(uint8_t channel, uint8_t controller, uint8_t value)
{
    midi_message_t message = {
        .status = MIDI_MSG_CONTROL_CHANGE | channel,
        .data = {controller, value, 0},
        .length = 3,
        .timestamp = 0
    };

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
            midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            break;
        }
    }
}

static void midi_program_change_handler(uint8_t channel, uint8_t program)
{
    midi_message_t message = {
        .status = MIDI_MSG_PROGRAM_CHANGE | channel,
        .data = {program, 0, 0},
        .length = 2,
        .timestamp = 0
    };

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
            midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            break;
        }
    }
}

static void midi_pitch_bend_handler(uint8_t channel, uint16_t bend)
{
    midi_message_t message = {
        .status = MIDI_MSG_PITCH_BEND | channel,
        .data = {bend & 0x7F, (bend >> 7) & 0x7F, 0},
        .length = 3,
        .timestamp = 0
    };

    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_connected && main_app.devices[i].is_midi_device) {
            midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            break;
        }
    }
}

static void midi_sysex_handler(uint8_t *data, uint16_t length)
{
    printf("SysEx received: %d bytes\n", length);
}

static void vw_device_state_callback(uint8_t device_id, midi_vw_device_state_t state)
{
    usb_midi_device_t *device = find_device_by_vw_id(device_id);
    const char *state_name = "UNKNOWN";
    
    switch (state) {
        case MIDI_VW_DEVICE_STATE_DISCONNECTED: state_name = "DISCONNECTED"; break;
        case MIDI_VW_DEVICE_STATE_CONNECTED: state_name = "CONNECTED"; break;
        case MIDI_VW_DEVICE_STATE_ACTIVE: state_name = "ACTIVE"; break;
        case MIDI_VW_DEVICE_STATE_ERROR: state_name = "ERROR"; break;
    }
    
    if (device) {
        printf("VW Device '%s' state: %s\n", device->device_name, state_name);
    } else {
        printf("VW Device %d state: %s\n", device_id, state_name);
    }
}

static void vw_message_callback(uint8_t device_id, midi_message_t *message)
{
    usb_midi_device_t *device = find_device_by_vw_id(device_id);
    if (device) {
        uint8_t msg_type = message->status & 0xF0;
        uint8_t channel = message->status & 0x0F;
        
        switch (msg_type) {
            case MIDI_MSG_NOTE_ON:
                printf("♪ %s: Note ON Ch%d Note:%d Vel:%d\n", 
                       device->device_name, channel, message->data[0], message->data[1]);
                break;
            case MIDI_MSG_NOTE_OFF:
                printf("♫ %s: Note OFF Ch%d Note:%d\n", 
                       device->device_name, channel, message->data[0]);
                break;
            case MIDI_MSG_CONTROL_CHANGE:
                printf("🎛 %s: CC Ch%d Ctrl:%d Val:%d\n", 
                       device->device_name, channel, message->data[0], message->data[1]);
                break;
        }
    }
}

static bool vw_filter_callback(uint8_t source_device_id, uint8_t dest_device_id, midi_message_t *message)
{
    (void)source_device_id;
    (void)dest_device_id;
    (void)message;
    return true;
}

static void process_midi_messages(void)
{
    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (!main_app.devices[i].is_connected || !main_app.devices[i].is_midi_device) {
            continue;
        }

        while (midi_has_pending_messages()) {
            midi_message_t message;
            if (midi_receive_message(&message) == MIDI_SUCCESS) {
                midi_vw_inject_message(main_app.devices[i].vw_device_id, &message);
            }
        }

        while (midi_vw_has_pending_messages(main_app.devices[i].vw_device_id)) {
            midi_message_t message;
            if (midi_vw_receive_message(main_app.devices[i].vw_device_id, &message) == MIDI_VW_SUCCESS) {
                midi_send_message(&message);
            }
        }
    }
}

static void print_status(void)
{
    static uint32_t last_print = 0;
    uint32_t current_time = main_app.loop_counter / 100;
    
    if (current_time - last_print < 30) {
        return;
    }
    last_print = current_time;

    printf("\n=== MIDI Virtual Wire Hub Status ===\n");
    printf("Connected devices: %d\n", main_app.device_count);
    
    for (uint8_t i = 0; i < main_app.device_count; i++) {
        usb_midi_device_t *device = &main_app.devices[i];
        printf("  %s %s (VID:0x%04X PID:0x%04X)", 
               device->is_midi_device ? "🎵" : "📱", 
               device->device_name, device->vendor_id, device->product_id);
        
        if (device->is_midi_device) {
            midi_vw_device_t vw_info;
            if (midi_vw_get_device_info(device->vw_device_id, &vw_info) == MIDI_VW_SUCCESS) {
                printf(" - RX:%u TX:%u", vw_info.messages_received, vw_info.messages_sent);
            }
        }
        printf("\n");
    }
    
    uint32_t total_messages, total_errors, total_filtered;
    if (midi_vw_get_statistics(&total_messages, &total_errors, &total_filtered) == MIDI_VW_SUCCESS) {
        printf("Total: Messages:%u Errors:%u Filtered:%u\n", 
               total_messages, total_errors, total_filtered);
    }
    
    uint8_t connection_count = midi_vw_get_connection_count();
    printf("Active connections: %d\n", connection_count);
    
    printf("===================================\n\n");
}

static usb_midi_device_t* find_device_by_usb_id(uint8_t usb_device_id)
{
    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].usb_device_id == usb_device_id) {
            return &main_app.devices[i];
        }
    }
    return NULL;
}

static usb_midi_device_t* find_device_by_vw_id(uint8_t vw_device_id)
{
    for (uint8_t i = 0; i < main_app.device_count; i++) {
        if (main_app.devices[i].is_midi_device && 
            main_app.devices[i].vw_device_id == vw_device_id) {
            return &main_app.devices[i];
        }
    }
    return NULL;
}