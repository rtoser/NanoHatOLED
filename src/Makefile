CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

TARGET_DIR ?= /opt/target
BUILD_DIR ?= build/target
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
GPIOCHIP_PATH ?= /dev/gpiochip1
BTN_OFFSETS ?= 0,2,3

CFLAGS += -DGPIOCHIP_PATH=\"$(GPIOCHIP_PATH)\" -DBTN_OFFSETS=$(BTN_OFFSETS)
CFLAGS += -I. -I$(TARGET_DIR)/usr/include

LDFLAGS ?= -lpthread -lgpiod

SRCS := \
	main.c \
	event_loop.c \
	event_queue.c \
	ring_queue.c \
	ui_thread.c \
	ui_controller.c \
	hal/gpio_hal_libgpiod.c \
	hal/time_hal_real.c \
	hal/display_hal_null.c

OBJS := $(SRCS:%.c=$(OBJ_DIR)/%.o)
BIN := $(BIN_DIR)/nanohat-oled

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p "$(BIN_DIR)"
	$(CC) $(CFLAGS) $(OBJS) -L$(TARGET_DIR)/usr/lib $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf "$(BUILD_DIR)"

.PHONY: all clean
