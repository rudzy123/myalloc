#ifndef __MYALLOC_H
#define __MYALLOC_H


#define HEAPSIZE 4096
#define HEAPMAGIC 0x10c0beefbad1dea5

/* linked list element */
typedef struct __node_t {
    long unsigned size;
    struct __node_t *next;
} node_t;

/* buffer metadata structure */
typedef struct __header_t {
    long unsigned size;
    long unsigned magic;
} header_t;

extern void *__heap; /* pointer to the start of our "heap" */
extern node_t *__head; /* the 'head' pointer for our freelist */

void print_header(header_t *header); /* print out header */
void print_node(node_t *node);  /* print out node */
void print_freelist_from(node_t *node); /* print freelist from node to NULL */
void destroy_heap(); /* destroy current heap (reset function) */
void *myalloc(size_t size); /* our new malloc-alike function */
void myfree(void *ptr);  /* our new free-alike function */
void coalesce_freelist(); /* coalesce all neighboring free regions */

/* FOR OFFICE USE ONLY */
header_t *get_header(void *ptr);

void init_heap();

#endif
