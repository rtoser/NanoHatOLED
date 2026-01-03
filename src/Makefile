CC = aarch64-linux-musl-gcc
TARGET_DIR ?= /opt/target
UBUS_LDFLAGS = -lubus -lubox -lblobmsg_json -ljson-c
UBUS_CFLAGS =
ifneq ("$(wildcard $(TARGET_DIR)/usr/include/libubus.h)","")
UBUS_CFLAGS += -I$(TARGET_DIR)/usr/include
UBUS_LDFLAGS = -L$(TARGET_DIR)/usr/lib $(UBUS_LDFLAGS)
endif

CFLAGS = -I./u8g2/csrc -I. -O2 -Wall $(UBUS_CFLAGS)
LDFLAGS = -static $(UBUS_LDFLAGS)

# U8g2 sources
U8G2_SRC_DIR = u8g2/csrc
U8G2_SOURCES = $(wildcard $(U8G2_SRC_DIR)/*.c)
U8G2_OBJECTS = $(U8G2_SOURCES:.c=.o)

# Main sources
APP_SOURCES = main.c u8g2_port_linux.c gpio_button.c sys_status.c ubus_service.c
APP_OBJECTS = $(APP_SOURCES:.c=.o)

TARGET = nanohat-oled

all: $(TARGET)

$(TARGET): $(APP_OBJECTS) $(U8G2_OBJECTS)
	$(CC) $(APP_OBJECTS) $(U8G2_OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(APP_OBJECTS) $(U8G2_OBJECTS) $(TARGET)

.PHONY: all clean
