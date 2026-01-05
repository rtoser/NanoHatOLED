CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

INCLUDES := -I../src -I./mocks

RING_TEST_SRCS := test_ring_queue.c
GPIO_TEST_SRCS := test_gpio_button.c mocks/time_mock.c

RING_TEST_OBJS := $(RING_TEST_SRCS:.c=.o) ../src/ring_queue.o
GPIO_TEST_OBJS := $(GPIO_TEST_SRCS:.c=.o) ../src/hal/gpio_hal_mock.o

test-host: test_ring_queue test_gpio_button
	./test_ring_queue
	./test_gpio_button

test_ring_queue: $(RING_TEST_OBJS)
	$(CC) $(CFLAGS) $(RING_TEST_OBJS) -lpthread -o $@

test_gpio_button: $(GPIO_TEST_OBJS)
	$(CC) $(CFLAGS) $(GPIO_TEST_OBJS) -lpthread -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/%.o: ../src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(RING_TEST_SRCS:.c=.o) $(GPIO_TEST_SRCS:.c=.o) ../src/*.o ../src/hal/*.o test_ring_queue test_gpio_button
