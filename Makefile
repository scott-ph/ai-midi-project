CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_DEFAULT_SOURCE
SOURCES = usb.c usb_example.c midi.c usb_midi_descriptors.c midi_example.c midi_virtual_wire.c midi_virtual_wire_example.c main.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = midi_hub

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean install uninstall run