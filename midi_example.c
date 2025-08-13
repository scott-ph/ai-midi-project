#include "midi.h"
#include <stdio.h>

static void on_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    printf("MIDI Note ON: Channel %d, Note %d, Velocity %d\n", channel, note, velocity);
    
    midi_send_note_off(channel, note, velocity);
}

static void on_note_off(uint8_t channel, uint8_t note, uint8_t velocity)
{
    printf("MIDI Note OFF: Channel %d, Note %d, Velocity %d\n", channel, note, velocity);
}

static void on_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    printf("MIDI Control Change: Channel %d, Controller %d, Value %d\n", channel, controller, value);
    
    if (controller == 7) {
        printf("Volume control received: %d\n", value);
    } else if (controller == 1) {
        printf("Modulation wheel: %d\n", value);
    }
}

static void on_program_change(uint8_t channel, uint8_t program)
{
    printf("MIDI Program Change: Channel %d, Program %d\n", channel, program);
}

static void on_pitch_bend(uint8_t channel, uint16_t bend)
{
    printf("MIDI Pitch Bend: Channel %d, Bend %d\n", channel, bend);
}

static void on_sysex(uint8_t *data, uint16_t length)
{
    printf("MIDI SysEx received, length: %d bytes\n", length);
    printf("Data: ");
    for (uint16_t i = 0; i < length && i < 16; i++) {
        printf("%02X ", data[i]);
    }
    if (length > 16) {
        printf("...");
    }
    printf("\n");
}

static void midi_send_test_sequence(void)
{
    printf("Sending MIDI test sequence...\n");
    
    midi_send_note_on(0, 60, 127);
    
    midi_send_control_change(0, 7, 100);
    
    midi_send_program_change(0, 42);
    
    midi_send_pitch_bend(0, 8192);
    
    uint8_t sysex_data[] = {0x43, 0x12, 0x00, 0x01, 0x02, 0x03};
    midi_send_sysex(sysex_data, sizeof(sysex_data));
    
    midi_send_note_off(0, 60, 0);
}

static void midi_process_pending_messages(void)
{
    midi_message_t message;
    
    while (midi_has_pending_messages()) {
        if (midi_receive_message(&message) == MIDI_SUCCESS) {
            printf("Processing MIDI message: Status=0x%02X, Length=%d\n", 
                   message.status, message.length);
        }
    }
}

int midi_example_init(void)
{
    printf("Initializing MIDI example...\n");

    midi_callbacks_t callbacks = {
        .note_on_callback = on_note_on,
        .note_off_callback = on_note_off,
        .control_change_callback = on_control_change,
        .program_change_callback = on_program_change,
        .pitch_bend_callback = on_pitch_bend,
        .sysex_callback = on_sysex
    };

    midi_status_t status = midi_init(&callbacks);
    if (status != MIDI_SUCCESS) {
        printf("MIDI initialization failed: %d\n", status);
        return -1;
    }

    status = midi_start();
    if (status != MIDI_SUCCESS) {
        printf("MIDI start failed: %d\n", status);
        midi_deinit();
        return -1;
    }

    printf("MIDI device initialized and started\n");
    printf("Waiting for USB enumeration...\n");
    
    return 0;
}

void midi_example_deinit(void)
{
    midi_deinit();
    printf("MIDI device deinitialized\n");
}

void midi_example_run(void)
{
    static uint32_t counter = 0;
    
    midi_process_pending_messages();
    
    counter++;
    if (counter % 10000 == 0) {
        midi_send_test_sequence();
    }
}

void midi_example_note_test(void)
{
    printf("Playing MIDI note sequence...\n");
    
    uint8_t notes[] = {60, 62, 64, 65, 67, 69, 71, 72};
    uint8_t num_notes = sizeof(notes) / sizeof(notes[0]);
    
    for (uint8_t i = 0; i < num_notes; i++) {
        midi_send_note_on(0, notes[i], 127);
        
        for (volatile int delay = 0; delay < 100000; delay++);
        
        midi_send_note_off(0, notes[i], 0);
        
        for (volatile int delay = 0; delay < 50000; delay++);
    }
}

void midi_example_control_test(void)
{
    printf("Testing MIDI control changes...\n");
    
    for (uint8_t value = 0; value <= 127; value += 16) {
        midi_send_control_change(0, 7, value);
        printf("Sent volume control: %d\n", value);
        
        for (volatile int delay = 0; delay < 50000; delay++);
    }
}

void midi_example_sysex_test(void)
{
    printf("Testing MIDI System Exclusive...\n");
    
    uint8_t device_inquiry[] = {0x7E, 0x00, 0x06, 0x01};
    midi_send_sysex(device_inquiry, sizeof(device_inquiry));
    printf("Sent device inquiry SysEx\n");
    
    uint8_t manufacturer_data[] = {0x43, 0x12, 0x00, 0x41, 0x10, 0x32, 0x40};
    midi_send_sysex(manufacturer_data, sizeof(manufacturer_data));
    printf("Sent manufacturer-specific SysEx\n");
}