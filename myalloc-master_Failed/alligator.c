#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "myalloc.h"

void *alloc_check(size_t size) {
    void *ptr = myalloc(size);

    if (!ptr) {
        printf("ALLOC FAILED: %lu\n", size);
        exit(1);
    }

    memset(ptr, rand(), size);
    printf("Allocation of size %lu succeeded @ %p!\n", size, ptr);

    return ptr;
}

int main() {
    srand(time(NULL));

    printf("sizes:\n");
    printf("node_t:\t\t%lu\n", sizeof(node_t));
    printf("header_t:\t%lu\n\n", sizeof(header_t));

    void *a, *b, *c, *d;

    (void)a; (void)b; (void)c; (void)d;

    /* TEST 0 */
    a = alloc_check(512);
    b = alloc_check(512);
    printf("Test 0 PASSED\n\n");

    destroy_heap();

    /* TEST 1 */
    a = alloc_check(1024);
    b = alloc_check(1024);
    c = alloc_check(512);
    printf("Test 1 PASSED\n\n");

    destroy_heap();

    /* TEST 3 */
    a = alloc_check(1024);
    b = alloc_check(1024);
    c = alloc_check(1024);

    myfree(b);
    printf("Test 3 PASSED\n\n");

    destroy_heap();

    /* TEST 5 */
    a = alloc_check(512);
    b = alloc_check(512);
    c = alloc_check(512);
    d = alloc_check(2048);

    myfree(b);
    myfree(c);

    coalesce_freelist();
    printf("Test 5 PASSED\n\n");

    printf("🎉 ALL TESTS PASSED 🎉\n");

    return 0;
}