#include "myalloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Compile-time knobs (defaults are what alligator.c requires):
 *
 *   -DMYALLOC_DEBUG=1
 *       Trace alloc, free, and coalesce on stderr.
 *
 *   -DMYALLOC_FIT=0|1|2
 *       0 first-fit (default), 1 best-fit, 2 next-fit.
 *       Best-fit passes the suite. Classic next-fit does not: test 7
 *       allocates 14 bytes while both a 16-byte block and a large tail
 *       fit, and the 16-byte block must be the one consumed.
 *
 *   -DMYALLOC_ALIGN=N
 *       Round requests up to a multiple of N. N is a power of two and a
 *       multiple of node_t's alignment (8). The default is 8: every size
 *       alligator.c actually checks is already a multiple of 8, except
 *       the 14-byte reuse in test 7, which consumes a 16-byte node whole
 *       either way. N=16 fails test 5.3, because a 1000-byte request into
 *       a 1040-byte free block must leave a 24-byte free node.
 *
 *   -DMYALLOC_AUTO_COALESCE=1
 *       Merge neighbors inside myfree(). Default off. Tests 5.1 and 6.1
 *       free two adjacent blocks and inspect the list before
 *       coalesce_freelist() runs.
 */

#ifndef MYALLOC_DEBUG
#define MYALLOC_DEBUG 0
#endif

#define MYALLOC_FIT_FIRST 0
#define MYALLOC_FIT_BEST  1
#define MYALLOC_FIT_NEXT  2

#ifndef MYALLOC_FIT
#define MYALLOC_FIT MYALLOC_FIT_FIRST
#endif

#ifndef MYALLOC_ALIGN
#define MYALLOC_ALIGN 8
#endif

#ifndef MYALLOC_AUTO_COALESCE
#define MYALLOC_AUTO_COALESCE 0
#endif

#if MYALLOC_DEBUG
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

/*
 * A free node occupies at least sizeof(node_t) bytes (payload may be 0).
 * That is the maximum number of free nodes a healthy heap can hold, so the
 * coalesce scratch array is bounded instead of the old list[100].
 */
enum { MYALLOC_MAX_FREE_NODES = HEAPSIZE / (int)sizeof(node_t) };

_Static_assert(sizeof(header_t) == sizeof(node_t),
               "split/coalesce math assumes header_t and node_t are the same size");
_Static_assert(offsetof(header_t, size) == 0 && offsetof(node_t, size) == 0,
               "size must be the first field of both metadata records");
_Static_assert(offsetof(header_t, magic) == offsetof(node_t, next),
               "myfree overwrites magic with the freelist link");
_Static_assert((MYALLOC_ALIGN & (MYALLOC_ALIGN - 1)) == 0,
               "MYALLOC_ALIGN must be a power of two");
_Static_assert(MYALLOC_ALIGN % _Alignof(node_t) == 0,
               "MYALLOC_ALIGN must keep free-node headers aligned");
#if MYALLOC_FIT != MYALLOC_FIT_FIRST && MYALLOC_FIT != MYALLOC_FIT_BEST && \
    MYALLOC_FIT != MYALLOC_FIT_NEXT
#error "MYALLOC_FIT must be 0 (first), 1 (best), or 2 (next)"
#endif

void *__heap = NULL;
node_t *__head = NULL;

#if MYALLOC_FIT == MYALLOC_FIT_NEXT
/* Block where the next next-fit search starts. Not used by the default. */
static node_t *rover = NULL;
#endif

typedef struct myalloc_stats {
    unsigned long heap_inits;
    unsigned long alloc_ok;
    unsigned long alloc_fail;
    unsigned long free_ok;
    unsigned long free_reject;
    unsigned long coalesces;
    unsigned long bytes_requested;
    unsigned long bytes_granted;
} myalloc_stats;

static myalloc_stats stats;

static void report_corrupt(void)
{
    /* Test 4 looks for this sentence on a double free. */
    printf("The heap is corrupt!\n");
}

static char *heap_lo(void)
{
    return (char *)__heap;
}

static char *heap_hi(void)
{
    return (char *)__heap + HEAPSIZE;
}

/* True when `node` and its payload lie entirely inside the heap. */
static int free_node_ok(const node_t *node)
{
    uintptr_t lo;
    uintptr_t hi;
    uintptr_t p;
    size_t room;

    if (__heap == NULL || node == NULL)
        return 0;

    /* Integer comparison: relational checks on pointers into other objects
     * are undefined, and a corrupt freelist can hold any bit pattern. */
    lo = (uintptr_t)__heap;
    hi = lo + HEAPSIZE;
    p = (uintptr_t)node;
    if (p < lo || p >= hi)
        return 0;
    /* Reject a misaligned link before the member read. node_t is aligned
     * to 8, and a corrupt next pointer is allowed to be anything. */
    if ((p % _Alignof(node_t)) != 0)
        return 0;
    room = (size_t)(hi - p);
    if (room < sizeof(node_t))
        return 0;
    if (node->size > room - sizeof(node_t))
        return 0;
    return 1;
}

static int too_many_nodes(size_t seen)
{
    if (seen > MYALLOC_MAX_FREE_NODES) {
        report_corrupt();
        return 1;
    }
    return 0;
}

header_t *get_header(void *ptr)
{
    return (header_t *)((char *)ptr - sizeof(header_t));
}

/*
 * A live user pointer: the header is inside the heap, magic matches, and
 * the recorded payload does not run past the end. Double-free fails here
 * because the freelist link has replaced magic.
 */
static header_t *live_header(void *ptr)
{
    uintptr_t lo;
    uintptr_t hi;
    uintptr_t user;
    uintptr_t block;
    header_t *hdr;
    size_t room;

    if (ptr == NULL || __heap == NULL)
        return NULL;

    lo = (uintptr_t)__heap;
    hi = lo + HEAPSIZE;
    user = (uintptr_t)ptr;
    if (user < lo + sizeof(header_t) || user > hi)
        return NULL;

    block = user - sizeof(header_t);
    if ((block % _Alignof(header_t)) != 0)
        return NULL;
    hdr = (header_t *)block;
    if (hdr->magic != HEAPMAGIC)
        return NULL;

    room = (size_t)(hi - user);
    if (hdr->size > room)
        return NULL;
    return hdr;
}

/* Round up so the next metadata record stays aligned. 0 stays 0. */
static size_t adjust_request(size_t size)
{
    const size_t align = (size_t)MYALLOC_ALIGN;

    if (size > SIZE_MAX - (align - 1))
        return SIZE_MAX;
    return (size + align - 1) & ~(align - 1);
}

/*
 * Carve `request` bytes out of `block`, which is already known to fit.
 *
 * Split when the bytes we would not give to the caller can hold a new
 * node_t. The new node's own size may be 0: test 6 allocates 1024 from a
 * 1040-byte free payload and expects a free node of size
 * 1040 - 1024 - sizeof(header_t) == 0. Using ">" instead of ">=" fails it.
 *
 * Otherwise the caller gets the whole payload, even if that is larger than
 * the request. Test 7 allocates 14 from a 16-byte node and requires the
 * header size to stay 16 so the extra two bytes are not a leaked fragment
 * and not a second free node.
 *
 * The leftover node is pushed on the head. When `block` itself was the
 * head, the leftover stays at the head and keeps the old successor.
 */
static void *take_block(node_t *prev, node_t *block, size_t request)
{
    node_t *next = block->next;
    unsigned long free_size = block->size;
    header_t *hdr;
    size_t spare;

    assert((size_t)free_size >= request);

    if (prev != NULL)
        prev->next = next;
    else
        __head = next;

    hdr = (header_t *)block;
    hdr->magic = HEAPMAGIC;

    spare = (size_t)free_size - request;
    if (spare >= sizeof(node_t)) {
        node_t *rem = (node_t *)((char *)hdr + sizeof(header_t) + request);

        assert((char *)rem >= heap_lo());
        assert((char *)rem + sizeof(node_t) <= heap_hi());
        assert(((uintptr_t)rem % _Alignof(node_t)) == 0);

        rem->size = (unsigned long)(spare - sizeof(header_t));
        rem->next = __head;
        __head = rem;
        hdr->size = (unsigned long)request;
    } else {
        hdr->size = free_size;
    }

    return (char *)hdr + sizeof(header_t);
}

#if MYALLOC_FIT == MYALLOC_FIT_FIRST
static void *first_fit(size_t request)
{
    node_t *prev = NULL;
    node_t *cur;
    size_t seen = 0;

    for (cur = __head; cur != NULL; prev = cur, cur = cur->next) {
        if (!free_node_ok(cur)) {
            report_corrupt();
            return NULL;
        }
        if (too_many_nodes(++seen))
            return NULL;
        if ((size_t)cur->size >= request)
            return take_block(prev, cur, request);
    }
    return NULL;
}
#endif

#if MYALLOC_FIT == MYALLOC_FIT_BEST
static void *best_fit(size_t request)
{
    node_t *prev = NULL;
    node_t *cur;
    node_t *best = NULL;
    node_t *best_prev = NULL;
    size_t seen = 0;

    for (cur = __head; cur != NULL; prev = cur, cur = cur->next) {
        if (!free_node_ok(cur)) {
            report_corrupt();
            return NULL;
        }
        if (too_many_nodes(++seen))
            return NULL;
        if ((size_t)cur->size >= request &&
            (best == NULL || cur->size < best->size)) {
            best = cur;
            best_prev = prev;
        }
    }
    if (best == NULL)
        return NULL;
    return take_block(best_prev, best, request);
}
#endif

#if MYALLOC_FIT == MYALLOC_FIT_NEXT
static void *next_fit(size_t request)
{
    node_t *prev = NULL;
    node_t *cur;
    node_t *start;
    size_t seen = 0;

    if (__head == NULL) {
        rover = NULL;
        return NULL;
    }

    cur = __head;
    if (rover != NULL) {
        node_t *walk_prev = NULL;
        node_t *walk = __head;

        while (walk != NULL && walk != rover) {
            if (!free_node_ok(walk)) {
                report_corrupt();
                return NULL;
            }
            if (too_many_nodes(++seen))
                return NULL;
            walk_prev = walk;
            walk = walk->next;
        }
        if (walk == rover) {
            if (!free_node_ok(walk)) {
                report_corrupt();
                return NULL;
            }
            prev = walk_prev;
            cur = walk;
        }
    }

    if (!free_node_ok(cur)) {
        report_corrupt();
        return NULL;
    }

    start = cur;
    seen = 0;
    for (;;) {
        node_t *after;
        size_t spare;
        void *mem;

        if (!free_node_ok(cur)) {
            report_corrupt();
            return NULL;
        }
        if (too_many_nodes(++seen))
            return NULL;
        if ((size_t)cur->size >= request) {
            /* Update the rover after the split, so it never keeps a
             * pointer to the block we just turned into a header. */
            after = cur->next;
            spare = (size_t)cur->size - request;
            mem = take_block(prev, cur, request);
            if (spare >= sizeof(node_t))
                rover = __head;
            else
                rover = (after != NULL) ? after : __head;
            return mem;
        }
        prev = cur;
        cur = cur->next;
        if (cur == NULL) {
            prev = NULL;
            cur = __head;
        }
        if (cur == start)
            return NULL;
    }
}
#endif

static void *search_freelist(size_t request)
{
#if MYALLOC_FIT == MYALLOC_FIT_BEST
    return best_fit(request);
#elif MYALLOC_FIT == MYALLOC_FIT_NEXT
    return next_fit(request);
#else
    return first_fit(request);
#endif
}

static int freelist_addr_cmp(const void *a, const void *b)
{
    uintptr_t pa = (uintptr_t)(*(node_t *const *)a);
    uintptr_t pb = (uintptr_t)(*(node_t *const *)b);

    if (pa < pb)
        return -1;
    if (pa > pb)
        return 1;
    return 0;
}

/* Byte just past a free node. The next neighbor starts here when adjacent. */
static char *free_block_end(const node_t *node)
{
    return (char *)node + sizeof(node_t) + node->size;
}

void init_heap(void)
{
    node_t *head;

    if (__heap != NULL)
        return;

    __heap = malloc(HEAPSIZE);
    if (__heap == NULL) {
        fprintf(stderr, "Couldn't initialize heap!\n");
        __head = NULL;
        return;
    }

    /*
     * One free node covers the heap. Its payload is everything after the
     * node header. sizeof(header_t) is the term the original allocator and
     * the test arithmetic use; the static assert makes it equal sizeof(node_t).
     * 16 + 4080 = 4096.
     */
    head = (node_t *)__heap;
    head->size = HEAPSIZE - sizeof(header_t);
    head->next = NULL;
    __head = head;
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
    rover = NULL;
#endif
    stats.heap_inits++;
    DBG("init_heap: heap %p payload %lu\n", __heap, head->size);
}

void destroy_heap(void)
{
    free(__heap);
    __heap = NULL;
    __head = NULL;
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
    rover = NULL;
#endif
}

void *myalloc(size_t size)
{
    size_t request;
    void *ptr;

    if (__heap == NULL)
        init_heap();
    if (__heap == NULL) {
        stats.alloc_fail++;
        return NULL;
    }

    request = adjust_request(size);
    DBG("myalloc(%zu) request %zu head %p\n", size, request, (void *)__head);
    ptr = search_freelist(request);
    if (ptr == NULL) {
        stats.alloc_fail++;
        DBG("myalloc(%zu) failed\n", size);
        return NULL;
    }

    stats.alloc_ok++;
    stats.bytes_requested += size;
    stats.bytes_granted += get_header(ptr)->size;
    return ptr;
}

void myfree(void *ptr)
{
    header_t *hdr;
    node_t *node;
    unsigned long bytes;

    if (ptr == NULL)
        return;

    hdr = live_header(ptr);
    if (hdr == NULL) {
        stats.free_reject++;
        report_corrupt();
        return;
    }

    /*
     * Keep the payload size, then overlay a node on the header. Assigning
     * next replaces magic, which is what makes the next free fail.
     * Insert at the head (O(1)). Test 7 depends on that order for first-fit:
     * the 16-byte block just freed must be tried before the large tail.
     * Coalescing here would merge blocks the suite still expects to see
     * as separate nodes.
     */
    bytes = hdr->size;
    node = (node_t *)hdr;
    node->size = bytes;
    node->next = __head;
    __head = node;
    stats.free_ok++;
    DBG("myfree(%p) size %lu\n", ptr, bytes);

#if MYALLOC_AUTO_COALESCE
    coalesce_freelist();
#endif
}

void *mycalloc(size_t nmemb, size_t size)
{
    void *ptr;
    header_t *hdr;

    if (size != 0 && nmemb > SIZE_MAX / size)
        return NULL;

    ptr = myalloc(nmemb * size);
    if (ptr == NULL)
        return NULL;

    /* Zero the whole granted block, which may be larger than nmemb * size. */
    hdr = get_header(ptr);
    memset(ptr, 0, hdr->size);
    return ptr;
}

void *myrealloc(void *ptr, size_t size)
{
    header_t *hdr;
    size_t request;
    size_t spare;
    void *fresh;

    if (ptr == NULL)
        return myalloc(size);
    if (size == 0) {
        myfree(ptr);
        return NULL;
    }

    hdr = live_header(ptr);
    if (hdr == NULL) {
        report_corrupt();
        return NULL;
    }

    request = adjust_request(size);
    if ((size_t)hdr->size >= request) {
        /*
         * Shrink in place when the tail can hold its own free node.
         * A smaller tail stays with the caller, same rule as allocation.
         */
        spare = (size_t)hdr->size - request;
        if (spare >= sizeof(node_t)) {
            node_t *rem = (node_t *)((char *)ptr + request);

            assert(((uintptr_t)rem % _Alignof(node_t)) == 0);
            rem->size = (unsigned long)(spare - sizeof(header_t));
            rem->next = __head;
            __head = rem;
            hdr->size = (unsigned long)request;
        }
        return ptr;
    }

    fresh = myalloc(size);
    if (fresh == NULL)
        return NULL;
    memcpy(fresh, ptr, hdr->size);
    myfree(ptr);
    return fresh;
}

void coalesce_freelist(void)
{
    node_t *ordered[MYALLOC_MAX_FREE_NODES];
    size_t n = 0;
    size_t i;
    node_t *cur;
    node_t *tail;

    stats.coalesces++;
    DBG("coalesce_freelist\n");

    for (cur = __head; cur != NULL; cur = cur->next) {
        if (!free_node_ok(cur) || n == MYALLOC_MAX_FREE_NODES) {
            report_corrupt();
            return;
        }
        ordered[n++] = cur;
    }

    if (n <= 1)
        return;

    qsort(ordered, n, sizeof ordered[0], freelist_addr_cmp);

    /* Reject overlap before any next pointer is rewritten. */
    for (i = 0; i + 1 < n; i++) {
        char *end = free_block_end(ordered[i]);

        if (end > (char *)ordered[i + 1]) {
            report_corrupt();
            return;
        }
    }

    /*
     * Rebuild a single address-ordered chain. Adjacent means the next free
     * node starts at the previous node's end. The absorbed header becomes
     * payload:
     *   size += sizeof(node_t) + next->size
     * which is the 1024 + sizeof(header_t) result test 5.2 expects when
     * two 512-byte neighbors merge. One pass, and absorbed nodes are not
     * visited again (the old list[100] loop wrote through them).
     */
    tail = ordered[0];
    tail->next = NULL;
    for (i = 1; i < n; i++) {
        node_t *rhs = ordered[i];

        if (free_block_end(tail) == (char *)rhs) {
            tail->size += (unsigned long)sizeof(node_t) + rhs->size;
        } else {
            tail->next = rhs;
            tail = rhs;
            tail->next = NULL;
        }
    }
    __head = ordered[0];
}

void print_header(const header_t *header)
{
    if (header == NULL) {
        printf("[header_t @ (nil)]\n");
        return;
    }
    printf("[header_t @ %p | buffer @ %p size: %lu magic: %08lx]\n",
           (const void *)header,
           (const void *)((const char *)header + sizeof(header_t)),
           header->size,
           header->magic);
}

void print_node(const node_t *node)
{
    if (node == NULL) {
        printf("[node @ (nil)]\n");
        return;
    }
    printf("[node @ %p | free region @ %p size: %lu next: %p]\n",
           (const void *)node,
           (const void *)((const char *)node + sizeof(node_t)),
           node->size,
           (const void *)node->next);
}

void print_freelist_from(const node_t *node)
{
    size_t seen = 0;

    printf("\nPrinting freelist from %p\n", (const void *)node);
    while (node != NULL && seen++ < MYALLOC_MAX_FREE_NODES) {
        print_node(node);
        node = node->next;
    }
}

void print_heap(void)
{
    node_t *free_nodes[MYALLOC_MAX_FREE_NODES];
    size_t nfree = 0;
    size_t fi = 0;
    char *p;
    char *hi;
    node_t *cur;

    if (__heap == NULL) {
        printf("heap is not initialized\n");
        return;
    }

    for (cur = __head; cur != NULL; cur = cur->next) {
        if (!free_node_ok(cur) || nfree == MYALLOC_MAX_FREE_NODES) {
            report_corrupt();
            return;
        }
        free_nodes[nfree++] = cur;
    }
    if (nfree > 1)
        qsort(free_nodes, nfree, sizeof free_nodes[0], freelist_addr_cmp);

    p = heap_lo();
    hi = heap_hi();
    printf("\nHeap %p .. %p (%d bytes)\n", (void *)__heap, (void *)hi, HEAPSIZE);
    while (p < hi) {
        if (fi < nfree && (char *)free_nodes[fi] == p) {
            node_t *node = free_nodes[fi++];

            printf("  free  @ %p payload %lu\n", (void *)node, node->size);
            p = free_block_end(node);
            continue;
        }
        if ((size_t)(hi - p) < sizeof(header_t)) {
            printf("  gap   @ %p %zu bytes\n", (void *)p, (size_t)(hi - p));
            break;
        }

        header_t *hdr = (header_t *)p;
        if (hdr->magic != HEAPMAGIC ||
            (char *)hdr + sizeof(header_t) + hdr->size > hi) {
            printf("  ???   @ %p unrecognized\n", (void *)p);
            break;
        }
        printf("  used  @ %p payload %lu\n",
               (void *)(p + sizeof(header_t)), hdr->size);
        p += sizeof(header_t) + hdr->size;
    }
}

void myalloc_dump_stats(void)
{
    printf("myalloc stats: inits %lu  alloc_ok %lu  alloc_fail %lu  "
           "free_ok %lu  free_reject %lu  coalesces %lu  "
           "requested %lu  granted %lu\n",
           stats.heap_inits, stats.alloc_ok, stats.alloc_fail,
           stats.free_ok, stats.free_reject, stats.coalesces,
           stats.bytes_requested, stats.bytes_granted);
}
