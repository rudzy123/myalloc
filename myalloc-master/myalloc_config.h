#ifndef MYALLOC_CONFIG_H
#define MYALLOC_CONFIG_H

/*
 * Compile-time configuration for the educational allocator.
 *
 * The defaults below are the alligator.c contract: first-fit, insert at
 * the head, explicit coalesce_freelist(), 8-byte rounding, no lock.
 * `make run` uses this file as it is. Override a knob from the compiler
 * command line (-D wins over the defaults here) or edit the defaults.
 *
 *   make run CFLAGS="-Wall -Wextra -std=c11 -g -DMYALLOC_FIT=1"
 *
 * Which combinations pass the suite is recorded in DESIGN.md. Short version:
 *
 *   MYALLOC_FIT=0 (first)     passes   required default
 *   MYALLOC_FIT=1 (best)      passes   smallest fit matches the checked sizes
 *   MYALLOC_FIT=2 (next)      FAILS test 7.3
 *                             the cursor is on the large tail, so the 14-byte
 *                             request splits that tail (remainder 2960) and
 *                             leaves the 16-byte node free. Expected: one
 *                             node of 2992. Address order does not change this.
 *   MYALLOC_ALIGN=8           passes   required default
 *   MYALLOC_ALIGN=16          FAILS test 5.3
 *                             1000 rounds to 1008 and the remainder becomes 16,
 *                             not the 24 the test records
 *   MYALLOC_AUTO_COALESCE=0   passes   required default
 *   MYALLOC_AUTO_COALESCE=1   FAILS test 5.1
 *                             the driver exits there. Test 6.1 is the same
 *                             check and is not reached. Neighbors are already
 *                             one 1040-byte node instead of two 512-byte nodes.
 *   MYALLOC_ADDR_ORDER=0      passes   LIFO insert-at-head, required default
 *   MYALLOC_ADDR_ORDER=1      passes the current suite
 *                             freelist checks compare a multiset of sizes.
 *                             It still changes which block first-fit sees
 *                             first; see DESIGN.md before relying on it.
 *   MYALLOC_LOCK=0            passes   required default, no atomics
 *   MYALLOC_LOCK=1            passes the suite (single threaded)
 *                             C11 spinlock around the allocator entry points
 *   MYALLOC_ATEXIT=1          passes   destroy_heap runs at process exit
 *   MYALLOC_ATEXIT=0          passes   the final 4096-byte region stays
 *                             reachable until the process ends
 *   MYALLOC_DEBUG=1           passes   extra tracing on stderr
 *
 * MYALLOC_FIT=2 with MYALLOC_ADDR_ORDER=1 still fails test 7.3, for the
 * same cursor reason. Coalesce drops that cursor before it rewrites links,
 * so a merge cannot leave it pointing into another node's payload. A
 * coalesce of 0 or 1 free nodes does not move the cursor.
 */

/* 1: trace init/alloc/free/coalesce on stderr. Suite: passes (stdout is
 * unchanged). Default 0. */
#ifndef MYALLOC_DEBUG
#define MYALLOC_DEBUG 0
#endif

#define MYALLOC_FIT_FIRST 0
#define MYALLOC_FIT_BEST  1
#define MYALLOC_FIT_NEXT  2

/* Free-list search policy.
 * 0 first-fit: required default, passes.
 * 1 best-fit:  smallest block that fits; passes.
 * 2 next-fit:  resumes from a cursor; FAILS test 7.3. */
#ifndef MYALLOC_FIT
#define MYALLOC_FIT MYALLOC_FIT_FIRST
#endif

/* Request rounding, a power of two. 8 is required. 16 FAILS test 5.3
 * (split remainder 16 instead of 24). */
#ifndef MYALLOC_ALIGN
#define MYALLOC_ALIGN 8
#endif

/* 0: coalesce only when coalesce_freelist() is called (required).
 * 1: myfree() coalesces; FAILS test 5.1 (neighbors merge too early). */
#ifndef MYALLOC_AUTO_COALESCE
#define MYALLOC_AUTO_COALESCE 0
#endif

/* 0: insert at head (LIFO), required default.
 * 1: keep the free list in address order; passes the current suite only
 *    because test 7's freed node sits below the tail (see DESIGN.md). */
#ifndef MYALLOC_ADDR_ORDER
#define MYALLOC_ADDR_ORDER 0
#endif

/* 0: no synchronization; lock calls compile to nothing, no <stdatomic.h>
 *    (required default).
 * 1: recursive C11 spinlock around entry points; passes the suite on one
 *    thread. Not a thread-safety claim: direct __head reads are unguarded. */
#ifndef MYALLOC_LOCK
#define MYALLOC_LOCK 0
#endif

/*
 * 1: init_heap registers destroy_heap with atexit (default; passes).
 * 0: no handler; the last region stays reachable until exit (passes).
 * The handler is idempotent, so an explicit destroy_heap still works.
 */
#ifndef MYALLOC_ATEXIT
#define MYALLOC_ATEXIT 1
#endif

#endif
