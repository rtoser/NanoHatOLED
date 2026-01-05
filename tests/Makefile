CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

INCLUDES := -I../src -I./mocks

TEST_SRCS := test_ring_queue.c mocks/time_mock.c
TEST_OBJS := $(TEST_SRCS:.c=.o)

test-host: test_runner
	./test_runner

test_runner: $(TEST_OBJS) ../src/ring_queue.o
	$(CC) $(CFLAGS) $(TEST_OBJS) ../src/ring_queue.o -lpthread -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

../src/%.o: ../src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TEST_OBJS) ../src/*.o test_runner
