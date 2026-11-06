/*
 * Purpose: Read, validate, and enqueue SMS sensor CSV samples.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#define _POSIX_C_SOURCE 200809L
#include "sensor_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "sms_sensor.h"

#define SENSOR_LINE_CAPACITY 128U
#define JITTER_SAMPLE_COUNT 1000U

struct JitterSample {
    int64_t deadline_ns;
    int64_t wakeup_ns;
    int64_t jitter_ns;
};

struct JitterMeasurement {
    FILE *csv;
    struct JitterSample samples[JITTER_SAMPLE_COUNT];
    size_t count;
    size_t skipped_deadlines;
};

static int64_t timespec_to_nanoseconds(const struct timespec *time)
{
    return (int64_t)time->tv_sec * 1000000000LL + time->tv_nsec;
}

static int compare_int64(const void *left, const void *right)
{
    const int64_t a = *(const int64_t *)left;
    const int64_t b = *(const int64_t *)right;

    return (a > b) - (a < b);
}

static int record_jitter(struct JitterMeasurement *measurement,
                         const struct timespec *deadline,
                         const struct timespec *wakeup)
{
    struct JitterSample *sample;

    if (measurement->csv == NULL ||
        measurement->count >= JITTER_SAMPLE_COUNT)
        return 0;

    sample = &measurement->samples[measurement->count];
    sample->deadline_ns = timespec_to_nanoseconds(deadline);
    sample->wakeup_ns = timespec_to_nanoseconds(wakeup);
    sample->jitter_ns = sample->wakeup_ns - sample->deadline_ns;

    if (fprintf(measurement->csv, "%zu,%" PRId64 ",%" PRId64 ",%" PRId64
                "\n",
                measurement->count + 1U, sample->deadline_ns, sample->wakeup_ns,
                sample->jitter_ns) < 0) {
        int error = errno != 0 ? errno : EIO;

        fprintf(stderr, "[SENSOR] cannot write jitter CSV: %s\n",
                strerror(error));
        return error;
    }
    measurement->count++;
    return 0;
}

static void report_jitter(struct JitterMeasurement *measurement)
{
    int64_t sorted[JITTER_SAMPLE_COUNT];
    int64_t minimum;
    int64_t maximum;
    int64_t sum = 0;
    size_t histogram[4] = {0};
    size_t early = 0;
    size_t i;

    if (measurement->csv == NULL)
        return;

    for (i = 0; i < measurement->count; i++) {
        int64_t jitter = measurement->samples[i].jitter_ns;

        sorted[i] = jitter;
        sum += jitter;
        if (jitter < 0) {
            early++;
        } else if (jitter < 100000LL) {
            histogram[0]++;
        } else if (jitter < 500000LL) {
            histogram[1]++;
        } else if (jitter < 1000000LL) {
            histogram[2]++;
        } else {
            histogram[3]++;
        }
    }

    if (measurement->count > 0U) {
        qsort(sorted, measurement->count, sizeof(sorted[0]), compare_int64);
        minimum = sorted[0];
        maximum = sorted[measurement->count - 1U];
        fprintf(stderr,
                "[JITTER] samples=%zu min_ns=%" PRId64
                " avg_ns=%.2f max_ns=%" PRId64 " p99_ns=%" PRId64 "\n",
                measurement->count, minimum,
                (double)sum / (double)measurement->count, maximum,
                sorted[(measurement->count * 99U + 99U) / 100U - 1U]);
    } else {
        fprintf(stderr, "[JITTER] samples=0; required=%u\n",
                JITTER_SAMPLE_COUNT);
    }

    fprintf(stderr,
            "[JITTER] histogram_ns: 0-100us=%zu 100-500us=%zu "
            "500-1000us=%zu >1ms=%zu early=%zu\n",
            histogram[0], histogram[1], histogram[2], histogram[3], early);
    fprintf(stderr, "[JITTER] skipped_overdue_deadlines=%zu\n",
            measurement->skipped_deadlines);
    if (measurement->count < JITTER_SAMPLE_COUNT)
        fprintf(stderr, "[JITTER] INCOMPLETE required=%u actual=%zu\n",
                JITTER_SAMPLE_COUNT, measurement->count);
}

static int finish_jitter(struct JitterMeasurement *measurement)
{
    int error = 0;

    if (measurement->csv == NULL)
        return 0;

    report_jitter(measurement);
    if (fclose(measurement->csv) != 0) {
        error = errno;
        fprintf(stderr, "[SENSOR] cannot close jitter CSV: %s\n",
                strerror(error));
    }
    measurement->csv = NULL;
    return error;
}

static int parse_sensor_line(char *line, struct SensorData *sample)
{
    char *cursor;
    char *end;
    long long timestamp;
    double temperature;
    double humidity;

    if (line == NULL || sample == NULL)
        return EINVAL;

    end = strchr(line, '\n');
    if (end == NULL)
        return EPROTO;
    *end = '\0';
    if (end[1] != '\0')
        return EPROTO;

    errno = 0;
    timestamp = strtoll(line, &cursor, 10);
    if (errno != 0 || cursor == line || *cursor != ',')
        return EPROTO;

    errno = 0;
    temperature = strtod(cursor + 1, &end);
    if (errno != 0 || end == cursor + 1 || *end != ',')
        return EPROTO;

    errno = 0;
    humidity = strtod(end + 1, &cursor);
    if (errno != 0 || cursor == end + 1 || *cursor != '\0')
        return EPROTO;

    if (timestamp < 0 || !isfinite(temperature) || !isfinite(humidity) ||
        temperature < 15.0 || temperature > 45.0 ||
        humidity < 30.0 || humidity > 90.0)
        return ERANGE;

    sample->timestamp_ms = timestamp;
    sample->temperature = temperature;
    sample->humidity = humidity;
    return 0;
}

static int read_sensor_sample(int fd, struct AppContext *context,
                              struct SensorData *sample)
{
    char line[SENSOR_LINE_CAPACITY];
    size_t used = 0;

    while (used + 1U < sizeof(line)) {
        ssize_t length;
        char *newline;

        if (app_should_stop(context))
            return ECANCELED;

        length = read(fd, line + used, sizeof(line) - used - 1U);
        if (length < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec retry = {.tv_sec = 0, .tv_nsec = 10000000L};

                while (nanosleep(&retry, &retry) != 0 && errno == EINTR) {
                    if (app_should_stop(context))
                        return ECANCELED;
                }
                continue;
            }
            return errno;
        }
        if (length == 0)
            return ENODATA;

        used += (size_t)length;
        newline = memchr(line, '\n', used);
        if (newline != NULL) {
            if (newline != line + used - 1U)
                return EPROTO;
            line[used] = '\0';
            return parse_sensor_line(line, sample);
        }
    }

    return EMSGSIZE;
}

static void add_milliseconds(struct timespec *time, unsigned int interval_ms)
{
    time->tv_sec += (time_t)(interval_ms / 1000U);
    time->tv_nsec += (long)(interval_ms % 1000U) * 1000000L;
    if (time->tv_nsec >= 1000000000L) {
        time->tv_sec++;
        time->tv_nsec -= 1000000000L;
    }
}

static int prepare_next_deadline(struct timespec *deadline,
                                 unsigned int interval_ms,
                                 size_t *skipped_deadlines)
{
    struct timespec now;

    add_milliseconds(deadline, interval_ms);
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return errno;

    while (deadline->tv_sec < now.tv_sec ||
           (deadline->tv_sec == now.tv_sec &&
            deadline->tv_nsec <= now.tv_nsec)) {
        add_milliseconds(deadline, interval_ms);
        (*skipped_deadlines)++;
    }

    return 0;
}

static int wait_until(struct AppContext *context,
                      const struct timespec *deadline,
                      struct timespec *wakeup)
{
    int error;

    do {
        if (app_should_stop(context))
            return ECANCELED;
        error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL);
    } while (error == EINTR);

    if (error == 0 && wakeup != NULL &&
        clock_gettime(CLOCK_MONOTONIC, wakeup) != 0)
        return errno;
    return error;
}

static int configure_sensor_scheduler(struct AppContext *context)
{
    struct sched_param parameters = {
        .sched_priority = context->config.sensor_use_fifo ? 50 : 0
    };
    int requested_policy = context->config.sensor_use_fifo
                              ? SCHED_FIFO : SCHED_OTHER;
    int actual_policy;
    int error;

    error = pthread_setschedparam(pthread_self(), requested_policy,
                                  &parameters);
    if (error != 0 && requested_policy == SCHED_FIFO) {
        fprintf(stderr,
                "[SENSOR] SCHED_FIFO priority=50 unavailable: %s; "
                "falling back to SCHED_OTHER\n",
                strerror(error));
        requested_policy = SCHED_OTHER;
        parameters.sched_priority = 0;
        error = pthread_setschedparam(pthread_self(), requested_policy,
                                      &parameters);
    }
    if (error != 0) {
        char message[160];

        (void)snprintf(message, sizeof(message),
                       "cannot configure SCHED_OTHER fallback: %s",
                       strerror(error));
        app_fail(context, message);
        return error;
    }

    error = pthread_getschedparam(pthread_self(), &actual_policy, &parameters);
    if (error != 0) {
        app_fail(context, "cannot verify sensor scheduling policy");
        return error;
    }
    fprintf(stderr, "[SENSOR] scheduler=%s priority=%d\n",
            actual_policy == SCHED_FIFO ? "SCHED_FIFO" : "SCHED_OTHER",
            parameters.sched_priority);
    return 0;
}

void *sensor_thread_main(void *argument)
{
    struct AppContext *context = argument;
    struct timespec deadline;
    unsigned int interval_ms = context->config.interval_ms;
    unsigned int consecutive_read_errors = 0;
    struct JitterMeasurement measurement = {0};
    int fd;
    int error;

    error = configure_sensor_scheduler(context);
    if (error != 0)
        return NULL;

    fd = open(context->config.device_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        char message[160];

        (void)snprintf(message, sizeof(message), "cannot open %s: %s",
                       context->config.device_path, strerror(errno));
        app_fail(context, message);
        return NULL;
    }

    if (ioctl(fd, SMS_SET_INTERVAL, &interval_ms) != 0) {
        char message[160];

        (void)snprintf(message, sizeof(message),
                       "SMS_SET_INTERVAL failed: %s", strerror(errno));
        app_fail(context, message);
        goto cleanup;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        app_fail(context, "clock_gettime(CLOCK_MONOTONIC) failed");
        goto cleanup;
    }

    if (context->config.jitter_csv_path != NULL) {
        measurement.csv = fopen(context->config.jitter_csv_path, "w");
        if (measurement.csv == NULL) {
            char message[160];

            (void)snprintf(message, sizeof(message),
                           "cannot open jitter CSV: %s", strerror(errno));
            app_fail(context, message);
            goto cleanup;
        }
        if (fprintf(measurement.csv,
                    "sample,deadline_ns,wakeup_ns,jitter_ns\n") < 0) {
            app_fail(context, "cannot write jitter CSV header");
            goto cleanup;
        }
    }

    while (!app_should_stop(context)) {
        struct SensorData sample;
        struct timespec wakeup;

        if (measurement.csv != NULL) {
            error = prepare_next_deadline(&deadline,
                                          context->config.interval_ms,
                                          &measurement.skipped_deadlines);
            if (error == 0)
                error = wait_until(context, &deadline, &wakeup);
            if (error != 0 && error != ECANCELED) {
                char message[160];

                (void)snprintf(message, sizeof(message),
                               "sensor period wait failed: %s",
                               strerror(error));
                app_fail(context, message);
                break;
            }
            if (error == ECANCELED)
                break;

            error = record_jitter(&measurement, &deadline, &wakeup);
            if (error != 0) {
                app_fail(context, "cannot record sensor wake-up jitter");
                break;
            }
        }

        error = read_sensor_sample(fd, context, &sample);
        if (error == 0) {
            int push_error = circular_buffer_push(context->buffer, &sample);

            consecutive_read_errors = 0;
            if (push_error == ENOSPC) {
                fprintf(stderr,
                        "[SENSOR] shared buffer full; dropping sample "
                        "timestamp=%lld\n",
                        sample.timestamp_ms);
            } else if (push_error == ECANCELED) {
                break;
            } else if (push_error != 0) {
                app_fail(context, "cannot enqueue sensor sample");
                break;
            } else if (context->config.verbose) {
                printf("[SENSOR] ts=%lld temperature=%.1f humidity=%.1f\n",
                       sample.timestamp_ms, sample.temperature,
                       sample.humidity);
            }
        } else if (error == ECANCELED) {
            break;
        } else if (error == ENODATA || error == EAGAIN ||
                   error == EWOULDBLOCK || error == EINTR) {
            fprintf(stderr, "[SENSOR] temporary read condition: %s\n",
                    error == ENODATA ? "EOF" : strerror(error));
            consecutive_read_errors++;
            if (consecutive_read_errors >= 5U) {
                app_fail(context, "sensor read failed repeatedly");
                break;
            }
        } else {
            char message[160];

            (void)snprintf(message, sizeof(message),
                           "invalid sensor data/read failure: %s",
                           strerror(error));
            app_fail(context, message);
            break;
        }

        if (measurement.csv == NULL) {
            add_milliseconds(&deadline, context->config.interval_ms);
            error = wait_until(context, &deadline, NULL);
            if (error != 0 && error != ECANCELED) {
                char message[160];

                (void)snprintf(message, sizeof(message),
                               "sensor period wait failed: %s",
                               strerror(error));
                app_fail(context, message);
                break;
            }
            if (error == ECANCELED)
                break;
        }
    }

cleanup:
    close(fd);
    error = finish_jitter(&measurement);
    if (error != 0)
        app_fail(context, "cannot finalize jitter CSV");
    return NULL;
}
