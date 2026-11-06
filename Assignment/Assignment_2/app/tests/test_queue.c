/*
 * Purpose: Validate POSIX queue full and receive-timeout error paths.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#define _POSIX_C_SOURCE 200809L
#include "message_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct mq_attr attributes;
    struct LogMessage message = {.type = LOG_SAMPLE};
    struct LogMessage received;
    char name[64];
    mqd_t queue;
    int written;
    long index;

    written = snprintf(name, sizeof(name), "/sms_queue_test_%ld",
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(name)) {
        fprintf(stderr, "FAIL: format queue name\n");
        return EXIT_FAILURE;
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = APP_MQ_MAX_MESSAGES;
    attributes.mq_msgsize = (long)sizeof(message);
    queue = mq_open(name, O_CREAT | O_EXCL | O_RDWR, 0600, &attributes);
    if (queue == (mqd_t)-1) {
        fprintf(stderr, "FAIL: mq_open: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    for (index = 0; index < APP_MQ_MAX_MESSAGES; index++) {
        if (mq_send(queue, (const char *)&message, sizeof(message), 0) != 0) {
            fprintf(stderr, "FAIL: fill queue: %s\n", strerror(errno));
            mq_close(queue);
            mq_unlink(name);
            return EXIT_FAILURE;
        }
    }

    if (app_queue_send(queue, &message, sizeof(message)) != ETIMEDOUT) {
        fprintf(stderr, "FAIL: full queue send did not time out\n");
        mq_close(queue);
        mq_unlink(name);
        return EXIT_FAILURE;
    }
    puts("PASS: bounded send reports timeout for full queue");

    for (index = 0; index < APP_MQ_MAX_MESSAGES; index++) {
        if (app_queue_receive(queue, &received, sizeof(received)) != 0) {
            fprintf(stderr, "FAIL: receive queued message\n");
            mq_close(queue);
            mq_unlink(name);
            return EXIT_FAILURE;
        }
    }

    if (app_queue_receive(queue, &received, sizeof(received)) != ETIMEDOUT) {
        fprintf(stderr, "FAIL: empty queue receive did not time out\n");
        mq_close(queue);
        mq_unlink(name);
        return EXIT_FAILURE;
    }
    puts("PASS: empty queue receive timeout");

    if (mq_close(queue) != 0 || mq_unlink(name) != 0) {
        fprintf(stderr, "FAIL: queue cleanup: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    puts("PASS: queue close and unlink");
    return EXIT_SUCCESS;
}
