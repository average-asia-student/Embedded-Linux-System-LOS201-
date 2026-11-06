#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_RECORDS 100
#define MSG_SIZE 256

typedef struct {
    int id;
    char *name;
    int *scores;
} Record;

Record *create_record(int id, const char *name, int num_scores) {
    Record *r = malloc(sizeof(Record));
    r->id = id;
    r->name = malloc(strlen(name) + 1);
    strcpy(r->name, name);
    r->scores = malloc(sizeof(int) * num_scores);
    for (int i = 0; i < num_scores; i++)
        r->scores[i] = id * 10 + i;
    return r;
}

void delete_record(Record *r) {
    free(r);
}

void process_messages(int count) {
    for (int i = 0; i < count; i++) {
        char *buf = malloc(MSG_SIZE);
        snprintf(buf, MSG_SIZE, "Message #%d: data payload here", i);
        if (i % 10 == 0)
            printf("  Processed: %s\n", buf);
    }
}

static int parse_hold_seconds(int argc, char **argv, unsigned int *seconds) {
    char *end;
    long value;

    *seconds = 0;
    if (argc == 1)
        return 0;
    if (argc != 3 || strcmp(argv[1], "--hold-seconds") != 0) {
        fprintf(stderr, "Usage: %s [--hold-seconds 1..3600]\n", argv[0]);
        return -1;
    }

    errno = 0;
    value = strtol(argv[2], &end, 10);
    if (errno != 0 || *argv[2] == '\0' || *end != '\0' || value < 1 || value > 3600) {
        fprintf(stderr, "Invalid hold duration: %s\n", argv[2]);
        return -1;
    }
    *seconds = (unsigned int)value;
    return 0;
}

int main(int argc, char *argv[]) {
    unsigned int hold_seconds;

    if (parse_hold_seconds(argc, argv, &hold_seconds) != 0)
        return EXIT_FAILURE;

    printf("=== Leaky App - LAB-03 Memory Analysis ===\n");

    printf("[1] Creating %d records...\n", NUM_RECORDS);
    Record **records = malloc(sizeof(Record *) * NUM_RECORDS);
    for (int i = 0; i < NUM_RECORDS; i++)
        records[i] = create_record(i, "EmbeddedLinuxStudent", 5);

    printf("[2] Deleting records (with leak)...\n");
    for (int i = 0; i < NUM_RECORDS; i++)
        delete_record(records[i]);
    free(records);

    printf("[3] Processing messages (with leak)...\n");
    process_messages(50);

    printf("Program finished - check valgrind output!\n");
    if (hold_seconds != 0) {
        printf("Holding process for %u seconds for /proc sampling (pid=%ld).\n",
               hold_seconds, (long)getpid());
        fflush(stdout);
        sleep(hold_seconds);
    }
    return 0;
}
