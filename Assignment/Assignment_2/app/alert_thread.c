/*
 * Purpose: Receive alarm events, report them, and forward them to logger.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include "alert_thread.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "message_queue.h"
#include "time_format.h"

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

static double alarm_value(const struct AlarmMessage *message)
{
    if (message->type == ALARM_HUMID_HIGH)
        return message->sample.humidity;
    return message->sample.temperature;
}

static int forward_alert_to_logger(struct AppContext *context,
                                   const struct AlarmMessage *alarm)
{
    struct LogMessage log_message = {
        .type = LOG_ALERT,
        .sample = alarm->sample,
        .alarm_type = alarm->type,
        .threshold = alarm->threshold
    };

    return app_queue_send(context->log_queue, &log_message,
                          sizeof(log_message));
}

static void send_logger_shutdown(struct AppContext *context)
{
    const struct LogMessage shutdown = {.type = LOG_SHUTDOWN};
    int error = app_queue_send(context->log_queue, &shutdown,
                               sizeof(shutdown));

    if (error != 0)
        app_fail(context, "cannot send logger shutdown message");
}

void *alert_thread_main(void *argument)
{
    struct AppContext *context = argument;
    bool shutdown_received = false;

    while (!shutdown_received) {
        struct AlarmMessage message;
        char timestamp[40];
        int error = app_queue_receive(context->alarm_queue, &message,
                                      sizeof(message));

        if (error == ETIMEDOUT || error == EINTR)
            continue;
        if (error != 0) {
            app_fail(context, "cannot receive alarm queue message");
            break;
        }

        if (message.type == ALARM_SHUTDOWN) {
            shutdown_received = true;
            continue;
        }

        if (message.type < ALARM_TEMP_HIGH ||
            message.type > ALARM_TEMP_SPIKE) {
            app_fail(context, "invalid alarm message type");
            continue;
        }

        if (format_realtime_timestamp(timestamp, sizeof(timestamp)) != 0) {
            app_fail(context, "cannot format alert timestamp");
            continue;
        }

        if (fprintf(stderr,
                    "[ALERT][%s] TYPE: %s VALUE: %.1f THRESHOLD: %.1f\n",
                    timestamp, alarm_name(message.type),
                    alarm_value(&message), message.threshold) < 0) {
            app_fail(context, "cannot write alert to stderr");
        }
        app_record_alert(context, message.type);

        error = forward_alert_to_logger(context, &message);
        if (error != 0)
            app_fail(context, "cannot enqueue alert log message");
    }

    send_logger_shutdown(context);
    return NULL;
}
