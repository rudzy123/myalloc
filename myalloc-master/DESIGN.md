# Allocator design

## Current status

- **The default path is green.** First-fit, insert-at-head, 8-byte alignment, explicit `coalesce_freelist()`, no lock, `atexit` cleanup on. `make clean && make && make run` passes `alligator.c` tests 0-7, `make san` (address + undefined-behavior sanitizers) is clean, and `make stress` ends with `stress: ok`.
- **Experimental modes that pass the suite:** `MYALLOC_FIT=1` (best-fit), `MYALLOC_ADDR_ORDER=1`, `MYALLOC_LOCK=1` (single thread), `MYALLOC_ATEXIT=0`, `MYALLOC_DEBUG=1`.
- **Experimental modes that fail:** `MYALLOC_FIT=2` (test 7.3), `MYALLOC_ALIGN=16` (test 5.3), `MYALLOC_AUTO_COALESCE=1` (test 5.1). `MYALLOC_FIT=2` with `MYALLOC_ADDR_ORDER=1` still fails 7.3.
- **Known coincidence:** `MYALLOC_ADDR_ORDER=1` passes only because test 7's freed 16-byte block lies at a lower address than the large tail, so address order and insert-at-head show the same block to first-fit first. It is not equivalent to insert-at-head and is not the default. See "Address-ordered free list".
- **Not done on purpose:** address-ordered coalescing without changing first-fit order, 16-byte alignment, thread safety. See `STATUS.md`.

`alligator.c` checks one memory model, not a general-purpose malloc. This file is that contract. The defaults in `myalloc_config.h` are the configuration the suite requires. Everything else is an experiment.

On this ABI both metadata records are 16 bytes. `size` is an `unsigned long` and the second field is a pointer or an `unsigned long`, so the records match. The code does not hard-code 16; it uses `sizeof` and checks the match with `_Static_assert`.

## Layout

```text
allocated: [ size | magic | user bytes ... ]
free:      [ size | next  | unused bytes ... ]
```

`size` is only the bytes after the record. A free node that occupies `sizeof(node_t) + size` bytes becomes an allocated block of `sizeof(header_t) + size` bytes, and the other way around. `myfree` writes the freelist link over `magic`. A second free of the same pointer no longer sees `HEAPMAGIC`.

The heap is one 4096-byte region from `malloc`. It starts as a single free node whose payload is `HEAPSIZE - sizeof(header_t)` (4080). Every later split and merge on a healthy heap preserves:

```text
sum(sizeof(record) + size) == 4096
```

## Invariants the suite depends on

1. `header_t` and `node_t` are the same size. `size` is the first field of both. `magic` and `next` sit at the same offset and have the same width. `myfree` overlays `next` on `magic`.
2. `size` is the payload only, never the whole block.
3. The initial free payload is `HEAPSIZE - sizeof(header_t)`.
4. Split when `payload - request >= sizeof(node_t)`. The leftover node's size is `payload - request - sizeof(header_t)`, and that value may be 0. Test 6 allocates 1024 from a 1040-byte node and requires that 0-byte node. A strict `>` test fails it.
5. If the leftover is smaller than a node, the caller gets the whole payload and no new free node is created. Test 7 allocates 14 bytes from a 16-byte node. The request rounds to 16, the spare is 0, and the header size stays 16. The 16-byte node must disappear from the freelist.
6. `myfree` inserts at the head and does not merge. Tests 5.1 and 6.1 free two neighbors and require them to still be separate until `coalesce_freelist()`.
7. Coalesce merges a pair only when the next node begins at `node + sizeof(node_t) + size`. The surviving size grows by `sizeof(node_t) + neighbor->size`. Two 512-byte neighbors become `1024 + sizeof(header_t)` (1040). That is tests 5.2 and 6.2.
8. A failed allocation returns NULL and leaves the list alone. Test 2's 3000-byte request does not fit.
9. A double free prints exactly `The heap is corrupt!` on stdout, because `next` has replaced `magic`. A short reason is written to stderr (`myalloc: ...`) and is not part of that sentence.
10. The default rounding is 8 bytes. Every successful size the suite checks is already a multiple of 8, except the 14-byte request in test 7, which is satisfied by the whole-block rule above.
11. `is_free_list_good` compares the multiset of payload sizes. It does not compare link order.

`destroy_heap` drops the region so the next `myalloc` builds a fresh one. It is safe before the first `init_heap` and safe to call again. Globals are cleared before `free`, so the second call does not free the region twice.

## Why insert-at-head is required

Test 7 is the one place the suite allocates after a free without calling `coalesce_freelist()`.

After `myalloc(512)`, `myalloc(16)`, and `myalloc(512)` the freelist is one tail whose payload is 2992. `myfree` of the 16-byte block pushes that node at the head, in front of the tail. Both nodes can satisfy a 16-byte request (14 rounded up to 8-byte alignment). First-fit takes the head. The spare is 0, so the whole 16-byte node is consumed and the freelist returns to a single node of 2992. Test 7.3 requires that.

If the tail were searched first, the allocator would split it:

```text
2992 - 16 - sizeof(header_t) == 2960
```

and the original 16-byte node would still be free. That is what next-fit does. Test 7.3 then fails, and the freelist trace shows sizes 2960 and 16 instead of 2992.

Insert-at-head is what puts the just-freed node in front of the tail. The size checker would not notice a pure reordering of equal sizes. It notices this case because the two free nodes have different sizes and first-fit's choice changes the sizes that remain.

Coalesce itself rebuilds an address-ordered chain. After `coalesce_freelist()`, first-fit sees the lowest address first until the next `myfree` or split pushes a node at the head. Tests 5.3 and 6.3 allocate after that rebuild. The tail left by those tests is 432 bytes, and the requests are 1000 and 1024, so only the coalesced 1040-byte node fits. Link order does not decide those two allocations.

## Alignment: 8 versus 16

`node_t` requires 8-byte alignment. An odd request would place the next header on a misaligned address, which is undefined, so requests are rounded up. The default multiple is 8.

Test 5.3 allocates 1000 bytes from the coalesced 1040-byte node:

```text
spare     = 1040 - 1000 = 40
remainder = 40 - sizeof(header_t) = 24
```

The test expects a free node of 24 next to the untouched 432-byte tail.

With `MYALLOC_ALIGN=16`, 1000 rounds to 1008:

```text
spare     = 1040 - 1008 = 32
remainder = 32 - sizeof(header_t) = 16
```

Test 5.3 fails. The trace shows a node of 16 where 24 was required. The 432-byte tail is unchanged, because 512 and 2048 are already multiples of 16. Test 7 still rounds 14 to 16, so alignment 16 does not disturb that test; the suite dies at 5.3.

## Coalesce scratch bound

A free node occupies at least `sizeof(node_t)` bytes, even when its payload is 0. The heap therefore holds at most

```text
HEAPSIZE / sizeof(node_t)
```

free nodes. With a 16-byte record and a 4096-byte heap that bound is 256. `coalesce_freelist` keeps a pointer array of that length. One more minimum-sized node would not fit in the heap, which a static assert checks. Another assert requires the bound to be greater than 100.

The previous scratch array was `node_t *list[100]`. `make stress` builds 151 free nodes (150 freed 8-byte blocks plus the tail) and coalesces them back to one node of 4080. That walk used to overflow the 100-slot array. A walk longer than the bound is reported as corruption instead of writing past the array. Absorbed nodes are not written again.

The default algorithm is still sort-then-merge, which is O(n log n), because the free list is not kept in address order. The merge arithmetic is shared with the address-ordered experiment: `size += sizeof(node_t) + neighbor->size`.

## Address-ordered free list

`MYALLOC_ADDR_ORDER=1` inserts free nodes so addresses increase. Coalesce is then one forward scan and does not call `qsort`. The merge rule is the same. Insertion becomes O(n).

This passes `alligator.c` today, including under address and undefined-behavior sanitizers. It passes because:

- The size checker ignores link order.
- The only allocation that happens after a free and before coalesce is test 7. The just-freed 16-byte node has a lower address than the tail, so address order and insert-at-head both present it to first-fit first.

It is not the default. A free of a higher-address block, while a lower-address block also fits, makes first-fit choose the lower block instead of the one `myfree` just pushed. The current tests never build that pattern. Do not treat a passing run as permission to change the default insertion order.

`MYALLOC_FIT=2` together with address order still fails test 7.3. The next-fit cursor is left on the tail by the three allocations, and the search starts there. Address order does not move that cursor.

## Configuration

All of the switches live in `myalloc_config.h`. Command-line `-D` wins over the defaults. `make run` does not pass any of them.

| Setting | Result on `alligator.c` |
| --- | --- |
| `MYALLOC_FIT=0` first-fit | passes (default) |
| `MYALLOC_FIT=1` best-fit | passes. In this suite the smallest fit is the block the tests require. |
| `MYALLOC_FIT=2` next-fit | fails test 7.3. Freelist sizes are 2960 and 16; the test expects 2992. |
| `MYALLOC_ALIGN=8` | passes (default) |
| `MYALLOC_ALIGN=16` | fails test 5.3. Remainder is 16, not 24. |
| `MYALLOC_AUTO_COALESCE=0` | passes (default) |
| `MYALLOC_AUTO_COALESCE=1` | fails test 5.1. The list holds a 1040-byte node instead of two 512-byte nodes. The driver exits, so test 6.1 is not reached; it is the same check. |
| `MYALLOC_ADDR_ORDER=0` | passes (default). Insert at head. |
| `MYALLOC_ADDR_ORDER=1` | passes the current suite. See above for why that is not the contract. |
| `MYALLOC_LOCK=0` | passes (default). No atomics. |
| `MYALLOC_LOCK=1` | passes the suite and `make stress` on one thread. |
| `MYALLOC_ATEXIT=1` | passes (default). The final heap is released at exit. |
| `MYALLOC_ATEXIT=0` | passes. The process exits with the 4096-byte region still reachable from `__heap`. |
| `MYALLOC_DEBUG=1` | passes. Extra tracing goes to stderr. |

`make clean` before a flag change. Make will not relink `allocator` only because `CFLAGS` changed.

## Lifetime

`init_heap` does nothing when `__heap` is already set, so a second call does not allocate a second region. `destroy_heap` then `init_heap` builds a new single node of payload 4080. `make stress` repeats that cycle.

`MYALLOC_INSTALL_ATEXIT()` (and `myalloc_atexit_install`) registers `destroy_heap` with `atexit`. `init_heap` calls it when `MYALLOC_ATEXIT` is 1. Registering twice is safe. `alligator.c` does not call `destroy_heap` after test 7; the handler releases that last region. Handlers registered after the first `init_heap` run first, so they still see the heap. Handlers registered earlier run after it has been released.

The default `make run` is clean under address and undefined-behavior sanitizers. LeakSanitizer does not report a reachable global, so the interesting check is the stress program: it registers its own `atexit` handler first and aborts if `__heap` or `__head` is still set when that handler runs.

## Lock

`MYALLOC_LOCK=1` wraps the allocator entry points in a recursive C11 spinlock (`atomic_flag`, with `pause` on x86). It is recursive so `mycalloc` and `myrealloc` can call `myalloc` and `myfree`. The default build does not take the lock and does not need `-pthread` or `-latomic`.

The lock does not cover a direct read of `__head`. The test driver does that. It is not safe to call from a signal handler. Do not call `exit` while another thread is inside the allocator: the `atexit` handler will wait for the same lock.

## Error reporting

Corruption still prints this line, and only this line, on stdout:

```text
The heap is corrupt!
```

The reason is a second line on stderr, for example `myalloc: double free or invalid pointer` or `myalloc: heap is not initialized`. `fflush(stdout)` runs first so the required sentence is not stuck behind a fully buffered stdout.

## What is still out of scope

The heap is one process-global 4096-byte region; it does not grow. There is no per-thread heap. Alignment is 8 bytes, not 16. The free list is unsorted by default. The default coalesce still sorts. `print_heap` prints an address-ordered text map; it does not draw a picture. `make stress` is the separate harness (151 free nodes, a gapped coalesce, calloc/realloc, and a short timed loop). It is not part of `make run`.
