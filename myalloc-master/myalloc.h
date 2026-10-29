#ifndef MYALLOC_H
#define MYALLOC_H

/*
 * Educational malloc/free over one fixed heap.
 *
 * Two metadata records occupy the same bytes at different times. They are
 * the same width, and the second field sits at the same offset:
 *
 *   allocated: [ header_t | size | magic | user bytes .............. ]
 *   free:      [ node_t   | size | next  | unused payload ........... ]
 *
 * `size` is always the byte count AFTER the metadata, never the whole block.
 * Requests are rounded up to 8 bytes by default (`MYALLOC_ALIGN` in
 * myalloc_config.h) so the next header stays aligned.
 * Every size alligator.c checks is already a multiple of 8, except the
 * 14-byte allocation in test 7, which reuses a whole 16-byte free node.
 * myfree() turns a header into a node by writing the freelist link over
 * `magic`. A second free of the same pointer fails that magic check.
 *
 * The free list is unsorted. myfree() and block splits insert at the head.
 * Neighboring free blocks merge only when coalesce_freelist() is called.
 * Those choices are part of the contract alligator.c checks; see DESIGN.md.
 * Compile-time experiments (fit, alignment, address order, locking) live
 * in myalloc_config.h. The defaults are the ones the suite requires.
 *
 * destroy_heap() is safe to call more than once, including before init.
 * init_heap() registers it with atexit unless MYALLOC_ATEXIT is 0.
 */

#include <stddef.h>

#define HEAPSIZE 4096
#define HEAPMAGIC 0x10c0beefbad1dea5UL

/* Free-list node. `size` is the payload after this struct. */
typedef struct __node_t {
    unsigned long size;
    struct __node_t *next;
} node_t;

/* Header of a live allocation. Same width and field offsets as node_t. */
typedef struct __header_t {
    unsigned long size;
    unsigned long magic;
} header_t;

extern void *__heap;   /* HEAPSIZE bytes from system malloc, or NULL */
extern node_t *__head; /* freelist head; NULL when nothing is free */

void print_header(const header_t *header);
void print_node(const node_t *node);
void print_freelist_from(const node_t *node);
void print_heap(void);          /* address-ordered map of the whole heap */
void myalloc_dump_stats(void);  /* counters for the current process */

void init_heap(void);
void destroy_heap(void);

/* Register destroy_heap with atexit. Returns 0 on success.
 * init_heap() does this when MYALLOC_ATEXIT is 1. Safe to call again. */
int myalloc_atexit_install(void);
#define MYALLOC_INSTALL_ATEXIT() myalloc_atexit_install()

void *myalloc(size_t size);
void myfree(void *ptr);
void *mycalloc(size_t nmemb, size_t size);
void *myrealloc(void *ptr, size_t size);

void coalesce_freelist(void);

/* Unchecked: ptr must be a live user pointer. myfree() validates first. */
header_t *get_header(void *ptr);

#endif
