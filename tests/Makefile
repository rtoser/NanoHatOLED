CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

INCLUDES := -I../src -I./mocks

RING_TEST_SRCS := test_ring_queue.c
GPIO_TEST_SRCS := test_gpio_button.c mocks/time_mock.c
EVENT_TEST_SRCS := test_event_queue.c
FLOW_TEST_SRCS := test_event_flow.c mocks/time_mock.c
THREAD_TEST_SRCS := test_thread_safety.c
UI_TEST_SRCS := test_ui_controller.c
UI_THREAD_TEST_SRCS := test_ui_thread_default.c

RING_TEST_OBJS := $(RING_TEST_SRCS:.c=.o) ../src/ring_queue.o
GPIO_TEST_OBJS := $(GPIO_TEST_SRCS:.c=.o) ../src/hal/gpio_hal_mock.o
EVENT_TEST_OBJS := $(EVENT_TEST_SRCS:.c=.o) ../src/event_queue.o ../src/ring_queue.o
FLOW_TEST_OBJS := $(FLOW_TEST_SRCS:.c=.o) mocks/display_mock.o ../src/ui_controller.o ../src/event_loop.o ../src/event_queue.o ../src/ui_thread.o ../src/ring_queue.o
THREAD_TEST_OBJS := $(THREAD_TEST_SRCS:.c=.o) ../src/event_queue.o ../src/ring_queue.o
UI_TEST_OBJS := $(UI_TEST_SRCS:.c=.o) mocks/display_mock.o ../src/ui_controller.o ../src/event_queue.o ../src/ring_queue.o
UI_THREAD_TEST_OBJS := $(UI_THREAD_TEST_SRCS:.c=.o) mocks/display_mock.o ../src/ui_thread.o ../src/ui_controller.o ../src/event_loop.o ../src/event_queue.o ../src/ring_queue.o

DOCKER_IMAGE ?= openwrt-sdk:sunxi-cortexa53-24.10.5
TARGET ?= 192.168.33.254
TARGET_USER ?= root
GPIOCHIP_PATH ?= /dev/gpiochip1
BTN_OFFSETS ?= 0,2,3
TEST_IDLE_TIMEOUT_MS ?= 5000

test-host: test_ring_queue test_gpio_button test_event_queue test_event_flow test_thread_safety test_ui_controller test_ui_thread_default
	./test_ring_queue
	./test_gpio_button
	./test_event_queue
	./test_event_flow
	./test_thread_safety
	./test_ui_controller
	./test_ui_thread_default

test_ring_queue: $(RING_TEST_OBJS)
	$(CC) $(CFLAGS) $(RING_TEST_OBJS) -lpthread -o $@

test_gpio_button: $(GPIO_TEST_OBJS)
	$(CC) $(CFLAGS) $(GPIO_TEST_OBJS) -lpthread -o $@

test_event_queue: $(EVENT_TEST_OBJS)
	$(CC) $(CFLAGS) $(EVENT_TEST_OBJS) -lpthread -o $@

test_event_flow: $(FLOW_TEST_OBJS)
	$(CC) $(CFLAGS) $(FLOW_TEST_OBJS) -lpthread -o $@

test_thread_safety: $(THREAD_TEST_OBJS)
	$(CC) $(CFLAGS) $(THREAD_TEST_OBJS) -lpthread -o $@

test_ui_controller: $(UI_TEST_OBJS)
	$(CC) $(CFLAGS) $(UI_TEST_OBJS) -lpthread -o $@

test_ui_thread_default: $(UI_THREAD_TEST_OBJS)
	$(CC) $(CFLAGS) $(UI_THREAD_TEST_OBJS) -lpthread -o $@

test-target: test-target-gpio test-target-dual

.PHONY: test-target test-target-gpio test-target-dual

test-target-gpio:
	docker run --rm --platform linux/amd64 -v "$(PWD)/..:/work" -w /work $(DOCKER_IMAGE) sh -c '\
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
				-DGPIOCHIP_PATH=\"$(GPIOCHIP_PATH)\" -DBTN_OFFSETS=$(BTN_OFFSETS) \
			tests/target/test_gpio_hw.c src/hal/gpio_hal_libgpiod.c src/hal/time_hal_real.c \
			-L$$TARGET_DIR/usr/lib -lgpiod -lpthread -o tests/target/test_gpio_hw; \
	'
	scp target/test_gpio_hw $(TARGET_USER)@$(TARGET):/tmp/
	ssh $(TARGET_USER)@$(TARGET) "/tmp/test_gpio_hw"

test-target-dual:
	docker run --rm --platform linux/amd64 -v "$(PWD)/..:/work" -w /work $(DOCKER_IMAGE) sh -c '\
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
			$$CC -I./src -I$$TARGET_DIR/usr/include -O2 -Wall -std=c11 \
				-DGPIOCHIP_PATH=\"$(GPIOCHIP_PATH)\" -DBTN_OFFSETS=$(BTN_OFFSETS) \
				-DTEST_IDLE_TIMEOUT_MS=$(TEST_IDLE_TIMEOUT_MS) \
			tests/target/test_dual_thread.c \
			src/event_queue.c src/ring_queue.c src/event_loop.c src/ui_thread.c src/ui_controller.c \
			src/hal/gpio_hal_libgpiod.c src/hal/time_hal_real.c \
			-L$$TARGET_DIR/usr/lib -lgpiod -lpthread -o tests/target/test_dual_thread; \
	'
	scp target/test_dual_thread $(TARGET_USER)@$(TARGET):/tmp/
	ssh $(TARGET_USER)@$(TARGET) "/tmp/test_dual_thread"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/%.o: ../src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(RING_TEST_SRCS:.c=.o) $(GPIO_TEST_SRCS:.c=.o) $(EVENT_TEST_SRCS:.c=.o) \
		$(FLOW_TEST_SRCS:.c=.o) $(THREAD_TEST_SRCS:.c=.o) \
		$(UI_TEST_SRCS:.c=.o) $(UI_THREAD_TEST_SRCS:.c=.o) mocks/display_mock.o ../src/*.o ../src/hal/*.o \
		test_ring_queue test_gpio_button test_event_queue test_event_flow test_thread_safety test_ui_controller test_ui_thread_default \
		target/test_gpio_hw target/test_dual_thread
