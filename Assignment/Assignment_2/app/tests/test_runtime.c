/*
 * Purpose: Exercise application queues, alert routing, logger, and shutdown.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#define _POSIX_C_SOURCE 200809L
#include "alert_thread.h"
#include "logger_thread.h"
#include "message_queue.h"
#include "processor_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TEST_BULK_MESSAGES 12000U
#define TEST_PERIODIC_FLUSH_SECONDS 6U

static char cleanup_log_path[160];
static char cleanup_rotated_path[176];
static char cleanup_alarm_queue[64];
static char cleanup_log_queue[64];

static void cleanup_test_artifacts(void)
{
    if (cleanup_alarm_queue[0] != '\0')
        (void)mq_unlink(cleanup_alarm_queue);
    if (cleanup_log_queue[0] != '\0')
        (void)mq_unlink(cleanup_log_queue);
    if (cleanup_log_path[0] != '\0')
        (void)unlink(cleanup_log_path);
    if (cleanup_rotated_path[0] != '\0')
        (void)unlink(cleanup_rotated_path);
}

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static long count_text(const char *path, const char *needle)
{
    FILE *stream = fopen(path, "r");
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    long count = 0;

    if (stream == NULL)
        return -1;

    while ((length = getline(&line, &capacity, stream)) >= 0) {
        if (length > 0 && strstr(line, needle) != NULL)
            count++;
    }

    free(line);
    fclose(stream);
    return count;
}

static long count_text_pair(const char *first_path, const char *second_path,
                            const char *needle)
{
    long first_count = count_text(first_path, needle);
    long second_count = count_text(second_path, needle);

    if (first_count < 0 || second_count < 0)
        return -1;
    return first_count + second_count;
}

static void sleep_seconds(unsigned int seconds)
{
    struct timespec remaining = {
        .tv_sec = (time_t)seconds,
        .tv_nsec = 0
    };

    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR)
        ;
}

static int wait_for_initial_events(struct AppContext *context)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 20U; attempt++) {
        struct AppMetrics metrics;
        bool failed;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000L};

        app_get_metrics(context, &metrics, &failed, NULL, 0);
        if (failed)
            return -1;
        if (metrics.processed_samples == 1 &&
            metrics.temperature_alerts == 1 &&
            metrics.humidity_alerts == 1)
            return 0;
        nanosleep(&pause, NULL);
    }
    return -1;
}

static void verify_queue_unlinked(const char *name)
{
    mqd_t queue;

    errno = 0;
    queue = mq_open(name, O_RDONLY);
    require(queue == (mqd_t)-1 && errno == ENOENT,
            "message queue unlinked during context cleanup");
}

int main(void)
{
    struct AppConfig config = {
        .interval_ms = 500,
        .temperature_threshold = 40.0,
        .humidity_threshold = 80.0,
        .duration_seconds = 0,
        .verbose = false,
        .device_path = "/dev/sms_sensor",
        .log_path = NULL
    };
    struct AppContext context;
    struct AppMetrics metrics;
    struct SensorData samples[] = {
        {.timestamp_ms = 1000, .temperature = 41.0, .humidity = 81.0},
        {.timestamp_ms = 1500, .temperature = 34.0, .humidity = 50.0},
        {.timestamp_ms = 2000, .temperature = 35.0, .humidity = 51.0}
    };
    pthread_t logger_thread;
    pthread_t alert_thread;
    pthread_t processor_thread;
    char log_path[160];
    char rotated_path[176];
    char alarm_queue_name[64];
    char log_queue_name[64];
    struct stat status;
    bool failed;
    unsigned int index;
    int written;

    written = snprintf(log_path, sizeof(log_path),
                       "/tmp/sms_runtime_test_%ld.log", (long)getpid());
    require(written > 0 && (size_t)written < sizeof(log_path),
            "format test log path");
    written = snprintf(rotated_path, sizeof(rotated_path), "%s.1", log_path);
    require(written > 0 && (size_t)written < sizeof(rotated_path),
            "format test rotated log path");
    (void)snprintf(cleanup_log_path, sizeof(cleanup_log_path), "%s", log_path);
    (void)snprintf(cleanup_rotated_path, sizeof(cleanup_rotated_path), "%s",
                   rotated_path);
    require(atexit(cleanup_test_artifacts) == 0,
            "register test artifact cleanup");
    unlink(log_path);
    unlink(rotated_path);
    config.log_path = log_path;

    require(app_context_init(&context, &config) == 0,
            "initialize application context and POSIX queues");
    (void)snprintf(alarm_queue_name, sizeof(alarm_queue_name), "%s",
                   context.alarm_queue_name);
    (void)snprintf(log_queue_name, sizeof(log_queue_name), "%s",
                   context.log_queue_name);
    (void)snprintf(cleanup_alarm_queue, sizeof(cleanup_alarm_queue), "%s",
                   alarm_queue_name);
    (void)snprintf(cleanup_log_queue, sizeof(cleanup_log_queue), "%s",
                   log_queue_name);

    require(pthread_create(&logger_thread, NULL, logger_thread_main,
                           &context) == 0,
            "start logger worker");
    require(pthread_create(&alert_thread, NULL, alert_thread_main,
                           &context) == 0,
            "start alert worker");
    require(pthread_create(&processor_thread, NULL, processor_thread_main,
                           &context) == 0,
            "start processor worker");

    require(circular_buffer_push(context.buffer, &samples[0]) == 0,
            "enqueue first deterministic processor sample");
    require(wait_for_initial_events(&context) == 0,
            "processor and alert workers handle first sample");
    sleep_seconds(TEST_PERIODIC_FLUSH_SECONDS);
    require(stat(log_path, &status) == 0 && status.st_size > 0,
            "logger periodically flushes buffered records");

    for (index = 1; index < sizeof(samples) / sizeof(samples[0]); index++)
        require(circular_buffer_push(context.buffer, &samples[index]) == 0,
                "enqueue remaining processor samples");

    for (index = 0; index < TEST_BULK_MESSAGES; index++) {
        struct LogMessage message = {
            .type = LOG_SAMPLE,
            .sample = {
                .timestamp_ms = 10000 + (long long)index,
                .temperature = 22.5,
                .humidity = 47.0
            },
            .temperature_average = 22.5,
            .humidity_average = 47.0
        };

        require(app_queue_send(context.log_queue, &message,
                               sizeof(message)) == 0,
                "queue accepts message under consumer backpressure");
    }

    circular_buffer_close(context.buffer);
    require(pthread_join(processor_thread, NULL) == 0,
            "processor exits and queues alert shutdown after drain");

    require(pthread_join(alert_thread, NULL) == 0,
            "alert worker drains alarms and queues logger shutdown");
    require(pthread_join(logger_thread, NULL) == 0,
            "logger drains pending records and flushes on shutdown");

    app_get_metrics(&context, &metrics, &failed, NULL, 0);
    require(!failed && metrics.processed_samples == 3 &&
                metrics.temperature_alerts == 1 &&
                metrics.humidity_alerts == 1 &&
                metrics.temperature_spikes == 1,
            "processor and alert worker metrics");

    require(stat(log_path, &status) == 0 &&
                status.st_size <= 1024L * 1024L &&
                stat(rotated_path, &status) == 0 &&
                status.st_size > 1024L * 1024L,
            "logger rotates log above one mebibyte");
    require(count_text_pair(log_path, rotated_path,
                            "ALERT type=TEMP_HIGH") == 1 &&
                count_text_pair(log_path, rotated_path,
                                "ALERT type=HUMID_HIGH") == 1 &&
                count_text_pair(log_path, rotated_path,
                                "ALERT type=TEMP_SPIKE") == 1,
            "logger persists every alert event");
    require(count_text_pair(log_path, rotated_path, "ts_ms=21999") == 1,
            "logger persists messages queued immediately before shutdown");

    require(app_context_destroy(&context) == 0,
            "close and unlink message queues during cleanup");
    verify_queue_unlinked(alarm_queue_name);
    verify_queue_unlinked(log_queue_name);
    unlink(log_path);
    unlink(rotated_path);
    puts("PASS: queue delivery/backpressure, alert routing, logger flush,"
         " rotation, drain-on-shutdown, and queue cleanup");
    return EXIT_SUCCESS;
}
