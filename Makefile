CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?= -Iinclude

BUILD := build
SESSION_TEST := $(BUILD)/test_session
BACKEND_TEST := $(BUILD)/test_backend_stub

.PHONY: all test clean

all: test

test: $(SESSION_TEST) $(BACKEND_TEST)
	$(SESSION_TEST)
	$(BACKEND_TEST)

$(BUILD):
	mkdir -p $(BUILD)

$(SESSION_TEST): src/session.c tests/test_session.c include/session.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/session.c tests/test_session.c -o $@

$(BACKEND_TEST): src/session.c src/mgba_backend_stub.c tests/test_backend_stub.c include/session.h include/mgba_backend.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/session.c src/mgba_backend_stub.c tests/test_backend_stub.c -o $@

clean:
	rm -rf $(BUILD)
