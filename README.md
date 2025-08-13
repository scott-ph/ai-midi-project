# MIDI Virtual Wire USB Hub

A real-time MIDI routing system for RTOS that automatically detects USB MIDI devices and creates a virtual wire network for routing MIDI messages between all connected devices.

## Features

- **USB MIDI Device Detection**: Automatically detects and connects USB MIDI devices
- **Virtual Wire Routing**: Creates bidirectional connections between all MIDI devices
- **Real-time Processing**: Low-latency MIDI message routing with RTOS scheduling
- **Hot-plug Support**: Devices can be connected/disconnected at runtime
- **Message Filtering**: Configurable filtering by message type and channel
- **Statistics & Monitoring**: Real-time statistics and device status monitoring
- **MIDI Class Compliance**: Full USB MIDI 1.0 class implementation

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  USB MIDI       │    │   Virtual Wire   │    │  USB MIDI       │
│  Device A       │◄──►│   Router Hub     │◄──►│  Device B       │
│  (Piano)        │    │                  │    │  (Synthesizer)  │
└─────────────────┘    │                  │    └─────────────────┘
                       │                  │    
┌─────────────────┐    │                  │    ┌─────────────────┐
│  USB MIDI       │◄──►│                  │◄──►│  USB MIDI       │
│  Device C       │    │                  │    │  Device D       │
│  (Drum Machine) │    │                  │    │  (Sequencer)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

## Building

```bash
# Build the application
make

# Build and run
make run

# Install system-wide
make install
```

## Usage

### Basic Usage

```bash
# Start the MIDI hub
./midi_hub

# The system will automatically:
# 1. Initialize the virtual wire system
# 2. Scan for USB MIDI devices
# 3. Connect detected devices to the virtual wire network
# 4. Route MIDI messages between all devices
```

### Example Output

```
MIDI Virtual Wire USB Hub Starting...
=====================================
System initialized successfully
Scanning for USB MIDI devices...
Press Ctrl+C to exit

Detected USB device: USB Piano (VID:0x1234 PID:0x0001)
✓ MIDI device 'USB MIDI Piano' connected and registered (VW ID: 1)

Detected USB device: USB Synth (VID:0x5678 PID:0x0002)
✓ MIDI device 'USB MIDI Synthesizer' connected and registered (VW ID: 2)
  ↔ Created bidirectional connection with 'USB MIDI Piano'

♪ USB MIDI Piano: Note ON Ch0 Note:60 Vel:127
♫ USB MIDI Piano: Note OFF Ch0 Note:60
🎛 USB MIDI Synthesizer: CC Ch0 Ctrl:7 Val:100

=== MIDI Virtual Wire Hub Status ===
Connected devices: 2
  🎵 USB MIDI Piano (VID:0x1234 PID:0x0001) - RX:15 TX:0
  🎵 USB MIDI Synthesizer (VID:0x5678 PID:0x0002) - RX:0 TX:15
Total: Messages:30 Errors:0 Filtered:0
Active connections: 2
===================================
```

## Supported Devices

The system includes built-in support for common MIDI device vendor IDs:

- **Yamaha** (VID: 0x0499)
- **Roland** (VID: 0x0582) 
- **Hercules** (VID: 0x06F8)
- **Custom/Example devices** (VID: 0x1234, 0x5678, 0x9ABC)

Additional devices can be added by modifying the `is_midi_device()` function in `main.c`.

## Configuration

Edit `config.h` to customize:

- Maximum number of USB devices
- USB scan interval
- Buffer sizes
- Debug output level
- Device detection parameters

## API Components

### Core Components

1. **USB API** (`usb.h`, `usb.c`)
   - USB device driver with endpoint management
   - USB descriptor handling
   - Hardware abstraction layer

2. **MIDI API** (`midi.h`, `midi.c`)
   - MIDI message parsing and formatting
   - USB MIDI class implementation
   - Event-driven callbacks

3. **Virtual Wire System** (`midi_virtual_wire.h`, `midi_virtual_wire.c`)
   - Device registration and management
   - Message routing and filtering
   - Connection management
   - Statistics and monitoring

### Message Flow

```
USB Device → USB Driver → MIDI Parser → Virtual Wire Router → MIDI Formatter → USB Driver → USB Device
```

## Extending the System

### Adding Custom Filters

```c
bool custom_filter(uint8_t source_id, uint8_t dest_id, midi_message_t *msg) {
    // Only route note messages from device 1 to device 2
    if (source_id == 1 && dest_id == 2) {
        uint8_t msg_type = msg->status & 0xF0;
        return (msg_type == MIDI_MSG_NOTE_ON || msg_type == MIDI_MSG_NOTE_OFF);
    }
    return true;
}
```

### Adding Device-Specific Handling

```c
void handle_special_device(uint8_t device_id, uint16_t vid, uint16_t pid) {
    if (vid == 0x1234 && pid == 0x5678) {
        // Special configuration for specific device
        uint8_t connection_id;
        midi_vw_create_connection(device_id, target_device_id, 
                                 0, 9, // Map channel 0 to channel 9 (drums)
                                 MIDI_VW_FILTER_NOTE, &connection_id);
    }
}
```

## Performance

- **Latency**: Sub-millisecond MIDI message routing
- **Throughput**: Supports full MIDI bandwidth (31.25 kbps per device)
- **Memory**: Configurable buffer sizes for memory-constrained systems
- **CPU Usage**: Optimized for real-time systems with minimal overhead

## Troubleshooting

### Device Not Detected
- Check if device VID/PID is in the supported device list
- Verify USB enumeration is working
- Enable debug messages in `config.h`

### Missing Messages
- Check buffer sizes in configuration
- Monitor for buffer overruns in statistics
- Verify USB transfer completion

### High Latency
- Reduce `CONFIG_MAIN_LOOP_DELAY_MS`
- Optimize message processing loop
- Check for blocking operations in callbacks

## License

This project is part of the RTOS USB MIDI system and follows the same licensing terms.