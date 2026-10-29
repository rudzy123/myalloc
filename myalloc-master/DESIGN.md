# Allocator design

`alligator.c` does not check a general-purpose malloc. It checks one memory model. This file records that model and the choices that keep the suite passing.

## Layout

On this ABI both metadata records are 16 bytes, and the second field is at offset 8:

```text
allocated: [ size | magic | user bytes ... ]
free:      [ size | next  | unused bytes ... ]
```

`size` is only the bytes after the record. A free node that occupies `16 + size` bytes becomes an allocated block of `16 + size` bytes, and the other way around. `myfree` writes the freelist link over `magic`, so a second free of the same pointer no longer sees `HEAPMAGIC` and prints `The heap is corrupt!`.

The heap is one 4096-byte `malloc` region. It starts as a single free node whose payload is `HEAPSIZE - sizeof(header_t)` (4080). Those two widths are equal, which the code checks with `_Static_assert`. Every later split and merge preserves:

```text
sum(sizeof(record) + size) == 4096
```

## Rules the tests actually check

Split a free node when `payload - request >= sizeof(node_t)`. The leftover node has size `payload - request - sizeof(header_t)`, which may be 0. Test 6 allocates 1024 from a 1040-byte node and requires that 0-byte node. A strict `>` test fails it.

If the leftover is smaller than a node, the caller gets the whole payload and no new free node is created. Test 7 allocates 14 bytes from a 16-byte node, then reads 16 bytes and expects the header size to be at least 16. The 16-byte node must disappear from the freelist.

`myfree` inserts at the head and does not merge. Test 7 then depends on first-fit seeing that 16-byte node before the large tail. Tests 5.1 and 6.1 free two neighbors and require them to still be separate until `coalesce_freelist()`.

Coalesce sorts free nodes by address and merges a pair only when the next node begins at `node + sizeof(node_t) + size`. The surviving size grows by `sizeof(node_t) + neighbor->size`. Two 512-byte neighbors become `1024 + sizeof(header_t)`.

Failed allocations return NULL and leave the list alone (test 2, the 3000-byte request). `destroy_heap` drops the region so the next `myalloc` builds a fresh one.

## What changed relative to the original

The checked-in `myalloc.c` did not compile (`#include NULL`, the header included after the code, `__heap` never defined). The behavior above is the original allocator from the first working commit, with these corrections:

- The coalesce scratch list was `node_t *list[100]`. A free node is at least 16 bytes, so the heap can hold 256 of them. The list is now `HEAPSIZE / sizeof(node_t)` long, and a longer walk is treated as corruption instead of a buffer overflow. Absorbed nodes are not written again.
- The original `first_fit` refused the last free node when it was large enough for the request but too small to split. Test 7 only covers that "take the whole block" path when another free node follows. The last node now follows the same rule.
- `myfree(NULL)` returns. A pointer outside the heap, or one whose header is misaligned, is rejected before it is dereferenced. `init_heap` leaves the allocator usable and returns failure to `myalloc` instead of exiting.
- Requests are rounded up to 8 bytes. `node_t` requires 8-byte alignment; an odd size places the next header on a misaligned address, which is undefined. Every successful size in the suite is already a multiple of 8, so the checked freelist numbers do not move. Rounding to 16 changes the test 5.3 remainder from 24 to 16 and fails the suite.
- Best-fit is `-DMYALLOC_FIT=1` and passes the suite. Next-fit is `-DMYALLOC_FIT=2` and fails test 7, because the cursor is sitting on the large tail when the 16-byte node is freed. Automatic coalesce is `-DMYALLOC_AUTO_COALESCE=1` and fails tests 5.1 and 6.1.

`mycalloc` and `myrealloc` are new. Growth allocates, copies, and frees. Shrink splits the tail when the spare bytes can hold a free node, using the same threshold as allocation.

## Still true after the refactor

Free-list insertion stays O(1) and unsorted, so coalesce stays O(n log n). An address-ordered freelist would make the merge a linear scan, and it would also change which block first-fit sees first. The suite does not require that change.

There is no per-thread heap and no lock. Two threads calling `myalloc` will race on `__head`.

`print_heap` prints an address-ordered text map. It does not draw a picture.

The last test in `alligator.c` does not call `destroy_heap`, so the process still exits holding one 4096-byte region. That is the test driver, not a leak inside a free.
