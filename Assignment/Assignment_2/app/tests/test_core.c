/*
 * Purpose: Deterministic host tests for SMS buffer and processor core.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include "circular_buffer.h"
#include "processor_core.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

struct ConsumerArgs {
    struct CircularBuffer *buffer;
    int result;
};

static void *blocked_consumer(void *argument)
{
    struct ConsumerArgs *args = argument;
    struct SensorData sample;

    args->result = circular_buffer_pop(args->buffer, &sample);
    return NULL;
}

static void test_buffer_capacity_and_overflow(void)
{
    struct CircularBuffer *buffer;
    struct CircularBufferStats stats;
    struct SensorData input = {.timestamp_ms = 1,
                               .temperature = 20.0,
                               .humidity = 40.0};
    struct SensorData output;
    size_t index;

    require(circular_buffer_create(&buffer, CIRCULAR_BUFFER_CAPACITY) == 0,
            "create 64-sample buffer");
    for (index = 0; index < CIRCULAR_BUFFER_CAPACITY; index++) {
        input.timestamp_ms = (long long)index;
        require(circular_buffer_push(buffer, &input) == 0,
                "push within capacity");
    }
    require(circular_buffer_push(buffer, &input) == ENOSPC,
            "reject sample when full");
    require(circular_buffer_get_stats(buffer, &stats) == 0 &&
                stats.current_size == CIRCULAR_BUFFER_CAPACITY &&
                stats.total_produced == CIRCULAR_BUFFER_CAPACITY &&
                stats.total_consumed == 0 &&
                stats.overflow_count == 1,
            "buffer counters and overflow count");

    for (index = 0; index < CIRCULAR_BUFFER_CAPACITY; index++) {
        require(circular_buffer_pop(buffer, &output) == 0 &&
                    output.timestamp_ms == (long long)index,
                "FIFO order");
    }
    require(circular_buffer_get_stats(buffer, &stats) == 0 &&
                stats.current_size == 0 &&
                stats.total_consumed == CIRCULAR_BUFFER_CAPACITY,
            "consumption counters");
    circular_buffer_destroy(buffer);
}

static void test_consumer_wakeup_and_close(void)
{
    struct CircularBuffer *buffer;
    struct ConsumerArgs args;
    struct SensorData sample = {.timestamp_ms = 9,
                                .temperature = 25.0,
                                .humidity = 50.0};
    struct SensorData output;
    struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000L};
    pthread_t thread;

    require(circular_buffer_create(&buffer, CIRCULAR_BUFFER_CAPACITY) == 0,
            "create buffer for blocking consumer");
    args.buffer = buffer;
    args.result = -1;
    require(pthread_create(&thread, NULL, blocked_consumer, &args) == 0,
            "create blocking consumer");
    nanosleep(&pause, NULL);
    require(circular_buffer_push(buffer, &sample) == 0,
            "push wakes waiting consumer");
    require(pthread_join(thread, NULL) == 0 && args.result == 0,
            "consumer receives pushed sample");

    args.result = -1;
    require(pthread_create(&thread, NULL, blocked_consumer, &args) == 0,
            "create consumer for close wakeup");
    nanosleep(&pause, NULL);
    circular_buffer_close(buffer);
    require(pthread_join(thread, NULL) == 0 &&
                args.result == ECANCELED,
            "close wakes empty consumer");
    require(circular_buffer_pop(buffer, &output) == ECANCELED,
            "closed empty buffer returns cancellation");
    circular_buffer_destroy(buffer);
}

static void test_processor_thresholds_and_rolling_window(void)
{
    struct ProcessorState state;
    struct ProcessorResult result;
    struct SensorData sample = {.timestamp_ms = 1,
                                .temperature = 39.0,
                                .humidity = 79.0};
    unsigned int index;

    processor_state_init(&state);
    require(processor_process_sample(&state, &sample, 40.0, 80.0,
                                     &result) == 0 &&
                result.alert_flags == 0,
            "first sample establishes previous temperature");

    sample.timestamp_ms++;
    sample.temperature = 44.1;
    sample.humidity = 80.1;
    require(processor_process_sample(&state, &sample, 40.0, 80.0,
                                     &result) == 0 &&
                result.alert_flags ==
                    (PROCESSOR_ALERT_TEMP_HIGH |
                     PROCESSOR_ALERT_HUMID_HIGH |
                     PROCESSOR_ALERT_TEMP_SPIKE),
            "strict threshold and spike conditions");
    require(fabs(result.rolling_temperature_average - 41.55) < 0.0001 &&
                fabs(result.rolling_humidity_average - 79.55) < 0.0001,
            "averages include current and previous sample");

    processor_state_init(&state);
    for (index = 1; index <= PROCESSOR_WINDOW_SIZE; index++) {
        sample.temperature = (double)index;
        sample.humidity = (double)(index * 2U);
        require(processor_process_sample(&state, &sample, 100.0, 100.0,
                                         &result) == 0,
                "process rolling window sample");
    }
    require(fabs(result.rolling_temperature_average - 5.5) < 0.0001 &&
                fabs(result.rolling_humidity_average - 11.0) < 0.0001,
            "rolling average over ten samples");

    sample.temperature = 20.0;
    sample.humidity = 40.0;
    require(processor_process_sample(&state, &sample, 100.0, 100.0,
                                     &result) == 0 &&
                fabs(result.rolling_temperature_average - 7.4) < 0.0001 &&
                fabs(result.rolling_humidity_average - 14.8) < 0.0001,
            "rolling window discards oldest sample");
}

int main(void)
{
    test_buffer_capacity_and_overflow();
    puts("PASS: capacity, FIFO order, and overflow counters");
    test_consumer_wakeup_and_close();
    puts("PASS: condition-variable wakeup and shutdown");
    test_processor_thresholds_and_rolling_window();
    puts("PASS: alert conditions and ten-sample rolling average");
    return EXIT_SUCCESS;
}
