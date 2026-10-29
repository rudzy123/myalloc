#include "myalloc.h"
#include "myalloc_config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MYALLOC_LOCK
#include <stdatomic.h>
#endif

/*
 * Knobs and the pass/fail matrix live in myalloc_config.h.
 * Defaults: first-fit, insert-at-head, explicit coalesce, 8-byte align.
 */

#if MYALLOC_DEBUG
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

/*
 * A free node occupies at least sizeof(node_t) bytes, even when its
 * payload size is 0. A HEAPSIZE-byte heap therefore holds at most
 * HEAPSIZE / sizeof(node_t) free nodes (256 when the record is 16 bytes
 * and the heap is 4096). The coalesce scratch array is exactly that long,
 * so a healthy list cannot write past it. A longer walk is a cycle or a
 * corrupt length and is reported instead of overflowing. This replaces
 * the old node_t *list[100], which overflowed once more than 100 free
 * nodes existed before coalescing.
 */
enum { MYALLOC_MAX_FREE_NODES = HEAPSIZE / (int)sizeof(node_t) };

_Static_assert(sizeof(header_t) == sizeof(node_t),
               "split/coalesce math assumes header_t and node_t are the same size");
_Static_assert(offsetof(header_t, size) == 0 && offsetof(node_t, size) == 0,
               "size must be the first field of both metadata records");
_Static_assert(offsetof(header_t, magic) == offsetof(node_t, next),
               "myfree overwrites magic with the freelist link");
_Static_assert(sizeof(((header_t){0}).magic) == sizeof(((node_t){0}).next),
               "magic and the freelist link must be the same width");
_Static_assert((MYALLOC_ALIGN & (MYALLOC_ALIGN - 1)) == 0 && MYALLOC_ALIGN != 0,
               "MYALLOC_ALIGN must be a power of two");
_Static_assert(MYALLOC_ALIGN % _Alignof(node_t) == 0,
               "MYALLOC_ALIGN must keep free-node headers aligned");
_Static_assert(MYALLOC_ALIGN >= _Alignof(node_t),
               "MYALLOC_ALIGN must be at least the alignment of node_t");
_Static_assert(MYALLOC_MAX_FREE_NODES >= 1,
               "heap cannot hold a free node");
_Static_assert((size_t)MYALLOC_MAX_FREE_NODES * sizeof(node_t) <= (size_t)HEAPSIZE,
               "free-node scratch bound does not fit in the heap");
_Static_assert((size_t)(MYALLOC_MAX_FREE_NODES + 1) * sizeof(node_t) > (size_t)HEAPSIZE,
               "free-node scratch bound is not tight");
_Static_assert(MYALLOC_MAX_FREE_NODES > 100,
               "scratch list must hold the >100 free-node stress case");
#if MYALLOC_FIT != MYALLOC_FIT_FIRST && MYALLOC_FIT != MYALLOC_FIT_BEST && \
    MYALLOC_FIT != MYALLOC_FIT_NEXT
#error "MYALLOC_FIT must be 0 (first), 1 (best), or 2 (next)"
#endif
#if MYALLOC_ADDR_ORDER != 0 && MYALLOC_ADDR_ORDER != 1
#error "MYALLOC_ADDR_ORDER must be 0 (LIFO) or 1 (address order)"
#endif
#if MYALLOC_AUTO_COALESCE != 0 && MYALLOC_AUTO_COALESCE != 1
#error "MYALLOC_AUTO_COALESCE must be 0 or 1"
#endif
#if MYALLOC_LOCK != 0 && MYALLOC_LOCK != 1
#error "MYALLOC_LOCK must be 0 or 1"
#endif
#if MYALLOC_ATEXIT != 0 && MYALLOC_ATEXIT != 1
#error "MYALLOC_ATEXIT must be 0 or 1"
#endif
#if MYALLOC_DEBUG != 0 && MYALLOC_DEBUG != 1
#error "MYALLOC_DEBUG must be 0 or 1"
#endif

void *__heap = NULL;
node_t *__head = NULL;

#if MYALLOC_FIT == MYALLOC_FIT_NEXT
/* Block where the next next-fit search starts. Not used by the default. */
static node_t *rover = NULL;
/* Remainder created by the take_block call in progress, or NULL. */
static node_t *split_remainder = NULL;
#endif

#if MYALLOC_LOCK
/*
 * Recursive so mycalloc/myrealloc can call myalloc/myfree while holding
 * the lock. Off unless MYALLOC_LOCK=1; the default build does not take it
 * and does not link pthread. Not signal-safe. Direct reads of __head from
 * outside the allocator (alligator.c does this) are not covered.
 */
static atomic_flag heap_lock_flag = ATOMIC_FLAG_INIT;
static _Thread_local int heap_lock_depth;

static void heap_lock(void)
{
    if (heap_lock_depth > 0) {
        heap_lock_depth++;
        return;
    }
    while (atomic_flag_test_and_set_explicit(&heap_lock_flag,
                                             memory_order_acquire)) {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
    heap_lock_depth = 1;
}

static void heap_unlock(void)
{
    if (heap_lock_depth > 1) {
        heap_lock_depth--;
        return;
    }
    if (heap_lock_depth == 1) {
        heap_lock_depth = 0;
        atomic_flag_clear_explicit(&heap_lock_flag, memory_order_release);
    }
}
#else
#define heap_lock() ((void)0)
#define heap_unlock() ((void)0)
#endif

#if MYALLOC_FIT == MYALLOC_FIT_NEXT
/* A merge can absorb the node rover points at. Drop it before rewriting. */
#define reset_next_fit_cursor() do { rover = NULL; } while (0)
#else
#define reset_next_fit_cursor() ((void)0)
#endif

static int exit_handler_installed;

#define CORRUPT_BAD_NODE "free-list node is outside the heap, misaligned, or mis-sized"
#define CORRUPT_TOO_LONG "free list is longer than the heap can hold"
#define CORRUPT_OVERLAP  "free-list nodes overlap"
#define CORRUPT_NOT_LIVE "double free or invalid pointer"
#define CORRUPT_UNORDERED "free list is not in address order"

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

static void report_corrupt(const char *detail)
{
    /*
     * Test 4 looks for this sentence on stdout. The detail goes to stderr
     * so the checked line stays exactly "The heap is corrupt!".
     */
    printf("The heap is corrupt!\n");
    fflush(stdout);
    if (detail != NULL && detail[0] != '\0')
        fprintf(stderr, "myalloc: %s\n", detail);
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
        report_corrupt(CORRUPT_TOO_LONG);
        return 1;
    }
    return 0;
}

/*
 * Default (MYALLOC_ADDR_ORDER=0): push at the head. O(1), LIFO.
 * Test 7's 14-byte request must see the just-freed 16-byte node before
 * the large tail. Do not reorder this branch.
 *
 * Address-order mode splices by address so coalesce can merge in one
 * forward scan. First-fit then prefers the lowest address.
 */
static void insert_freenode(node_t *node)
{
#if MYALLOC_ADDR_ORDER
    node_t *prev = NULL;
    node_t *cur = __head;
    size_t seen = 0;

    while (cur != NULL && (uintptr_t)cur < (uintptr_t)node) {
        int bad = !free_node_ok(cur);

        if (!bad)
            seen++;
        if (bad || seen > MYALLOC_MAX_FREE_NODES) {
            report_corrupt(bad ? CORRUPT_BAD_NODE : CORRUPT_TOO_LONG);
            node->next = __head;
            __head = node;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
    if (cur != NULL && !free_node_ok(cur)) {
        report_corrupt(CORRUPT_BAD_NODE);
        node->next = __head;
        __head = node;
        return;
    }
    node->next = cur;
    if (prev == NULL)
        __head = node;
    else
        prev->next = node;
#else
    node->next = __head;
    __head = node;
#endif
}

/*
 * MERGE ARITHMETIC: rhs starts exactly where lhs ends, i.e.
 *   (char *)lhs + sizeof(node_t) + lhs->size == (char *)rhs.
 * rhs's record becomes payload of lhs:
 *   lhs->size += sizeof(node_t) + rhs->size
 * Two 512-byte neighbors become 512 + 16 + 512 = 1040 (tests 5.2, 6.2).
 * The caller checks adjacency and unlinks rhs.
 */
static void absorb_neighbor(node_t *lhs, const node_t *rhs)
{
    lhs->size += (unsigned long)sizeof(node_t) + rhs->size;
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
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
    split_remainder = NULL;
#endif

    if (prev != NULL)
        prev->next = next;
    else
        __head = next;

    hdr = (header_t *)block;
    /* Same bytes myfree will overwrite with the freelist link. */
    hdr->magic = HEAPMAGIC;

    spare = (size_t)free_size - request;
    /*
     * SPLIT ARITHMETIC (sizes are payload-only; both records are 16 bytes):
     *   spare     = block->size - request
     *   split iff spare >= sizeof(node_t)       (>=, so a 0-byte node is legal)
     *   rem       = (char *)hdr + sizeof(header_t) + request
     *   rem->size = spare - sizeof(header_t)    (the new node's own record
     *                                            comes out of the spare)
     * Test 6: 1040 - 1024 = 16 -> rem->size 0. Test 5.3: 1040 - 1000 = 40
     * -> rem->size 24. Otherwise no split: the caller keeps the whole block.
     */
    if (spare >= sizeof(node_t)) {
        node_t *rem = (node_t *)((char *)hdr + sizeof(header_t) + request);

        assert((char *)rem >= heap_lo());
        assert((char *)rem + sizeof(node_t) <= heap_hi());
        assert(((uintptr_t)rem % _Alignof(node_t)) == 0);

        rem->size = (unsigned long)(spare - sizeof(header_t));
        insert_freenode(rem);
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
        split_remainder = rem;
#endif
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
            report_corrupt(CORRUPT_BAD_NODE);
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
            report_corrupt(CORRUPT_BAD_NODE);
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
                report_corrupt(CORRUPT_BAD_NODE);
                return NULL;
            }
            if (too_many_nodes(++seen))
                return NULL;
            walk_prev = walk;
            walk = walk->next;
        }
        if (walk == rover) {
            if (!free_node_ok(walk)) {
                report_corrupt(CORRUPT_BAD_NODE);
                return NULL;
            }
            prev = walk_prev;
            cur = walk;
        }
    }

    if (!free_node_ok(cur)) {
        report_corrupt(CORRUPT_BAD_NODE);
        return NULL;
    }

    start = cur;
    seen = 0;
    for (;;) {
        node_t *after;
        void *mem;

        if (!free_node_ok(cur)) {
            report_corrupt(CORRUPT_BAD_NODE);
            return NULL;
        }
        if (too_many_nodes(++seen))
            return NULL;
        if ((size_t)cur->size >= request) {
            /*
             * Do not leave rover on the block that is about to become a
             * header. A split resumes at the remainder. Insert-at-head
             * pushes that remainder to __head, so this is the old
             * `rover = __head` assignment when MYALLOC_ADDR_ORDER is off.
             */
            after = cur->next;
            mem = take_block(prev, cur, request);
            if (split_remainder != NULL)
                rover = split_remainder;
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

int myalloc_atexit_install(void)
{
    if (exit_handler_installed)
        return 0;
    if (atexit(destroy_heap) != 0)
        return -1;
    exit_handler_installed = 1;
    return 0;
}

void init_heap(void)
{
    node_t *head;

    heap_lock();
    if (__heap != NULL)
        goto out;

    __head = NULL;
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
    rover = NULL;
    split_remainder = NULL;
#endif

    __heap = malloc(HEAPSIZE);
    if (__heap == NULL) {
        fprintf(stderr, "Couldn't initialize heap!\n");
        __head = NULL;
        goto out;
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

#if MYALLOC_ATEXIT
    /* Idempotent. A later destroy_heap, including this handler, is safe. */
    (void)myalloc_atexit_install();
#endif
out:
    heap_unlock();
}

void destroy_heap(void)
{
    void *heap;

    heap_lock();
    /*
     * Null the globals before free so a second call, including the atexit
     * handler after an explicit destroy, does not free the region twice.
     */
    if (__heap == NULL) {
        __head = NULL;
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
        rover = NULL;
        split_remainder = NULL;
#endif
        goto out;
    }

    heap = __heap;
    __heap = NULL;
    __head = NULL;
#if MYALLOC_FIT == MYALLOC_FIT_NEXT
    rover = NULL;
    split_remainder = NULL;
#endif
    free(heap);
out:
    heap_unlock();
}

void *myalloc(size_t size)
{
    size_t request;
    void *ptr = NULL;

    heap_lock();
    if (__heap == NULL)
        init_heap();
    if (__heap == NULL) {
        stats.alloc_fail++;
        goto out;
    }

    request = adjust_request(size);
    DBG("myalloc(%zu) request %zu head %p\n", size, request, (void *)__head);
    ptr = search_freelist(request);
    if (ptr == NULL) {
        stats.alloc_fail++;
        DBG("myalloc(%zu) failed\n", size);
        goto out;
    }

    stats.alloc_ok++;
    stats.bytes_requested += size;
    stats.bytes_granted += get_header(ptr)->size;
out:
    heap_unlock();
    return ptr;
}

void myfree(void *ptr)
{
    header_t *hdr;
    node_t *node;
    unsigned long bytes;

    heap_lock();
    if (ptr == NULL)
        goto out;

    if (__heap == NULL) {
        stats.free_reject++;
        report_corrupt("heap is not initialized");
        goto out;
    }

    hdr = live_header(ptr);
    if (hdr == NULL) {
        stats.free_reject++;
        report_corrupt(CORRUPT_NOT_LIVE);
        goto out;
    }

    /*
     * Keep the payload size, then overlay a node on the header. Writing
     * next replaces magic, which is what makes the next free fail.
     * Insert at the head (O(1)) unless address order is compiled in.
     * Test 7 depends on that LIFO order for first-fit: the 16-byte block
     * just freed must be tried before the large tail. Coalescing here
     * would merge blocks the suite still expects to see as separate nodes.
     */
    bytes = hdr->size;
    node = (node_t *)hdr;
    node->size = bytes;
    insert_freenode(node);
    stats.free_ok++;
    DBG("myfree(%p) size %lu\n", ptr, bytes);

#if MYALLOC_AUTO_COALESCE
    coalesce_freelist();
#endif
out:
    heap_unlock();
}

void *mycalloc(size_t nmemb, size_t size)
{
    void *ptr = NULL;
    header_t *hdr;

    heap_lock();
    if (size != 0 && nmemb > SIZE_MAX / size)
        goto out;

    ptr = myalloc(nmemb * size);
    if (ptr == NULL)
        goto out;

    /* Zero the whole granted block, which may be larger than nmemb * size. */
    hdr = get_header(ptr);
    memset(ptr, 0, hdr->size);
out:
    heap_unlock();
    return ptr;
}

void *myrealloc(void *ptr, size_t size)
{
    header_t *hdr;
    size_t request;
    size_t spare;
    void *fresh = NULL;

    heap_lock();
    if (ptr == NULL) {
        fresh = myalloc(size);
        goto out;
    }
    if (size == 0) {
        myfree(ptr);
        goto out;
    }
    if (__heap == NULL) {
        report_corrupt("heap is not initialized");
        goto out;
    }

    hdr = live_header(ptr);
    if (hdr == NULL) {
        report_corrupt(CORRUPT_NOT_LIVE);
        goto out;
    }

    request = adjust_request(size);
    if ((size_t)hdr->size >= request) {
        /*
         * Shrink in place when the tail can hold its own free node.
         * A smaller tail stays with the caller, same rule as allocation.
         * spare = hdr->size - request; split iff spare >= sizeof(node_t);
         * rem = ptr + request; rem->size = spare - sizeof(header_t).
         * Same formulas as take_block.
         */
        spare = (size_t)hdr->size - request;
        if (spare >= sizeof(node_t)) {
            node_t *rem = (node_t *)((char *)ptr + request);

            assert(((uintptr_t)rem % _Alignof(node_t)) == 0);
            rem->size = (unsigned long)(spare - sizeof(header_t));
            insert_freenode(rem);
            hdr->size = (unsigned long)request;
        }
        fresh = ptr;
        goto out;
    }

    fresh = myalloc(size);
    if (fresh == NULL)
        goto out;
    memcpy(fresh, ptr, hdr->size);
    myfree(ptr);
out:
    heap_unlock();
    return fresh;
}

#if MYALLOC_ADDR_ORDER
/*
 * The free list is already in address order, so merging is one forward
 * scan. Same absorb rule as the sort path. Validate the whole list before
 * rewriting any next pointer.
 */
static void coalesce_linear(void)
{
    node_t *prev = NULL;
    node_t *cur;
    size_t n = 0;

    for (cur = __head; cur != NULL; prev = cur, cur = cur->next) {
        if (!free_node_ok(cur)) {
            report_corrupt(CORRUPT_BAD_NODE);
            return;
        }
        if (n == MYALLOC_MAX_FREE_NODES) {
            report_corrupt(CORRUPT_TOO_LONG);
            return;
        }
        if (prev != NULL) {
            if ((uintptr_t)cur <= (uintptr_t)prev) {
                report_corrupt(CORRUPT_UNORDERED);
                return;
            }
            if (free_block_end(prev) > (char *)cur) {
                report_corrupt(CORRUPT_OVERLAP);
                return;
            }
        }
        n++;
    }

    if (n <= 1)
        return;

    reset_next_fit_cursor();
    cur = __head;
    while (cur != NULL && cur->next != NULL) {
        /* Adjacent: end of cur == start of next. Merge, unlink next. */
        if (free_block_end(cur) == (char *)cur->next) {
            absorb_neighbor(cur, cur->next);
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}
#endif

void coalesce_freelist(void)
{
#if !MYALLOC_ADDR_ORDER
    node_t *ordered[MYALLOC_MAX_FREE_NODES];
    size_t n = 0;
    size_t i;
    node_t *cur;
    node_t *tail;
#endif

    heap_lock();
    stats.coalesces++;
    DBG("coalesce_freelist\n");

#if MYALLOC_ADDR_ORDER
    coalesce_linear();
    goto out;
#else
    for (cur = __head; cur != NULL; cur = cur->next) {
        if (!free_node_ok(cur)) {
            report_corrupt(CORRUPT_BAD_NODE);
            goto out;
        }
        if (n == MYALLOC_MAX_FREE_NODES) {
            report_corrupt(CORRUPT_TOO_LONG);
            goto out;
        }
        /* Bounded by MYALLOC_MAX_FREE_NODES; see the static asserts. */
        ordered[n++] = cur;
    }

    if (n <= 1)
        goto out;

    qsort(ordered, n, sizeof ordered[0], freelist_addr_cmp);

    /* Reject overlap before any next pointer is rewritten. */
    for (i = 0; i + 1 < n; i++) {
        char *end = free_block_end(ordered[i]);

        if (end > (char *)ordered[i + 1]) {
            report_corrupt(CORRUPT_OVERLAP);
            goto out;
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
    reset_next_fit_cursor();
    tail = ordered[0];
    tail->next = NULL;
    for (i = 1; i < n; i++) {
        node_t *rhs = ordered[i];

        /* Adjacent: end of tail == start of rhs. tail->size grows by
         * sizeof(node_t) + rhs->size and rhs drops out of the chain. */
        if (free_block_end(tail) == (char *)rhs)
            absorb_neighbor(tail, rhs);
        else {
            tail->next = rhs;
            tail = rhs;
            tail->next = NULL;
        }
    }
    __head = ordered[0];
#endif
out:
    heap_unlock();
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

    heap_lock();
    printf("\nPrinting freelist from %p\n", (const void *)node);
    while (node != NULL && seen++ < MYALLOC_MAX_FREE_NODES) {
        print_node(node);
        node = node->next;
    }
    heap_unlock();
}

void print_heap(void)
{
    node_t *free_nodes[MYALLOC_MAX_FREE_NODES];
    size_t nfree = 0;
    size_t fi = 0;
    char *p;
    char *hi;
    node_t *cur;

    heap_lock();
    if (__heap == NULL) {
        printf("heap is not initialized\n");
        goto out;
    }

    for (cur = __head; cur != NULL; cur = cur->next) {
        if (!free_node_ok(cur)) {
            report_corrupt(CORRUPT_BAD_NODE);
            goto out;
        }
        if (nfree == MYALLOC_MAX_FREE_NODES) {
            report_corrupt(CORRUPT_TOO_LONG);
            goto out;
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
out:
    heap_unlock();
}

void myalloc_dump_stats(void)
{
    heap_lock();
    printf("myalloc stats: inits %lu  alloc_ok %lu  alloc_fail %lu  "
           "free_ok %lu  free_reject %lu  coalesces %lu  "
           "requested %lu  granted %lu\n",
           stats.heap_inits, stats.alloc_ok, stats.alloc_fail,
           stats.free_ok, stats.free_reject, stats.coalesces,
           stats.bytes_requested, stats.bytes_granted);
    heap_unlock();
}
