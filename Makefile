# Paperleaf — build on Raspberry Pi OS: sudo apt install build-essential libncurses-dev
# Optional cross: make CC=arm-linux-gnueabihf-gcc

CFLAGS ?= -std=c99 -Wall -Wextra -O2
LDFLAGS ?=
LDLIBS ?= -lncurses

TARGET = paperleaf
SRC = src/main.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)
