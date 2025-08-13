#ifndef CONFIG_H
#define CONFIG_H

#define MIDI_HUB_VERSION "1.0.0"
#define MIDI_HUB_NAME "RTOS MIDI Virtual Wire Hub"

#define CONFIG_MAX_USB_MIDI_DEVICES 8
#define CONFIG_USB_SCAN_INTERVAL_MS 1000
#define CONFIG_MAIN_LOOP_DELAY_MS 10
#define CONFIG_STATUS_PRINT_INTERVAL_S 30

#define CONFIG_ENABLE_DEBUG_MESSAGES 1
#define CONFIG_ENABLE_DEVICE_HOTPLUG 1
#define CONFIG_ENABLE_AUTO_CONNECT 1

#define CONFIG_MIDI_BUFFER_SIZE 64
#define CONFIG_VW_MESSAGE_BUFFER_SIZE 128
#define CONFIG_MAX_VW_DEVICES 8
#define CONFIG_MAX_VW_CONNECTIONS 16

#ifdef CONFIG_ENABLE_DEBUG_MESSAGES
#define DEBUG_PRINTF(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) do {} while(0)
#endif

#define INFO_PRINTF(fmt, ...) printf("[INFO] " fmt, ##__VA_ARGS__)
#define ERROR_PRINTF(fmt, ...) printf("[ERROR] " fmt, ##__VA_ARGS__)
#define SUCCESS_PRINTF(fmt, ...) printf("[SUCCESS] " fmt, ##__VA_ARGS__)

#endif