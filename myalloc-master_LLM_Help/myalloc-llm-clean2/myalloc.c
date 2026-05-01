#include <stdio.h>
#include <stdlib.h>
#include "myalloc.h"

#define DEBUG 0

void *__heap = NULL;
node_t *__head = NULL;

/* pointer helper */
header_t *get_header(void *ptr) {
    return (header_t *)((char *)ptr - sizeof(header_t));
}

/* debug helpers (required) */
void print_node(node_t *node) {
    printf("[node @ %p | size: %lu | next: %p]\n",
           node, node->size, node->next);
}

void print_freelist_from(node_t *node) {
    printf("\nFree list:\n");
    while (node) {
        print_node(node);
        node = node->next;
    }
}

/* initialize heap */
void init_heap() {
    __heap = malloc(HEAPSIZE);
    if (!__heap) {
        printf("Heap init failed\n");
        exit(1);
    }

    __head = (node_t *)__heap;
    __head->size = HEAPSIZE - sizeof(header_t);
    __head->next = NULL;
}

/* comparison helper for sorting */
int compare(const void *a, const void *b) {
    node_t *n1 = *(node_t **)a;
    node_t *n2 = *(node_t **)b;
    if (n1 < n2) return -1;
    if (n1 > n2) return 1;
    return 0;
}

/* EXACT coalesce (matches your design) */
void coalesce_freelist() {
    node_t *list[128];
    int count = 0;

    node_t *curr = __head;

    /* copy into array */
    while (curr) {
        list[count++] = curr;
        curr = curr->next;
    }

    if (count == 0) return;

    /* sort */
    qsort(list, count, sizeof(node_t *), compare);

    __head = list[0];

    for (int i = 0; i < count - 1; i++) {
        node_t *curr = list[i];
        node_t *next = list[i + 1];

        char *end = (char *)curr + sizeof(node_t) + curr->size;

        if (end == (char *)next) {
            curr->size += sizeof(node_t) + next->size;
            list[i + 1] = curr; /* collapse */
        } else {
            curr->next = next;
        }
    }

    list[count - 1]->next = NULL;
}

/* first-fit (adjusted to match test expectations) */
void *first_fit(size_t size) {
    node_t *curr = __head;
    node_t *prev = NULL;

    while (curr) {

        if (curr->size >= size) {

            unsigned long original = curr->size;

            header_t *alloc = (header_t *)curr;
            alloc->magic = HEAPMAGIC;

            /* split condition (match YOUR expectations) */
            if (original >= size + sizeof(header_t)) {

                node_t *newnode =
                    (node_t *)((char *)curr + sizeof(header_t) + size);

                newnode->size =
                    original - size - sizeof(header_t);

                newnode->next = curr->next;

                if (prev)
                    prev->next = newnode;
                else
                    __head = newnode;

                alloc->size = size;

            } else {
                /* take full block */
                alloc->size = original;

                if (prev)
                    prev->next = curr->next;
                else
                    __head = curr->next;
            }

            return (char *)alloc + sizeof(header_t);
        }

        prev = curr;
        curr = curr->next;
    }

    return NULL;
}

/* allocation */
void *myalloc(size_t size) {
    if (!__heap)
        init_heap();

    return first_fit(size);
}

/* FREE (unsorted insert — REQUIRED for your tests) */
void myfree(void *ptr) {
    if (!ptr) return;

    header_t *h = get_header(ptr);

    if (h->magic != HEAPMAGIC) {
        printf("The heap is corrupt!\n");
        return;
    }

    node_t *node = (node_t *)h;
    node->size = h->size;

    /* ✅ UNSORTED insert (critical for your spec) */
    node->next = __head;
    __head = node;
}

/* reset */
void destroy_heap() {
    free(__heap);
    __heap = NULL;
    __head = NULL;
}