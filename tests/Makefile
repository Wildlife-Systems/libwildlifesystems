# Makefile for libwildlifesystems unit tests
# Uses Unity test framework

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../src -DTEST_BUILD

# Source files
UNITY_SRC = unity.c
TEST_SRC = test_ws_utils.c
LIB_SRC = ../src/ws_utils.c

# Object files
OBJS = unity.o test_ws_utils.o ws_utils.o

# Test binary
TEST_BIN = run_tests

.PHONY: all clean test

all: $(TEST_BIN)

$(TEST_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

unity.o: unity.c unity.h
	$(CC) $(CFLAGS) -c unity.c

test_ws_utils.o: test_ws_utils.c unity.h ../src/ws_utils.h
	$(CC) $(CFLAGS) -c test_ws_utils.c

ws_utils.o: $(LIB_SRC) ../src/ws_utils.h
	$(CC) $(CFLAGS) -c $(LIB_SRC)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(TEST_BIN)
