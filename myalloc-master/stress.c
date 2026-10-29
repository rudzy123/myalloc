/*
 * Separate from alligator.c. `make run` does not build or run this file.
 *
 * Checks init/destroy cycles, a free list longer than the old 100-node
 * coalesce scratch array, and a short alloc/free loop. Compile it against
 * the same myalloc.c the suite uses:
 *
 *   make stress
 *   make stress CFLAGS="-Wall -Wextra -std=c11 -g -DMYALLOC_LOCK=1"
 */

#include "myalloc.h"
#include "myalloc_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if MYALLOC_ATEXIT
#include <unistd.h>
#endif

enum { NSMALL = 150, BENCH_REPS = 2000, BENCH_BATCH = 64 };

#if MYALLOC_ATEXIT
static void check_released(void)
{
    if (__heap != NULL || __head != NULL) {
        fprintf(stderr, "stress: atexit did not release the heap\n");
        fflush(stderr);
        _exit(1);
    }
}
#endif

static size_t freelist_len(void)
{
    size_t n = 0;
    node_t *cur;

    for (cur = __head; cur != NULL; cur = cur->next) {
        if (++n > 100000)
            break;
    }
    return n;
}

static int expect_single(unsigned long payload)
{
    if (__heap == NULL || __head == NULL) {
        fprintf(stderr, "stress: heap is not a single free node (missing)\n");
        return -1;
    }
    if ((void *)__head != __heap) {
        fprintf(stderr, "stress: freelist head is not the heap base\n");
        return -1;
    }
    if (__head->next != NULL) {
        fprintf(stderr, "stress: expected one free node\n");
        return -1;
    }
    if (__head->size != payload) {
        fprintf(stderr, "stress: payload %lu, expected %lu\n",
                __head->size, payload);
        return -1;
    }
    return 0;
}

static int test_cycles(void)
{
    void *first;
    int i;
    unsigned long payload = HEAPSIZE - sizeof(header_t);

    destroy_heap();
    destroy_heap();
    if (__heap != NULL || __head != NULL) {
        fprintf(stderr, "stress: destroy before init left a heap\n");
        return -1;
    }

    init_heap();
    first = __heap;
    init_heap();
    if (__heap != first) {
        fprintf(stderr, "stress: second init_heap replaced the heap\n");
        return -1;
    }
    if (expect_single(payload) != 0)
        return -1;

    for (i = 0; i < 5; i++) {
        destroy_heap();
        destroy_heap();
        if (__heap != NULL || __head != NULL) {
            fprintf(stderr, "stress: destroy left globals set\n");
            return -1;
        }
        init_heap();
        if (expect_single(payload) != 0)
            return -1;
    }
    printf("stress: init/destroy cycles ok\n");
    return 0;
}

static int test_many_free_nodes(void)
{
    void *ptr[NSMALL];
    int i;
    size_t n;
    unsigned long payload = HEAPSIZE - sizeof(header_t);

    destroy_heap();
    for (i = 0; i < NSMALL; i++) {
        ptr[i] = myalloc(8);
        if (ptr[i] == NULL) {
            fprintf(stderr, "stress: alloc %d of %d failed\n", i, NSMALL);
            return -1;
        }
    }
    for (i = 0; i < NSMALL; i++)
        myfree(ptr[i]);

    n = freelist_len();
    if (n <= 100) {
        fprintf(stderr, "stress: expected >100 free nodes before coalesce, got %zu\n", n);
        return -1;
    }
    printf("stress: %zu free nodes before coalesce\n", n);

    coalesce_freelist();
    if (expect_single(payload) != 0)
        return -1;
    printf("stress: coalesced %zu nodes back to one\n", n);
    return 0;
}

static int test_gapped_coalesce(void)
{
    void *ptr[NSMALL];
    int i;
    int n8 = 0;
    int ntail = 0;
    int nother = 0;
    size_t n;
    unsigned long payload = HEAPSIZE - sizeof(header_t);
    unsigned long tail = payload - (unsigned long)NSMALL * (sizeof(header_t) + 8);

    destroy_heap();
    for (i = 0; i < NSMALL; i++) {
        ptr[i] = myalloc(8);
        if (ptr[i] == NULL) {
            fprintf(stderr, "stress: gapped alloc failed\n");
            return -1;
        }
    }
    for (i = 0; i < NSMALL; i += 2)
        myfree(ptr[i]);

    coalesce_freelist();
    for (node_t *cur = __head; cur != NULL; cur = cur->next) {
        if (cur->size == 8)
            n8++;
        else if (cur->size == tail)
            ntail++;
        else
            nother++;
    }
    /* Evens are separated by live odds, so nothing merges. Tail stays. */
    if (n8 != NSMALL / 2 || ntail != 1 || nother != 0) {
        fprintf(stderr, "stress: gapped coalesce changed sizes (8:%d tail:%d other:%d)\n",
                n8, ntail, nother);
        return -1;
    }

    for (i = 1; i < NSMALL; i += 2)
        myfree(ptr[i]);
    n = freelist_len();
    if (n <= 100) {
        fprintf(stderr, "stress: gapped free list has %zu nodes\n", n);
        return -1;
    }
    coalesce_freelist();
    if (expect_single(payload) != 0)
        return -1;
    printf("stress: gapped free nodes stayed split, then merged\n");
    return 0;
}

static int test_calloc_realloc(void)
{
    unsigned char *p;
    unsigned char *grown;
    int i;

    destroy_heap();
    if (mycalloc((size_t)-1, (size_t)-1) != NULL) {
        fprintf(stderr, "stress: overflowing mycalloc returned a pointer\n");
        return -1;
    }

    p = mycalloc(8, 4);
    if (p == NULL) {
        fprintf(stderr, "stress: mycalloc failed\n");
        return -1;
    }
    for (i = 0; i < 32; i++) {
        if (p[i] != 0) {
            fprintf(stderr, "stress: mycalloc left byte %d nonzero\n", i);
            return -1;
        }
    }
    p[0] = 0xab;

    grown = myrealloc(p, 128);
    if (grown == NULL || grown[0] != 0xab) {
        fprintf(stderr, "stress: myrealloc grow failed\n");
        return -1;
    }
    grown = myrealloc(grown, 8);
    if (grown == NULL || grown[0] != 0xab) {
        fprintf(stderr, "stress: myrealloc shrink failed\n");
        return -1;
    }
    if (myrealloc(grown, 0) != NULL) {
        fprintf(stderr, "stress: myrealloc to 0 did not free\n");
        return -1;
    }
    myfree(NULL);

    p = myrealloc(NULL, 16);
    if (p == NULL) {
        fprintf(stderr, "stress: myrealloc(NULL) failed\n");
        return -1;
    }
    myfree(p);
    printf("stress: calloc/realloc ok\n");
    return 0;
}

static int test_free_after_destroy(void)
{
    void *p;

    p = myalloc(32);
    if (p == NULL) {
        fprintf(stderr, "stress: alloc before destroy failed\n");
        return -1;
    }
    destroy_heap();
    destroy_heap();
    /* The region is gone. myfree must not read it. */
    printf("stress: freeing a stale pointer after destroy "
           "(expect 'heap is not initialized' on stderr)\n");
    myfree(p);
    if (__heap != NULL || __head != NULL) {
        fprintf(stderr, "stress: stale free resurrected the heap\n");
        return -1;
    }
    p = myalloc(32);
    if (p == NULL) {
        fprintf(stderr, "stress: alloc after stale free failed\n");
        return -1;
    }
    myfree(p);
    printf("stress: destroy then reuse ok\n");
    return 0;
}

static int test_benchmark(void)
{
    void *ptr[BENCH_BATCH];
    int rep;
    int i;
    clock_t t0;
    clock_t t1;
    double ms;

    destroy_heap();
    t0 = clock();
    for (rep = 0; rep < BENCH_REPS; rep++) {
        for (i = 0; i < BENCH_BATCH; i++) {
            ptr[i] = myalloc(32);
            if (ptr[i] == NULL) {
                fprintf(stderr, "stress: benchmark alloc failed\n");
                return -1;
            }
        }
        for (i = 0; i < BENCH_BATCH; i++)
            myfree(ptr[i]);
        coalesce_freelist();
    }
    t1 = clock();
    ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    printf("stress: benchmark %d x %d alloc/free/coalesce in %.1f ms\n",
           BENCH_REPS, BENCH_BATCH, ms);
    return 0;
}

int main(void)
{
#if MYALLOC_ATEXIT
    if (atexit(check_released) != 0) {
        fprintf(stderr, "stress: could not arm the release check\n");
        return 1;
    }
#endif

    printf("stress: fit=%d align=%d auto_coalesce=%d addr_order=%d lock=%d atexit=%d\n",
           MYALLOC_FIT, MYALLOC_ALIGN, MYALLOC_AUTO_COALESCE, MYALLOC_ADDR_ORDER,
           MYALLOC_LOCK, MYALLOC_ATEXIT);

    if (test_cycles() != 0)
        return 1;
    if (test_many_free_nodes() != 0)
        return 1;
    if (test_gapped_coalesce() != 0)
        return 1;
    if (test_calloc_realloc() != 0)
        return 1;
    if (test_free_after_destroy() != 0)
        return 1;
    if (test_benchmark() != 0)
        return 1;

    myalloc_dump_stats();
    printf("stress: ok\n");
    return 0;
}
