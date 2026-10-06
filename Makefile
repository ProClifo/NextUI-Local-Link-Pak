CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?= -Isrc

TEST_BIN := build/test_session
TEST_SRCS := src/session.c tests/test_session.c

.PHONY: all test clean

all: test

$(TEST_BIN): $(TEST_SRCS) src/session.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SRCS) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build
