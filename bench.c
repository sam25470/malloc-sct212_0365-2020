#define _DEFAULT_SOURCE
#include "myalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

enum { OPERATIONS = 50000, SLOTS = 1000 };

typedef struct {
    unsigned int choice;
    size_t size;
} operation_t;

static operation_t workload[OPERATIONS];

static double elapsed_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

/* Generate once so every allocator receives exactly the same workload. */
static void make_workload(void)
{
    srand(20261119);
    for (int i = 0; i < OPERATIONS; ++i) {
        workload[i].choice = (unsigned int)rand() % SLOTS;
        workload[i].size = 1U + (size_t)((unsigned int)rand() % 4096U);
    }
}

static double run_custom(fit_policy_t policy, heap_stats_t *stats)
{
    void *slots[SLOTS] = {0};
    struct timespec start, end;

    set_policy(policy);
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < OPERATIONS; ++i) {
        unsigned int index = workload[i].choice;

        if (slots[index] != NULL) {
            my_free(slots[index]);
            slots[index] = NULL;
        } else {
            slots[index] = my_malloc(workload[i].size);
        }
    }

    for (int i = 0; i < SLOTS; ++i) {
        if (slots[i] != NULL)
            my_free(slots[i]);
    }

    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    heap_stats(stats);
    return elapsed_seconds(start, end);
}

static double run_libc(void)
{
    void *slots[SLOTS] = {0};
    struct timespec start, end;

    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < OPERATIONS; ++i) {
        unsigned int index = workload[i].choice;

        if (slots[index] != NULL) {
            free(slots[index]);
            slots[index] = NULL;
        } else {
            slots[index] = malloc(workload[i].size);
        }
    }

    for (int i = 0; i < SLOTS; ++i)
        free(slots[i]);

    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_seconds(start, end);
}

static void print_stats(const char *name, const heap_stats_t *s, double seconds)
{
    double fragmentation = s->free_bytes == 0
        ? 0.0
        : 1.0 - (double)s->largest_free / (double)s->free_bytes;

    printf("%-10s heap_bytes=%zu free_bytes=%zu free_blocks=%zu "
           "largest_free=%zu external_fragmentation=%.4f time=%.6f s\n",
           name, s->heap_bytes, s->free_bytes, s->free_blocks,
           s->largest_free, fragmentation, seconds);
}

int main(void)
{
    heap_stats_t first, best;
    double first_time, best_time, libc_time;

    make_workload();

    first_time = run_custom(FIT_FIRST, &first);
    best_time = run_custom(FIT_BEST, &best);
    libc_time = run_libc();

    puts("50,000-operation fixed workload; allocation sizes 1..4096 bytes");
    print_stats("FIT_FIRST", &first, first_time);
    print_stats("FIT_BEST", &best, best_time);
    printf("%-10s time=%.6f s\n", "libc", libc_time);

    return 0;
}
