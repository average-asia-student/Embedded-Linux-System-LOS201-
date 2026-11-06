/*
 * Purpose: Consume samples and compute rolling averages and alert conditions.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include "processor_thread.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include "message_queue.h"

static int send_sample_log(struct AppContext *context,
                           const struct SensorData *sample,
                           const struct ProcessorResult *result)
{
    struct LogMessage message = {
        .type = LOG_SAMPLE,
        .sample = *sample,
        .temperature_average = result->rolling_temperature_average,
        .humidity_average = result->rolling_humidity_average
    };

    return app_queue_send(context->log_queue, &message, sizeof(message));
}

static int send_sample_alarms(struct AppContext *context,
                              const struct SensorData *sample,
                              unsigned int alert_flags)
{
    struct AlarmMessage message = {.sample = *sample};
    int error;

    if ((alert_flags & PROCESSOR_ALERT_TEMP_HIGH) != 0) {
        message.type = ALARM_TEMP_HIGH;
        message.threshold = context->config.temperature_threshold;
        error = app_queue_send(context->alarm_queue, &message,
                               sizeof(message));
        if (error != 0)
            return error;
    }

    if ((alert_flags & PROCESSOR_ALERT_HUMID_HIGH) != 0) {
        message.type = ALARM_HUMID_HIGH;
        message.threshold = context->config.humidity_threshold;
        error = app_queue_send(context->alarm_queue, &message,
                               sizeof(message));
        if (error != 0)
            return error;
    }

    if ((alert_flags & PROCESSOR_ALERT_TEMP_SPIKE) != 0) {
        message.type = ALARM_TEMP_SPIKE;
        message.threshold = 5.0;
        error = app_queue_send(context->alarm_queue, &message,
                               sizeof(message));
        if (error != 0)
            return error;
    }

    return 0;
}

static void send_alert_shutdown(struct AppContext *context)
{
    const struct AlarmMessage shutdown = {.type = ALARM_SHUTDOWN};
    int error = app_queue_send(context->alarm_queue, &shutdown,
                               sizeof(shutdown));

    if (error != 0)
        app_fail(context, "cannot send alert shutdown message");
}

void *processor_thread_main(void *argument)
{
    struct AppContext *context = argument;
    struct ProcessorState state;

    processor_state_init(&state);

    for (;;) {
        struct SensorData sample;
        struct ProcessorResult result;
        int error;

        error = circular_buffer_pop(context->buffer, &sample);
        if (error == ECANCELED)
            break;
        if (error != 0) {
            app_fail(context, "circular buffer consumer failed");
            break;
        }

        error = processor_process_sample(
            &state, &sample, context->config.temperature_threshold,
            context->config.humidity_threshold, &result);
        if (error != 0) {
            app_fail(context, "sensor sample processing failed");
            break;
        }

        app_record_sample_processed(context);
        error = send_sample_log(context, &sample, &result);
        if (error == 0)
            error = send_sample_alarms(context, &sample, result.alert_flags);
        if (error != 0) {
            app_fail(context, "cannot enqueue processor message");
            continue;
        }
        if (context->config.verbose) {
            printf("[PROCESSOR] ts=%lld temp_avg=%.2f humidity_avg=%.2f "
                   "alert_flags=0x%x\n",
                   sample.timestamp_ms, result.rolling_temperature_average,
                   result.rolling_humidity_average, result.alert_flags);
        }
    }

    send_alert_shutdown(context);
    return NULL;
}
