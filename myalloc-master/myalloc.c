#include <stdio.h>
#include NULL;#include <stdlib.h>
node_t *__head = NULL;

/* pointer helper */
header_t *get_header(void *ptr) {
    return (header_t *)((char *)ptr - sizeof(header_t));
}

/* debug helpers (required by tester) */
void print_node(node_t *node) {
    printf("[node @ %p | free region @ %p size: %lu next: %p]\n",
           node,
           (char *)node + sizeof(node_t),
           node->size,
           node->next);
}

void print_freelist_from(node_t *node) {
    printf("\nPrinting freelist from %p\n", node);
    while (node) {
        print_node(node);
        node = node->next;
    }
}

/* init heap */
void init_heap() {
    __heap = malloc(HEAPSIZE);
    if (!__heap) {
        printf("Heap init failed\n");
        exit(1);
    }

    __head = (node_t *)__heap;

    /* EXACTLY your invariant */
    __head->size = HEAPSIZE - sizeof(header_t);
    __head->next = NULL;
}

/* compare for qsort */
int compare(const void *a, const void *b) {
    node_t *n1 = *(node_t **)a;
    node_t *n2 = *(node_t **)b;

    if (n1 < n2) return -1;
    if (n1 > n2) return 1;
    return 0;
}

/* EXACT coalescing (same logic as yours but cleaner) */
void coalesce_freelist() {
    node_t *list[100];
    int n = 0;

    node_t *curr = __head;

    while (curr) {
        list[n++] = curr;
        curr = curr->next;
    }

    if (n == 0) return;

    qsort(list, n, sizeof(node_t *), compare);

    __head = list[0];

    for (int i = 0; i < n - 1; i++) {
        list[i]->next = list[i + 1];
    }
    list[n - 1]->next = NULL;

    for (int i = 0; i < n - 1; i++) {
        node_t *cur = list[i];

        while (cur->next &&
               (char *)cur + cur->size + sizeof(node_t) == (char *)cur->next) {

            cur->size += sizeof(node_t) + cur->next->size;
            cur->next = cur->next->next;
        }
    }
}

/* ✅ CLEAN first_fit — EXACTLY your model */
void *first_fit(size_t req_size) {
    node_t *curr = __head;
    node_t *prev = NULL;

    while (curr) {

        if (curr->size >= req_size) {

            unsigned long free_size = curr->size;
            node_t *next = curr->next;

            /* remove curr */
            if (prev)
                prev->next = next;
            else
                __head = next;

            header_t *alloc = (header_t *)curr;
            alloc->magic = HEAPMAGIC;

            /* SPLIT — your exact invariant */
            if (free_size - req_size >= sizeof(node_t)) {

                node_t *newnode =
                    (node_t *)((char *)alloc + sizeof(header_t) + req_size);

                newnode->size =
                    free_size - req_size - sizeof(header_t);

                newnode->next = __head;
                __head = newnode;

                alloc->size = req_size;

            } else {
                alloc->size = free_size;
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

/* FREE — unsorted insert */
void myfree(void *ptr) {
    if (!ptr) return;

    header_t *h = get_header(ptr);

    if (h->magic != HEAPMAGIC) {
        printf("The heap is corrupt!\n");
        return;
    }

    node_t *node = (node_t *)h;
    node->size = h->size;

    /* EXACT behavior your tests expect */
    node->next = __head;
    __head = node;
}

/* destroy */
void destroy_heap() {
    free(__heap);
    __heap = NULL;
    __head = NULL;
}

#include "myalloc.h"

#define DEBUG 0

