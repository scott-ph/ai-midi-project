#include "usb.h"

#define USB_AUDIO_CLASS 0x01
#define USB_AUDIO_SUBCLASS_AUDIOCONTROL 0x01
#define USB_AUDIO_SUBCLASS_MIDISTREAMING 0x03

#define USB_AUDIO_PROTOCOL_UNDEFINED 0x00
#define USB_AUDIO_PROTOCOL_VERSION_02_00 0x20

#define USB_AUDIO_CS_INTERFACE 0x24
#define USB_AUDIO_CS_ENDPOINT 0x25

#define USB_AUDIO_AC_HEADER 0x01
#define USB_AUDIO_MS_HEADER 0x01
#define USB_AUDIO_MS_MIDI_IN_JACK 0x02
#define USB_AUDIO_MS_MIDI_OUT_JACK 0x03
#define USB_AUDIO_MS_ELEMENT 0x04
#define USB_AUDIO_MS_GENERAL 0x01

#define USB_AUDIO_JACK_TYPE_EMBEDDED 0x01
#define USB_AUDIO_JACK_TYPE_EXTERNAL 0x02

usb_device_descriptor_t midi_device_descriptor = {
    .bLength = sizeof(usb_device_descriptor_t),
    .bDescriptorType = 0x01,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1234,
    .idProduct = 0xABCD,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1
};

static uint8_t midi_config_descriptor_data[] = {
    0x09, 0x02, 0x65, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
    
    0x09, 0x04, 0x00, 0x00, 0x00, USB_AUDIO_CLASS, USB_AUDIO_SUBCLASS_AUDIOCONTROL, 0x00, 0x00,
    
    0x09, USB_AUDIO_CS_INTERFACE, USB_AUDIO_AC_HEADER, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,
    
    0x09, 0x04, 0x01, 0x00, 0x02, USB_AUDIO_CLASS, USB_AUDIO_SUBCLASS_MIDISTREAMING, 0x00, 0x00,
    
    0x07, USB_AUDIO_CS_INTERFACE, USB_AUDIO_MS_HEADER, 0x00, 0x01, 0x41, 0x00,
    
    0x06, USB_AUDIO_CS_INTERFACE, USB_AUDIO_MS_MIDI_IN_JACK, USB_AUDIO_JACK_TYPE_EMBEDDED, 0x01, 0x00,
    
    0x06, USB_AUDIO_CS_INTERFACE, USB_AUDIO_MS_MIDI_IN_JACK, USB_AUDIO_JACK_TYPE_EXTERNAL, 0x02, 0x00,
    
    0x09, USB_AUDIO_CS_INTERFACE, USB_AUDIO_MS_MIDI_OUT_JACK, USB_AUDIO_JACK_TYPE_EMBEDDED, 0x03, 0x01, 0x02, 0x01, 0x00,
    
    0x09, USB_AUDIO_CS_INTERFACE, USB_AUDIO_MS_MIDI_OUT_JACK, USB_AUDIO_JACK_TYPE_EXTERNAL, 0x04, 0x01, 0x01, 0x01, 0x00,
    
    0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    
    0x05, USB_AUDIO_CS_ENDPOINT, USB_AUDIO_MS_GENERAL, 0x01, 0x01,
    
    0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    
    0x05, USB_AUDIO_CS_ENDPOINT, USB_AUDIO_MS_GENERAL, 0x01, 0x03
};

usb_config_descriptor_t *midi_config_descriptor = (usb_config_descriptor_t*)midi_config_descriptor_data;

char* midi_string_descriptors[] = {
    "RTOS MIDI",
    "USB MIDI Device", 
    "000001"
};

#define MIDI_NUM_STRING_DESCRIPTORS 3