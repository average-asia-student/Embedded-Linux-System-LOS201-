/*
 * Purpose: Write queued SMS samples and alerts with periodic flush and rotate.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#define _POSIX_C_SOURCE 200809L
#include "logger_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_messages.h"
#include "message_queue.h"
#include "time_format.h"

#define LOGGER_ROTATE_BYTES (1024L * 1024L)
#define LOGGER_ROTATED_PATH_CAPACITY 4096U

static const char *alarm_name(enum AlarmType type)
{
    switch (type) {
    case ALARM_TEMP_HIGH:
        return "TEMP_HIGH";
    case ALARM_HUMID_HIGH:
        return "HUMID_HIGH";
    case ALARM_TEMP_SPIKE:
        return "TEMP_SPIKE";
    case ALARM_SHUTDOWN:
        break;
    }
    return "UNKNOWN";
}

static int rotate_log(FILE **stream, const char *path)
{
    char rotated_path[LOGGER_ROTATED_PATH_CAPACITY];
    int written;
    int fd;

    written = snprintf(rotated_path, sizeof(rotated_path), "%s.1", path);
    if (written < 0 || (size_t)written >= sizeof(rotated_path))
        return ENAMETOOLONG;

    if (fflush(*stream) != 0)
        return errno;
    if (fclose(*stream) != 0) {
        *stream = NULL;
        return errno;
    }
    *stream = NULL;

    if (rename(path, rotated_path) != 0)
        return errno;

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
        return errno;

    *stream = fdopen(fd, "a");
    if (*stream == NULL) {
        int error = errno;

        close(fd);
        return error;
    }
    return 0;
}

static int write_message(FILE **stream, const char *path,
                         const struct LogMessage *message,
                         off_t *file_size)
{
    char timestamp[40];
    int result;

    if (*stream == NULL)
        return EIO;
    if (format_realtime_timestamp(timestamp, sizeof(timestamp)) != 0)
        return EIO;

    if (message->type == LOG_SAMPLE) {
        result = fprintf(
            *stream,
            "[%s] SAMPLE ts_ms=%lld temperature=%.1f humidity=%.1f "
            "temp_avg=%.2f humidity_avg=%.2f\n",
            timestamp, message->sample.timestamp_ms,
            message->sample.temperature, message->sample.humidity,
            message->temperature_average, message->humidity_average);
    } else if (message->type == LOG_ALERT) {
        double value = message->alarm_type == ALARM_HUMID_HIGH
                           ? message->sample.humidity
                           : message->sample.temperature;

        result = fprintf(
            *stream,
            "[%s] ALERT type=%s ts_ms=%lld value=%.1f threshold=%.1f\n",
            timestamp, alarm_name(message->alarm_type),
            message->sample.timestamp_ms, value, message->threshold);
    } else {
        return EINVAL;
    }

    if (result < 0)
        return errno != 0 ? errno : EIO;

    *file_size += (off_t)result;
    if (*file_size > LOGGER_ROTATE_BYTES) {
        int error = rotate_log(stream, path);

        if (error != 0)
            return error;
        *file_size = 0;
    }
    return 0;
}

static int flush_log(FILE *stream)
{
    if (stream == NULL)
        return 0;
    if (fflush(stream) != 0)
        return errno != 0 ? errno : EIO;
    return 0;
}

static int close_log(FILE **stream)
{
    int error = 0;

    if (*stream == NULL)
        return 0;
    if (fflush(*stream) != 0) {
        error = errno != 0 ? errno : EIO;
        fprintf(stderr, "sms_app: final log flush failed: %s\n",
                strerror(errno));
    }
    if (fclose(*stream) != 0) {
        if (error == 0)
            error = errno != 0 ? errno : EIO;
        fprintf(stderr, "sms_app: log close failed: %s\n", strerror(errno));
    }
    *stream = NULL;
    return error;
}

void *logger_thread_main(void *argument)
{
    struct AppContext *context = argument;
    FILE *stream;
    struct timespec last_flush;
    off_t file_size = 0;
    int fd;
    int error = 0;
    bool file_failed = false;

    fd = open(context->config.log_path,
              O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        app_fail(context, "cannot open SMS log file");
        stream = NULL;
        file_failed = true;
    } else {
        struct stat status;

        stream = fdopen(fd, "a");
        if (stream == NULL) {
            error = errno;
            close(fd);
            app_fail(context, strerror(error));
            file_failed = true;
        } else if (fstat(fd, &status) == 0) {
            file_size = status.st_size;
            if (file_size > LOGGER_ROTATE_BYTES) {
                error = rotate_log(&stream, context->config.log_path);
                if (error != 0) {
                    app_fail(context, "cannot rotate oversized SMS log");
                    file_failed = true;
                    (void)close_log(&stream);
                } else {
                    file_size = 0;
                }
            }
        } else {
            app_fail(context, "cannot inspect SMS log file");
            file_failed = true;
            (void)close_log(&stream);
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &last_flush) != 0) {
        app_fail(context, "cannot read monotonic clock in logger");
        last_flush.tv_sec = 0;
        last_flush.tv_nsec = 0;
    }

    for (;;) {
        struct LogMessage message;

        error = app_queue_receive(context->log_queue, &message,
                                  sizeof(message));
        if (error == ETIMEDOUT || error == EINTR) {
            error = 0;
        } else if (error != 0) {
            app_fail(context, "cannot receive logger queue message");
            continue;
        } else {
            if (message.type == LOG_SHUTDOWN)
                break;

            if (!file_failed) {
                error = write_message(&stream, context->config.log_path,
                                      &message, &file_size);
                if (error != 0) {
                    char error_message[160];

                    (void)snprintf(error_message, sizeof(error_message),
                                   "SMS log write failed: %s",
                                   strerror(error));
                    app_fail(context, error_message);
                    file_failed = true;
                    (void)close_log(&stream);
                }
            }
        }

        {
            struct timespec now;
            long long elapsed_ms;

            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                app_fail(context, "cannot read logger flush clock");
                continue;
            }
            elapsed_ms = (long long)(now.tv_sec - last_flush.tv_sec) * 1000LL +
                         (now.tv_nsec - last_flush.tv_nsec) / 1000000LL;
            if (elapsed_ms >= 5000LL) {
                error = flush_log(stream);
                if (error != 0) {
                    app_fail(context, "periodic log flush failed");
                    file_failed = true;
                    (void)close_log(&stream);
                }
                last_flush = now;
            }
        }
    }

    error = close_log(&stream);
    if (error != 0)
        app_fail(context, "final SMS log flush/close failed");
    return NULL;
}
