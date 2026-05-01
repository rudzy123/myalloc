#ifndef __MYALLOC_H
#define __MYALLOC_H

#define HEAPSIZE 4096
#define HEAPMAGIC 0x10c0beefbad1dea5

typedef struct __node_t {
    unsigned long size;
    struct __node_t *next;
} node_t;

typedef struct __header_t {
    unsigned long size;
    unsigned long magic;
} header_t;

extern void *__heap;
extern node_t *__head;

void print_header(header_t *header);
void print_node(node_t *node);
void print_freelist_from(node_t *node);

void destroy_heap();
void *myalloc(size_t size);
void myfree(void *ptr);
void coalesce_freelist();

header_t *get_header(void *ptr);
void init_heap();

#endif