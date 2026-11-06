/*
 * Purpose: Calculate rolling averages and detect SMS sensor alerts.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include "processor_core.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

void processor_state_init(struct ProcessorState *state)
{
    if (state != NULL)
        memset(state, 0, sizeof(*state));
}

int processor_process_sample(struct ProcessorState *state,
                             const struct SensorData *sample,
                             double temperature_threshold,
                             double humidity_threshold,
                             struct ProcessorResult *result)
{
    double temperature_sum = 0.0;
    double humidity_sum = 0.0;
    unsigned int index;

    if (state == NULL || sample == NULL || result == NULL ||
        !isfinite(sample->temperature) || !isfinite(sample->humidity) ||
        !isfinite(temperature_threshold) || !isfinite(humidity_threshold))
        return EINVAL;

    state->temperatures[state->next_index] = sample->temperature;
    state->humidities[state->next_index] = sample->humidity;
    state->next_index = (state->next_index + 1U) % PROCESSOR_WINDOW_SIZE;
    if (state->sample_count < PROCESSOR_WINDOW_SIZE)
        state->sample_count++;

    for (index = 0; index < state->sample_count; index++) {
        temperature_sum += state->temperatures[index];
        humidity_sum += state->humidities[index];
    }

    result->rolling_temperature_average =
        temperature_sum / (double)state->sample_count;
    result->rolling_humidity_average =
        humidity_sum / (double)state->sample_count;
    result->alert_flags = 0;

    if (sample->temperature > temperature_threshold)
        result->alert_flags |= PROCESSOR_ALERT_TEMP_HIGH;
    if (sample->humidity > humidity_threshold)
        result->alert_flags |= PROCESSOR_ALERT_HUMID_HIGH;
    if (state->has_previous_temperature &&
        fabs(sample->temperature - state->previous_temperature) > 5.0)
        result->alert_flags |= PROCESSOR_ALERT_TEMP_SPIKE;

    state->previous_temperature = sample->temperature;
    state->has_previous_temperature = true;
    return 0;
}
