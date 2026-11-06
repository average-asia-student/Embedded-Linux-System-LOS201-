#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE 100000
#define DEFAULT_ITERATIONS 50
#define DEFAULT_SEARCH_RUNS 1000
#define DEFAULT_IO_LINES 10000

static int parse_count(const char *text, int minimum, int maximum, int *value) {
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int parse_options(int argc, char **argv, int *iterations,
                         int *search_runs, int *io_lines) {
    int *destination;

    *iterations = DEFAULT_ITERATIONS;
    *search_runs = DEFAULT_SEARCH_RUNS;
    *io_lines = DEFAULT_IO_LINES;

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return -1;
        }
        if (strcmp(argv[i], "--iterations") == 0) {
            destination = iterations;
            if (parse_count(argv[i + 1], 1, DEFAULT_ITERATIONS, destination) != 0)
                goto invalid;
        } else if (strcmp(argv[i], "--search-runs") == 0) {
            destination = search_runs;
            if (parse_count(argv[i + 1], 1, DEFAULT_SEARCH_RUNS, destination) != 0)
                goto invalid;
        } else if (strcmp(argv[i], "--io-lines") == 0) {
            destination = io_lines;
            if (parse_count(argv[i + 1], 1, DEFAULT_IO_LINES, destination) != 0)
                goto invalid;
        } else {
            fprintf(stderr,
                    "Usage: %s [--iterations 1..50] [--search-runs 1..1000] "
                    "[--io-lines 1..10000]\n",
                    argv[0]);
            return -1;
        }
        continue;

invalid:
        fprintf(stderr, "Invalid value for %s: %s\n", argv[i], argv[i + 1]);
        return -1;
    }
    return 0;
}

__attribute__((noinline))
double heavy_compute(const double *arr, int n) {
    double result = 0.0;
    for (int i = 0; i < n; i++)
        result += sqrt(arr[i]) * log(arr[i] + 1.0);
    return result;
}

__attribute__((noinline))
int linear_search(const int *arr, int n, int target) {
    for (int i = 0; i < n; i++) {
        if (arr[i] == target)
            return i;
    }
    return -1;
}

static int io_intensive(int count) {
    FILE *f = fopen("/tmp/perf_test.txt", "w");
    if (f == NULL) {
        perror("fopen /tmp/perf_test.txt");
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (fprintf(f, "Line %d: measurement data\n", i) < 0) {
            perror("fprintf /tmp/perf_test.txt");
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0) {
        perror("fclose /tmp/perf_test.txt");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int iterations;
    int search_runs;
    int io_lines;

    if (parse_options(argc, argv, &iterations, &search_runs, &io_lines) != 0)
        return EXIT_FAILURE;

    printf("=== Perf Target App - LAB-03 Part C ===\n");
    printf("Workload: array=%d, iterations=%d, search_runs=%d, io_lines=%d\n",
           ARRAY_SIZE, iterations, search_runs, io_lines);

    double *darr = malloc(sizeof(*darr) * ARRAY_SIZE);
    int *iarr = malloc(sizeof(*iarr) * ARRAY_SIZE);
    if (darr == NULL || iarr == NULL) {
        perror("malloc");
        free(darr);
        free(iarr);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < ARRAY_SIZE; i++) {
        darr[i] = (double)(i + 1);
        iarr[i] = i;
    }

    printf("[1] CPU-intensive computation...\n");
    double total = 0.0;
    for (int iter = 0; iter < iterations; iter++)
        total += heavy_compute(darr, ARRAY_SIZE);
    printf("    Result: %.6f\n", total);

    printf("[2] Linear search...\n");
    long search_total = 0;
    for (int i = 0; i < search_runs; i++)
        search_total += linear_search(iarr, ARRAY_SIZE, ARRAY_SIZE - 1);
    printf("    Search result checksum: %ld\n", search_total);

    printf("[3] I/O intensive...\n");
    if (io_intensive(io_lines) != 0) {
        free(darr);
        free(iarr);
        return EXIT_FAILURE;
    }

    free(darr);
    free(iarr);
    printf("Done.\n");
    return 0;
}
