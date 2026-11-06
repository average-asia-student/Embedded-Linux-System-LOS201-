/*
 * Purpose: Shared application configuration and runtime interfaces.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#ifndef SMS_APP_H
#define SMS_APP_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <mqueue.h>

#include "app_messages.h"
#include "circular_buffer.h"
#include "processor_core.h"

#define SMS_SENSOR_DEVICE_PATH "/dev/sms_sensor"
#define SMS_LOG_FILE_PATH "/tmp/sms.log"

struct AppConfig {
    unsigned int interval_ms;
    double temperature_threshold;
    double humidity_threshold;
    unsigned int duration_seconds;
    bool sensor_use_fifo;
    bool verbose;
    const char *device_path;
    const char *log_path;
    const char *jitter_csv_path;
};

struct AppMetrics {
    uint64_t processed_samples;
    uint64_t temperature_alerts;
    uint64_t humidity_alerts;
    uint64_t temperature_spikes;
};

struct AppContext {
    struct AppConfig config;
    struct CircularBuffer *buffer;
    mqd_t alarm_queue;
    mqd_t log_queue;
    char alarm_queue_name[64];
    char log_queue_name[64];
    pthread_mutex_t state_mutex;
    bool stop_requested;
    bool failed;
    char error_message[160];
    struct AppMetrics metrics;
};

int app_context_init(struct AppContext *context,
                     const struct AppConfig *config);
int app_context_destroy(struct AppContext *context);
void app_request_stop(struct AppContext *context);
bool app_should_stop(struct AppContext *context);
void app_fail(struct AppContext *context, const char *message);
void app_record_sample_processed(struct AppContext *context);
void app_record_alert(struct AppContext *context, enum AlarmType type);
void app_get_metrics(struct AppContext *context, struct AppMetrics *metrics,
                     bool *failed, char *error_message,
                     unsigned int error_message_size);

#endif
