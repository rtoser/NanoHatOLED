CC = aarch64-linux-musl-gcc
CFLAGS = -I./u8g2/csrc -I. -O2 -Wall
LDFLAGS = -static

# U8g2 sources
U8G2_SRC_DIR = u8g2/csrc
U8G2_SOURCES = $(wildcard $(U8G2_SRC_DIR)/*.c)
U8G2_OBJECTS = $(U8G2_SOURCES:.c=.o)

# Main sources
APP_SOURCES = main.c u8g2_port_linux.c gpio_button.c sys_status.c
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
