CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = single_packet_tx

all: $(TARGET)

$(TARGET): single_packet_tx.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
