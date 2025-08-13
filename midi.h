#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>
#include <stdbool.h>

#define MIDI_MAX_DATA_SIZE 3
#define MIDI_BUFFER_SIZE 64

typedef enum {
    MIDI_SUCCESS = 0,
    MIDI_ERROR_INVALID_PARAM,
    MIDI_ERROR_NOT_INITIALIZED,
    MIDI_ERROR_BUFFER_FULL,
    MIDI_ERROR_NO_DATA,
    MIDI_ERROR_USB_ERROR
} midi_status_t;

typedef enum {
    MIDI_MSG_NOTE_OFF = 0x80,
    MIDI_MSG_NOTE_ON = 0x90,
    MIDI_MSG_POLY_PRESSURE = 0xA0,
    MIDI_MSG_CONTROL_CHANGE = 0xB0,
    MIDI_MSG_PROGRAM_CHANGE = 0xC0,
    MIDI_MSG_CHANNEL_PRESSURE = 0xD0,
    MIDI_MSG_PITCH_BEND = 0xE0,
    MIDI_MSG_SYSTEM_EXCLUSIVE = 0xF0,
    MIDI_MSG_TIME_CODE = 0xF1,
    MIDI_MSG_SONG_POSITION = 0xF2,
    MIDI_MSG_SONG_SELECT = 0xF3,
    MIDI_MSG_TUNE_REQUEST = 0xF6,
    MIDI_MSG_END_SYSEX = 0xF7,
    MIDI_MSG_TIMING_CLOCK = 0xF8,
    MIDI_MSG_START = 0xFA,
    MIDI_MSG_CONTINUE = 0xFB,
    MIDI_MSG_STOP = 0xFC,
    MIDI_MSG_ACTIVE_SENSING = 0xFE,
    MIDI_MSG_SYSTEM_RESET = 0xFF
} midi_message_type_t;

typedef struct {
    uint8_t status;
    uint8_t data[MIDI_MAX_DATA_SIZE];
    uint8_t length;
    uint32_t timestamp;
} midi_message_t;

typedef struct {
    uint8_t code_index;
    uint8_t cable_number;
    uint8_t midi_data[3];
} usb_midi_event_t;

typedef void (*midi_note_on_callback_t)(uint8_t channel, uint8_t note, uint8_t velocity);
typedef void (*midi_note_off_callback_t)(uint8_t channel, uint8_t note, uint8_t velocity);
typedef void (*midi_control_change_callback_t)(uint8_t channel, uint8_t controller, uint8_t value);
typedef void (*midi_program_change_callback_t)(uint8_t channel, uint8_t program);
typedef void (*midi_pitch_bend_callback_t)(uint8_t channel, uint16_t bend);
typedef void (*midi_sysex_callback_t)(uint8_t *data, uint16_t length);

typedef struct {
    midi_note_on_callback_t note_on_callback;
    midi_note_off_callback_t note_off_callback;
    midi_control_change_callback_t control_change_callback;
    midi_program_change_callback_t program_change_callback;
    midi_pitch_bend_callback_t pitch_bend_callback;
    midi_sysex_callback_t sysex_callback;
} midi_callbacks_t;

midi_status_t midi_init(midi_callbacks_t *callbacks);
midi_status_t midi_deinit(void);
midi_status_t midi_start(void);
midi_status_t midi_stop(void);

midi_status_t midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
midi_status_t midi_send_note_off(uint8_t channel, uint8_t note, uint8_t velocity);
midi_status_t midi_send_control_change(uint8_t channel, uint8_t controller, uint8_t value);
midi_status_t midi_send_program_change(uint8_t channel, uint8_t program);
midi_status_t midi_send_pitch_bend(uint8_t channel, uint16_t bend);
midi_status_t midi_send_sysex(uint8_t *data, uint16_t length);

midi_status_t midi_send_message(midi_message_t *message);
midi_status_t midi_receive_message(midi_message_t *message);

bool midi_has_pending_messages(void);
uint16_t midi_get_pending_count(void);

#endif