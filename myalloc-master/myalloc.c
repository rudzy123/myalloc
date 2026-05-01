#include <stdio.h>
#include <stdlib.h>
#include "myalloc.h"

#define DEBUG 1

void *__heap = NULL;
node_t *__head = NULL;

header_t *get_header(void *ptr) {
    return (header_t *)((char *)ptr - sizeof(header_t));
}

void print_header(header_t *header) {
    printf("[header @ %p | size: %lu | magic: %lx]\n",
           header, header->size, header->magic);
}

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

void destroy_heap() {
    free(__heap);
    __heap = NULL;
    __head = NULL;
}


void init_heap() {
    __heap = malloc(HEAPSIZE);
    if (!__heap) {
        printf("Heap init failed\n");
        exit(1);
    }

    __head = (node_t *)__heap;

    /* ✅ FIX HERE */
    __head->size = HEAPSIZE - sizeof(node_t);

    __head->next = NULL;

    if (DEBUG) print_node(__head);
}


/* O(n) coalescing */
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

/* ✅ FIXED first-fit allocator */
void *first_fit(size_t req_size) {
    node_t *curr = __head;
    node_t *prev = NULL;

    while (curr) {
        if (curr->size >= req_size) {
            size_t remaining = curr->size - req_size;

            header_t *alloc = (header_t *)curr;
            alloc->magic = HEAPMAGIC;

            if (remaining >= sizeof(node_t) + sizeof(header_t)) {
                /* split block */
                node_t *newnode = (node_t *)((char *)curr + sizeof(header_t) + req_size);

                /* ✅ FIX HERE */
                newnode->size = remaining - sizeof(node_t);

                newnode->next = curr->next;

                if (prev == NULL)
                    __head = newnode;
                else
                    prev->next = newnode;

                alloc->size = req_size;

            } else {
                /* use entire block */
                alloc->size = curr->size;

                if (prev == NULL)
                    __head = curr->next;
                else
                    prev->next = curr->next;
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

    if (DEBUG) {
        printf("\nmyalloc(%lu)\n", size);
    }

    return first_fit(size);
}

/* Sorted insertion + auto coalesce */
void myfree(void *ptr) {
    if (!ptr) return;

    header_t *header = get_header(ptr);

    if (header->magic != HEAPMAGIC) {
        printf("Heap corruption detected!\n");
        return;
    }

    node_t *node = (node_t *)header;
    node->size = header->size;

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