CC ?= gcc
BUILD ?= default
BASE_CFLAGS ?= -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
MODE_CFLAGS :=
ifeq ($(BUILD),debug)
MODE_CFLAGS := -O0 -g -DDEBUG
else ifeq ($(BUILD),release)
MODE_CFLAGS := -O2 -DNDEBUG
else
MODE_CFLAGS := -O2
endif
CFLAGS ?= $(BASE_CFLAGS) $(MODE_CFLAGS)

INCLUDES := -I../src -I./mocks

RING_TEST_SRCS := test_ring_queue.c
GPIO_TEST_SRCS := test_gpio_button.c mocks/time_mock.c
EVENT_TEST_SRCS := test_event_queue.c
FLOW_TEST_SRCS := test_event_flow.c mocks/time_mock.c
THREAD_TEST_SRCS := test_thread_safety.c
UI_TEST_SRCS := test_ui_controller.c
UI_THREAD_TEST_SRCS := test_ui_thread_default.c
TASK_QUEUE_TEST_SRCS := test_task_queue.c
RESULT_QUEUE_TEST_SRCS := test_result_queue.c
UBUS_ASYNC_TEST_SRCS := test_ubus_async.c

RING_TEST_OBJS := $(RING_TEST_SRCS:.c=.o) ../src/ring_queue.o
GPIO_TEST_OBJS := $(GPIO_TEST_SRCS:.c=.o) ../src/hal/gpio_hal_mock.o
EVENT_TEST_OBJS := $(EVENT_TEST_SRCS:.c=.o) ../src/event_queue.o ../src/ring_queue.o
# Common UI dependencies
UI_COMMON_OBJS := ../src/ui_controller.o ../src/page_controller.o ../src/anim.o \
	../src/pages/page_home.o ../src/pages/page_network.o ../src/pages/page_services.o \
	../src/hal/u8g2_stub.o ../src/service_config.o mocks/sys_status_mock.o mocks/display_mock.o

FLOW_TEST_OBJS := $(FLOW_TEST_SRCS:.c=.o) $(UI_COMMON_OBJS) ../src/event_loop.o ../src/event_queue.o ../src/ui_thread.o ../src/ring_queue.o
THREAD_TEST_OBJS := $(THREAD_TEST_SRCS:.c=.o) ../src/event_queue.o ../src/ring_queue.o
UI_TEST_OBJS := $(UI_TEST_SRCS:.c=.o) $(UI_COMMON_OBJS) ../src/event_queue.o ../src/ring_queue.o
UI_THREAD_TEST_OBJS := $(UI_THREAD_TEST_SRCS:.c=.o) $(UI_COMMON_OBJS) ../src/ui_thread.o ../src/event_loop.o ../src/event_queue.o ../src/ring_queue.o
TASK_QUEUE_TEST_OBJS := $(TASK_QUEUE_TEST_SRCS:.c=.o) ../src/task_queue.o ../src/ring_queue.o
RESULT_QUEUE_TEST_OBJS := $(RESULT_QUEUE_TEST_SRCS:.c=.o) ../src/result_queue.o ../src/ring_queue.o
UBUS_ASYNC_TEST_OBJS := $(UBUS_ASYNC_TEST_SRCS:.c=.o) ../src/ubus_thread.o ../src/task_queue.o ../src/result_queue.o ../src/ring_queue.o ../src/hal/ubus_hal_mock.o ../src/hal/time_hal_real.o

DOCKER_IMAGE ?= openwrt-sdk:sunxi-cortexa53-24.10.5
TARGET ?= 192.168.33.254
TARGET_USER ?= root
GPIOCHIP_PATH ?= /dev/gpiochip1
BTN_OFFSETS ?= 0,2,3
TEST_IDLE_TIMEOUT_MS ?= 5000
DEBUG ?= 0
VERBOSE ?= 0

DEBUG_CFLAGS :=
ifeq ($(DEBUG),1)
DEBUG_CFLAGS += -DGPIO_DEBUG -DLOOP_DEBUG
endif
ifeq ($(VERBOSE),1)
DEBUG_CFLAGS += -DGPIO_DEBUG_VERBOSE -DLOOP_DEBUG_VERBOSE
endif

test-host: test_ring_queue test_gpio_button test_event_queue test_event_flow test_thread_safety test_ui_controller test_ui_thread_default test_task_queue test_result_queue test_ubus_async
	./test_ring_queue
	./test_gpio_button
	./test_event_queue
	./test_event_flow
	./test_thread_safety
	./test_ui_controller
	./test_ui_thread_default
	./test_task_queue
	./test_result_queue
	./test_ubus_async

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

test_task_queue: $(TASK_QUEUE_TEST_OBJS)
	$(CC) $(CFLAGS) $(TASK_QUEUE_TEST_OBJS) -lpthread -o $@

test_result_queue: $(RESULT_QUEUE_TEST_OBJS)
	$(CC) $(CFLAGS) $(RESULT_QUEUE_TEST_OBJS) -lpthread -o $@

test_ubus_async: $(UBUS_ASYNC_TEST_OBJS)
	$(CC) $(CFLAGS) $(UBUS_ASYNC_TEST_OBJS) -lpthread -o $@

MONITORED_SERVICES ?= dropbear,uhttpd

test-target: test-target-gpio test-target-dual test-target-ubus

.PHONY: test-target test-target-gpio test-target-dual test-target-ubus

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
				-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
				$(DEBUG_CFLAGS) \
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
				-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
				$(DEBUG_CFLAGS) \
				tests/target/test_dual_thread.c \
			src/event_queue.c src/ring_queue.c src/event_loop.c src/ui_thread.c src/ui_controller.c \
			src/hal/gpio_hal_libgpiod.c src/hal/time_hal_real.c \
			-L$$TARGET_DIR/usr/lib -lgpiod -lpthread -o tests/target/test_dual_thread; \
	'
	scp target/test_dual_thread $(TARGET_USER)@$(TARGET):/tmp/
	ssh $(TARGET_USER)@$(TARGET) "/tmp/test_dual_thread"

test-target-ubus:
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
				-DMONITORED_SERVICES=\"$(MONITORED_SERVICES)\" \
				-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
			tests/target/test_ubus_hw.c src/hal/ubus_hal_real.c src/service_config.c \
			-L$$TARGET_DIR/usr/lib -lubus -lubox -lpthread -o tests/target/test_ubus_hw; \
	'
	scp target/test_ubus_hw $(TARGET_USER)@$(TARGET):/tmp/
	ssh $(TARGET_USER)@$(TARGET) "/tmp/test_ubus_hw"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/%.o: ../src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/pages/%.o: ../src/pages/%.c
	@mkdir -p ../src/pages
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/hal/%.o: ../src/hal/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

mocks/%.o: mocks/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(RING_TEST_SRCS:.c=.o) $(GPIO_TEST_SRCS:.c=.o) $(EVENT_TEST_SRCS:.c=.o) \
		$(FLOW_TEST_SRCS:.c=.o) $(THREAD_TEST_SRCS:.c=.o) \
		$(UI_TEST_SRCS:.c=.o) $(UI_THREAD_TEST_SRCS:.c=.o) \
		$(TASK_QUEUE_TEST_SRCS:.c=.o) $(RESULT_QUEUE_TEST_SRCS:.c=.o) \
		$(UBUS_ASYNC_TEST_SRCS:.c=.o) \
		mocks/display_mock.o mocks/sys_status_mock.o ../src/*.o ../src/hal/*.o ../src/pages/*.o \
		test_ring_queue test_gpio_button test_event_queue test_event_flow test_thread_safety \
		test_ui_controller test_ui_thread_default test_task_queue test_result_queue test_ubus_async \
		target/test_gpio_hw target/test_dual_thread target/test_ubus_hw
