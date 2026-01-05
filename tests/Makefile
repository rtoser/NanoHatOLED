CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

INCLUDES := -I../src -I./mocks

RING_TEST_SRCS := test_ring_queue.c
GPIO_TEST_SRCS := test_gpio_button.c mocks/time_mock.c

RING_TEST_OBJS := $(RING_TEST_SRCS:.c=.o) ../src/ring_queue.o
GPIO_TEST_OBJS := $(GPIO_TEST_SRCS:.c=.o) ../src/hal/gpio_hal_mock.o

DOCKER_IMAGE ?= openwrt-sdk:sunxi-cortexa53-24.10.5
TARGET ?= 192.168.33.254
TARGET_USER ?= root
GPIOCHIP_PATH ?= /dev/gpiochip1
BTN_OFFSETS ?= 0,2,3

test-host: test_ring_queue test_gpio_button
	./test_ring_queue
	./test_gpio_button

test_ring_queue: $(RING_TEST_OBJS)
	$(CC) $(CFLAGS) $(RING_TEST_OBJS) -lpthread -o $@

test_gpio_button: $(GPIO_TEST_OBJS)
	$(CC) $(CFLAGS) $(GPIO_TEST_OBJS) -lpthread -o $@

test-target:
	docker run --rm -v "$(PWD)/..:/work" -w /work $(DOCKER_IMAGE) sh -c '\
		set -e; \
		if [ -d /builder/staging_dir ]; then \
			TOOLCHAIN_DIR=$$(find /builder/staging_dir -maxdepth 1 -type d -name "toolchain-*" | head -1); \
			TARGET_DIR=$$(find /builder/staging_dir -maxdepth 1 -type d -name "target-*" | head -1); \
			if [ -n "$$TOOLCHAIN_DIR" ]; then export PATH="$$TOOLCHAIN_DIR/bin:$$PATH"; fi; \
		fi; \
		if command -v aarch64-openwrt-linux-musl-gcc >/dev/null 2>&1; then \
			CC=aarch64-openwrt-linux-musl-gcc; \
		elif command -v aarch64-linux-musl-gcc >/dev/null 2>&1; then \
			CC=aarch64-linux-musl-gcc; \
		else \
			echo "Error: No suitable cross compiler found" >&2; exit 1; \
		fi; \
		TARGET_DIR=$${TARGET_DIR:-/opt/target}; \
		$$CC -I./src -I$$TARGET_DIR/usr/include -O2 -Wall \
			-DGPIOCHIP_PATH=\\"$(GPIOCHIP_PATH)\\" -DBTN_OFFSETS=$(BTN_OFFSETS) \
			tests/target/test_gpio_hw.c src/hal/gpio_hal_libgpiod.c src/hal/time_hal_real.c \
			-L$$TARGET_DIR/usr/lib -lgpiod -lpthread -o tests/target/test_gpio_hw; \
	'
	scp tests/target/test_gpio_hw $(TARGET_USER)@$(TARGET):/tmp/
	ssh $(TARGET_USER)@$(TARGET) "/tmp/test_gpio_hw"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/%.o: ../src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(RING_TEST_SRCS:.c=.o) $(GPIO_TEST_SRCS:.c=.o) ../src/*.o ../src/hal/*.o test_ring_queue test_gpio_button
