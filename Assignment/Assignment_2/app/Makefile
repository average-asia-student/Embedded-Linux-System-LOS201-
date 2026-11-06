# Author: Tong Tran Dang (SE204445)
CC_HOST ?= gcc
CC_ARM ?= arm-linux-gnueabihf-gcc

COMMON_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -pthread -I. \
	-I../driver -D_POSIX_C_SOURCE=200809L
APP_SOURCES := sms_app.c sensor_thread.c processor_thread.c \
	alert_thread.c logger_thread.c message_queue.c time_format.c \
	app_context.c processor_core.c circular_buffer.c
TEST_CORE_SOURCES := tests/test_core.c processor_core.c circular_buffer.c
TEST_RUNTIME_SOURCES := tests/test_runtime.c sensor_thread.c processor_thread.c \
	alert_thread.c logger_thread.c message_queue.c time_format.c \
	app_context.c processor_core.c circular_buffer.c
TEST_QUEUE_SOURCES := tests/test_queue.c message_queue.c

.PHONY: all arm host test clean

all: host arm

arm:
	$(CC_ARM) $(COMMON_CFLAGS) -static -o sms_app $(APP_SOURCES) -lm

host:
	$(CC_HOST) $(COMMON_CFLAGS) -o sms_app_x86 $(APP_SOURCES) -lm

test: test_core test_queue_test test_runtime
	./test_core
	./test_queue_test
	./test_runtime

test_core: $(TEST_CORE_SOURCES)
	$(CC_HOST) $(COMMON_CFLAGS) -o $@ $(TEST_CORE_SOURCES) -lm

test_runtime: $(TEST_RUNTIME_SOURCES)
	$(CC_HOST) $(COMMON_CFLAGS) -o $@ $(TEST_RUNTIME_SOURCES) -lrt -lm

test_queue_test: $(TEST_QUEUE_SOURCES)
	$(CC_HOST) $(COMMON_CFLAGS) -DAPP_QUEUE_SEND_RETRIES=2 -o $@ \
		$(TEST_QUEUE_SOURCES) -lrt

clean:
	rm -f sms_app sms_app_x86 test_core test_runtime test_queue_test
