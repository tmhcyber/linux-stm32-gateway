CC = gcc
BUILD ?= build
CPPFLAGS = -Iinclude $(shell pkg-config --cflags sqlite3 libmosquitto)
CPPFLAGS += -DGATEWAY_VERSION='"$(shell python3 scripts/linux/build_identity.py)"'
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS =
CORE = src/protocol.c src/framer.c src/diagnostic.c
STORAGE = src/record_queue.c src/storage.c src/history.c src/query.c
SOURCES = $(CORE) src/serial.c src/output.c src/main.c $(STORAGE) src/mqtt.c src/ipc.c
LDLIBS = -pthread $(shell pkg-config --libs sqlite3 libmosquitto)
.PHONY: all test sanitize tsan-build clean
all: $(BUILD)/gateway $(BUILD)/gatewayctl
$(BUILD):
	mkdir -p $@
$(BUILD)/gateway: $(SOURCES) $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/gatewayctl: src/gatewayctl.c src/query.c src/protocol.c src/diagnostic.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/gatewayctl.c src/query.c src/protocol.c src/diagnostic.c $(LDFLAGS) -o $@
$(BUILD)/test_core: tests/test_core.c $(CORE) $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_core.c $(CORE) $(LDFLAGS) -o $@
$(BUILD)/test_queue: tests/test_queue.c src/record_queue.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread tests/test_queue.c src/record_queue.c $(LDFLAGS) -o $@
$(BUILD)/gateway-fault: $(SOURCES) tests/storage_fault.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -DSTORAGE_TESTING $(SOURCES) tests/storage_fault.c $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_queue_threads: tests/test_queue_threads.c src/record_queue.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread tests/test_queue_threads.c src/record_queue.c $(LDFLAGS) -o $@
$(BUILD)/test_history: tests/test_history.c src/history.c src/protocol.c src/diagnostic.c src/query.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread tests/test_history.c src/history.c src/protocol.c src/diagnostic.c src/query.c $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_mqtt_capacity: tests/test_mqtt_capacity.c src/mqtt.c src/protocol.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread tests/test_mqtt_capacity.c src/protocol.c $(LDFLAGS) $(LDLIBS) -o $@
test: all $(BUILD)/test_core $(BUILD)/test_queue $(BUILD)/test_queue_threads $(BUILD)/test_history $(BUILD)/test_mqtt_capacity $(BUILD)/gateway-fault
	$(BUILD)/test_core
	$(BUILD)/test_queue
	$(BUILD)/test_queue_threads
	$(BUILD)/test_history
	$(BUILD)/test_mqtt_capacity | python3 -c 'import json,sys; r=json.load(sys.stdin); assert r["diagnostic"]["received"]==18446744073709551615'
	GATEWAY=$(abspath $(BUILD)/gateway) GATEWAY_FAULT=$(abspath $(BUILD)/gateway-fault) python3 scripts/linux/run_test_suite.py
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test
tsan-build:
	$(MAKE) BUILD=$(BUILD)/tsan CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=thread -fno-pie' LDFLAGS='-fsanitize=thread -no-pie' all $(BUILD)/tsan/test_queue_threads $(BUILD)/tsan/gateway-fault
clean:
	rm -f $(BUILD)/gateway $(BUILD)/gateway-fault $(BUILD)/test_core $(BUILD)/test_queue $(BUILD)/test_queue_threads $(BUILD)/test_history
