/*
 * Purpose: Implement a bounded producer-consumer circular buffer.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include "circular_buffer.h"

#include <errno.h>
#include <stdlib.h>

struct CircularBuffer {
    struct SensorData *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
    uint64_t total_produced;
    uint64_t total_consumed;
    uint64_t overflow_count;
    bool closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
};

int circular_buffer_create(struct CircularBuffer **buffer, size_t capacity)
{
    struct CircularBuffer *created;
    int error;

    if (buffer == NULL || capacity == 0)
        return EINVAL;
    *buffer = NULL;

    created = calloc(1, sizeof(*created));
    if (created == NULL)
        return ENOMEM;

    created->items = calloc(capacity, sizeof(*created->items));
    if (created->items == NULL) {
        free(created);
        return ENOMEM;
    }

    error = pthread_mutex_init(&created->mutex, NULL);
    if (error != 0) {
        free(created->items);
        free(created);
        return error;
    }

    error = pthread_cond_init(&created->not_empty, NULL);
    if (error != 0) {
        pthread_mutex_destroy(&created->mutex);
        free(created->items);
        free(created);
        return error;
    }

    created->capacity = capacity;
    *buffer = created;
    return 0;
}

void circular_buffer_close(struct CircularBuffer *buffer)
{
    if (buffer == NULL)
        return;

    pthread_mutex_lock(&buffer->mutex);
    buffer->closed = true;
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_mutex_unlock(&buffer->mutex);
}

void circular_buffer_destroy(struct CircularBuffer *buffer)
{
    if (buffer == NULL)
        return;

    circular_buffer_close(buffer);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mutex);
    free(buffer->items);
    free(buffer);
}

int circular_buffer_push(struct CircularBuffer *buffer,
                         const struct SensorData *sample)
{
    int result = 0;

    if (buffer == NULL || sample == NULL)
        return EINVAL;

    pthread_mutex_lock(&buffer->mutex);
    if (buffer->closed) {
        result = ECANCELED;
    } else if (buffer->size == buffer->capacity) {
        buffer->overflow_count++;
        result = ENOSPC;
    } else {
        buffer->items[buffer->head] = *sample;
        buffer->head = (buffer->head + 1U) % buffer->capacity;
        buffer->size++;
        buffer->total_produced++;
        pthread_cond_signal(&buffer->not_empty);
    }
    pthread_mutex_unlock(&buffer->mutex);
    return result;
}

int circular_buffer_pop(struct CircularBuffer *buffer,
                        struct SensorData *sample)
{
    if (buffer == NULL || sample == NULL)
        return EINVAL;

    pthread_mutex_lock(&buffer->mutex);
    while (buffer->size == 0 && !buffer->closed)
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);

    if (buffer->size == 0) {
        pthread_mutex_unlock(&buffer->mutex);
        return ECANCELED;
    }

    *sample = buffer->items[buffer->tail];
    buffer->tail = (buffer->tail + 1U) % buffer->capacity;
    buffer->size--;
    buffer->total_consumed++;
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

int circular_buffer_get_stats(struct CircularBuffer *buffer,
                              struct CircularBufferStats *stats)
{
    if (buffer == NULL || stats == NULL)
        return EINVAL;

    pthread_mutex_lock(&buffer->mutex);
    stats->current_size = buffer->size;
    stats->total_produced = buffer->total_produced;
    stats->total_consumed = buffer->total_consumed;
    stats->overflow_count = buffer->overflow_count;
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}
