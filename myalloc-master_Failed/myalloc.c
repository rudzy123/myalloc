#include <stdio.h>
#include <stdlib.h>
#include "myalloc.h"

#define DEBUG 0

void *__heap = NULL;
node_t *__head = NULL;

/* correct pointer math */
header_t *get_header(void *ptr) {
    return (header_t *)((char *)ptr - sizeof(header_t));
}

/* initialize heap */
void init_heap() {
    __heap = malloc(HEAPSIZE);
    if (!__heap) {
        printf("Couldn't initialize heap!\n");
        exit(1);
    }

    __head = (node_t *)__heap;

    /* ✅ FIX 1 */
    __head->size = HEAPSIZE - sizeof(node_t);

    __head->next = NULL;
}

/* simple O(n) coalescing */
void coalesce_freelist() {
    node_t *curr = __head;

    while (curr && curr->next) {
        char *end = (char *)curr + sizeof(node_t) + curr->size;

        if (end == (char *)curr->next) {
            curr->size += sizeof(node_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

/* CLEAN first-fit */
void *first_fit(size_t size) {
    node_t *curr = __head;
    node_t *prev = NULL;

    while (curr) {

        if (curr->size >= size) {

            size_t original = curr->size;

            header_t *alloc = (header_t *)curr;
            alloc->magic = HEAPMAGIC;

            /* can we split? */
            if (original >= size + sizeof(header_t) + sizeof(node_t)) {

                node_t *newnode =
                    (node_t *)((char *)curr + sizeof(header_t) + size);

                newnode->size =
                    original - size - sizeof(header_t) - sizeof(node_t);

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

void *myalloc(size_t size) {
    if (__heap == NULL) {
        init_heap();
    }

    return first_fit(size);
}

/* ✅ FIX 2: sorted insertion */
void myfree(void *ptr) {
    if (!ptr) return;

    header_t *header = get_header(ptr);

    if (header->magic != HEAPMAGIC) {
        printf("The heap is corrupt!\n");
        return;
    }

    node_t *node = (node_t *)header;
    node->size = header->size;

    /* insert sorted by address */
    if (__head == NULL || node < __head) {
        node->next = __head;
        __head = node;
    } else {
        node_t *curr = __head;

        while (curr->next && curr->next < node) {
            curr = curr->next;
        }

        node->next = curr->next;
        curr->next = node;
    }

    coalesce_freelist();
}

void destroy_heap() {
    free(__heap);
    __heap = NULL;
    __head = NULL;
}