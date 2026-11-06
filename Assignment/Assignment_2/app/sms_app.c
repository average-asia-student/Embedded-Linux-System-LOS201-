/*
 * Purpose: Configure, run, and shut down all SMS application workers.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#define _POSIX_C_SOURCE 200809L
#include "sms_app.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "processor_thread.h"
#include "alert_thread.h"
#include "logger_thread.h"
#include "message_queue.h"
#include "sensor_thread.h"

#define APP_DEFAULT_INTERVAL_MS 500U
#define APP_DEFAULT_TEMP_THRESHOLD 40.0
#define APP_DEFAULT_HUMID_THRESHOLD 80.0
#define APP_DEFAULT_DURATION_SECONDS 60U

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "  -i <ms>    Sensor sampling interval (default: 500 ms)\n"
            "  -t <C>     Temperature alert threshold (default: 40.0)\n"
            "  -h <%%>     Humidity alert threshold (default: 80.0)\n"
            "  -d <sec>   Run duration; 0 means infinite (default: 60)\n"
            "  -v         Print each sample and processor result\n",
            program);
}

static void sensor_wakeup_handler(int signal_number)
{
    (void)signal_number;
}

static int create_worker_thread(pthread_t *thread,
                                void *(*start_routine)(void *),
                                void *argument, bool *memory_locked)
{
    int error = pthread_create(thread, NULL, start_routine, argument);

    if (error != EAGAIN || !*memory_locked)
        return error;

    if (munlockall() != 0) {
        fprintf(stderr,
                "[RT] cannot release memory lock after worker creation "
                "failed: %s\n",
                strerror(errno));
        return error;
    }

    *memory_locked = false;
    fprintf(stderr,
            "[RT] worker creation hit the memory-lock limit; "
            "retrying without memory locking\n");
    return pthread_create(thread, NULL, start_routine, argument);
}

static int parse_unsigned(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || value == NULL || *text == '\0' || *text == '-')
        return EINVAL;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT_MAX)
        return EINVAL;

    *value = (unsigned int)parsed;
    return 0;
}

static int parse_threshold(const char *text, double *value)
{
    char *end;
    double parsed;

    if (text == NULL || value == NULL || *text == '\0')
        return EINVAL;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || *end != '\0' || !isfinite(parsed) ||
        parsed < 0.0 || parsed > 100.0)
        return EINVAL;

    *value = parsed;
    return 0;
}

static int parse_options(int argc, char **argv, struct AppConfig *config)
{
    int option;
    const char *sensor_policy;

    config->interval_ms = APP_DEFAULT_INTERVAL_MS;
    config->temperature_threshold = APP_DEFAULT_TEMP_THRESHOLD;
    config->humidity_threshold = APP_DEFAULT_HUMID_THRESHOLD;
    config->duration_seconds = APP_DEFAULT_DURATION_SECONDS;
    config->sensor_use_fifo = true;
    config->verbose = false;
    config->device_path = SMS_SENSOR_DEVICE_PATH;
    config->log_path = SMS_LOG_FILE_PATH;
    config->jitter_csv_path = getenv("SMS_JITTER_CSV");

    sensor_policy = getenv("SMS_SENSOR_POLICY");
    if (sensor_policy != NULL) {
        if (strcmp(sensor_policy, "fifo") == 0) {
            config->sensor_use_fifo = true;
        } else if (strcmp(sensor_policy, "other") == 0) {
            config->sensor_use_fifo = false;
        } else {
            fprintf(stderr,
                    "SMS_SENSOR_POLICY must be either 'fifo' or 'other'\n");
            return EINVAL;
        }
    }
    if (config->jitter_csv_path != NULL &&
        config->jitter_csv_path[0] == '\0') {
        fprintf(stderr, "SMS_JITTER_CSV must name a non-empty path\n");
        return EINVAL;
    }

    while ((option = getopt(argc, argv, "i:t:h:d:v")) != -1) {
        int error = 0;

        switch (option) {
        case 'i':
            error = parse_unsigned(optarg, &config->interval_ms);
            if (error == 0 && config->interval_ms == 0)
                error = EINVAL;
            break;
        case 't':
            error = parse_threshold(optarg,
                                    &config->temperature_threshold);
            break;
        case 'h':
            error = parse_threshold(optarg, &config->humidity_threshold);
            break;
        case 'd':
            error = parse_unsigned(optarg, &config->duration_seconds);
            break;
        case 'v':
            config->verbose = true;
            break;
        default:
            return EINVAL;
        }

        if (error != 0)
            return error;
    }

    return optind == argc ? 0 : EINVAL;
}

static int wait_for_stop_or_signal(struct AppContext *context,
                                   const sigset_t *signal_set,
                                   const struct timespec *start_time)
{
    while (!app_should_stop(context)) {
        struct timespec timeout = {.tv_sec = 0, .tv_nsec = 100000000L};
        siginfo_t signal_info;
        int received_signal;

        if (context->config.duration_seconds != 0U) {
            struct timespec now;
            long long elapsed_ms;

            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                return errno;
            elapsed_ms = (long long)(now.tv_sec - start_time->tv_sec) * 1000LL +
                         (now.tv_nsec - start_time->tv_nsec) / 1000000LL;
            if (elapsed_ms >=
                (long long)context->config.duration_seconds * 1000LL)
                break;
        }

        received_signal = sigtimedwait(signal_set, &signal_info, &timeout);
        if (received_signal == SIGINT)
            break;
        if (received_signal < 0 && errno != EAGAIN && errno != EINTR)
            return errno;
    }
    return 0;
}

static void print_summary(struct AppContext *context,
                          const struct timespec *start_time)
{
    struct CircularBufferStats buffer_stats;
    struct AppMetrics metrics;
    struct timespec now;
    long long uptime_seconds = 0;
    bool failed;
    char error_message[160];

    app_get_metrics(context, &metrics, &failed, error_message,
                    sizeof(error_message));
    (void)circular_buffer_get_stats(context->buffer, &buffer_stats);
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        uptime_seconds = (long long)(now.tv_sec - start_time->tv_sec) +
                         (now.tv_nsec < start_time->tv_nsec ? -1 : 0);
    }

    printf("SMS summary: uptime=%lld s processed=%llu buffered=%llu consumed=%llu "
           "buffer_overflow=%llu alerts={TEMP_HIGH:%llu,HUMID_HIGH:%llu,"
           "TEMP_SPIKE:%llu}\n",
           uptime_seconds,
           (unsigned long long)metrics.processed_samples,
           (unsigned long long)buffer_stats.total_produced,
           (unsigned long long)buffer_stats.total_consumed,
           (unsigned long long)buffer_stats.overflow_count,
           (unsigned long long)metrics.temperature_alerts,
           (unsigned long long)metrics.humidity_alerts,
           (unsigned long long)metrics.temperature_spikes);
    if (failed)
        fprintf(stderr, "sms_app stopped after error: %s\n", error_message);
}

int main(int argc, char **argv)
{
    struct AppConfig config;
    struct AppContext context;
    struct timespec start_time;
    sigset_t signal_set;
    struct sigaction wake_action;
    pthread_t processor_thread;
    pthread_t sensor_thread;
    pthread_t alert_thread;
    pthread_t logger_thread;
    bool processor_started = false;
    bool sensor_started = false;
    bool alert_started = false;
    bool logger_started = false;
    bool memory_locked = false;
    int error;
    int exit_status = EXIT_FAILURE;

    error = parse_options(argc, argv, &config);
    if (error != 0) {
        print_usage(argv[0]);
        fprintf(stderr, "Invalid command-line options: %s\n",
                strerror(error));
        return EXIT_FAILURE;
    }

    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    error = pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
    if (error != 0) {
        fprintf(stderr, "Cannot block SIGINT: %s\n", strerror(error));
        return EXIT_FAILURE;
    }

    memset(&wake_action, 0, sizeof(wake_action));
    wake_action.sa_handler = sensor_wakeup_handler;
    sigemptyset(&wake_action.sa_mask);
    if (sigaction(SIGUSR1, &wake_action, NULL) != 0) {
        fprintf(stderr, "Cannot install sensor wakeup handler: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    error = app_context_init(&context, &config);
    if (error != 0) {
        fprintf(stderr, "Cannot initialize SMS application: %s\n",
                strerror(error));
        return EXIT_FAILURE;
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        memory_locked = true;
        fprintf(stderr, "[RT] memory locking enabled\n");
    } else {
        fprintf(stderr, "[RT] mlockall unavailable: %s; continuing unlocked\n",
                strerror(errno));
    }

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        goto cleanup;
    }

    error = create_worker_thread(&logger_thread, logger_thread_main, &context,
                                 &memory_locked);
    if (error != 0) {
        fprintf(stderr, "Cannot start logger thread: %s\n", strerror(error));
        app_fail(&context, "cannot start logger thread");
        goto shutdown_threads;
    }
    logger_started = true;

    error = create_worker_thread(&alert_thread, alert_thread_main, &context,
                                 &memory_locked);
    if (error != 0) {
        fprintf(stderr, "Cannot start alert thread: %s\n", strerror(error));
        app_fail(&context, "cannot start alert thread");
        goto shutdown_threads;
    }
    alert_started = true;

    error = create_worker_thread(&processor_thread, processor_thread_main,
                                 &context, &memory_locked);
    if (error != 0) {
        fprintf(stderr, "Cannot start processor thread: %s\n",
                strerror(error));
        app_fail(&context, "cannot start processor thread");
        goto shutdown_threads;
    }
    processor_started = true;

    error = create_worker_thread(&sensor_thread, sensor_thread_main, &context,
                                 &memory_locked);
    if (error != 0) {
        fprintf(stderr, "Cannot start sensor thread: %s\n",
                strerror(error));
        app_fail(&context, "cannot start sensor thread");
        goto shutdown_threads;
    }
    sensor_started = true;

    printf("SMS application running: interval=%u ms, duration=%u s\n",
           config.interval_ms, config.duration_seconds);
    error = wait_for_stop_or_signal(&context, &signal_set, &start_time);
    if (error != 0)
        app_fail(&context, "wait for shutdown signal failed");

shutdown_threads:
    app_request_stop(&context);

    if (sensor_started) {
        int signal_error = pthread_kill(sensor_thread, SIGUSR1);

        if (signal_error != 0 && signal_error != ESRCH)
            fprintf(stderr, "Cannot wake sensor thread: %s\n",
                    strerror(signal_error));
    }

    if (sensor_started) {
        int join_error = pthread_join(sensor_thread, NULL);

        if (join_error != 0)
            fprintf(stderr, "Cannot join sensor thread: %s\n",
                    strerror(join_error));
    }

    if (processor_started) {
        int join_error = pthread_join(processor_thread, NULL);

        if (join_error != 0)
            fprintf(stderr, "Cannot join processor thread: %s\n",
                    strerror(join_error));
    } else if (alert_started) {
        const struct AlarmMessage shutdown = {.type = ALARM_SHUTDOWN};
        int send_error = app_queue_send(context.alarm_queue, &shutdown,
                                        sizeof(shutdown));

        if (send_error != 0)
            app_fail(&context, "cannot stop alert thread after startup error");
    }

    if (alert_started) {
        int join_error = pthread_join(alert_thread, NULL);

        if (join_error != 0)
            fprintf(stderr, "Cannot join alert thread: %s\n",
                    strerror(join_error));
    } else if (logger_started) {
        const struct LogMessage shutdown = {.type = LOG_SHUTDOWN};
        int send_error = app_queue_send(context.log_queue, &shutdown,
                                        sizeof(shutdown));

        if (send_error != 0)
            app_fail(&context, "cannot stop logger thread after startup error");
    }

    if (logger_started) {
        int join_error = pthread_join(logger_thread, NULL);

        if (join_error != 0)
            fprintf(stderr, "Cannot join logger thread: %s\n",
                    strerror(join_error));
    }

    print_summary(&context, &start_time);
    {
        bool failed;

        app_get_metrics(&context, &(struct AppMetrics){0}, &failed, NULL, 0U);
        if (!failed && error == 0)
            exit_status = EXIT_SUCCESS;
    }

cleanup:
    error = app_context_destroy(&context);
    if (error != 0) {
        fprintf(stderr, "Cannot clean up application resources: %s\n",
                strerror(error));
        exit_status = EXIT_FAILURE;
    }
    if (memory_locked && munlockall() != 0) {
        fprintf(stderr, "Cannot unlock application memory: %s\n",
                strerror(errno));
        exit_status = EXIT_FAILURE;
    }
    return exit_status;
}
