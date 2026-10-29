# Custom Memory Allocator (C)

**Authors:** Rudolf Musika, Alfonso Sainz

A small educational `malloc`/`free` in C. It manages one fixed 4096-byte heap with an explicit free list, splits blocks on allocation, merges neighbors on request, and detects double frees with a magic number. It is validated by the course test driver `alligator.c` (tests 0-7), which checks exact block sizes, so the allocator follows a specific memory model rather than being a general-purpose `malloc`.

**Status:** stable and tested. See [STATUS.md](STATUS.md).

## Build and run

```bash
make          # build ./allocator (myalloc.c + alligator.c)
make run      # run alligator.c, tests 0-7. Does not build stress.c
make san      # the same suite under AddressSanitizer + UBSan
make stress   # separate harness: init/destroy cycles, 151-node coalesce, calloc/realloc
make clean
```

Expected: `make run` ends with `Test 7.3: Passed. I could not find a Memory Leak :)` and exit code 0; `make stress` ends with `stress: ok`. After changing a compile-time flag, run `make clean` first, because Make does not rebuild just because `CFLAGS` changed.

## Memory model

```text
allocated: [ header_t: size | magic ][ user bytes ...      ]
free:      [ node_t:   size | next  ][ unused payload ...  ]
```

- Both records are 16 bytes (checked with `_Static_assert`); `size` is the first field of both, and `magic` and `next` share an offset and width.
- `size` is the payload only, never the whole block.
- The heap is one 4096-byte `malloc` region that starts as a single free node of payload `4096 - 16 = 4080`.
- `myfree` turns a header into a node by writing `next` over `magic`. A second free no longer sees `HEAPMAGIC`, prints `The heap is corrupt!` on stdout, and a reason on stderr.

### Invariants the test suite requires

1. **Split** when `payload - request >= sizeof(node_t)`. The leftover node has `size = payload - request - sizeof(header_t)`, which may be 0 (test 6 needs a 0-byte node; `>` instead of `>=` fails it).
2. **No split** otherwise: the caller gets the whole payload and no fragment is created (test 7: 14 bytes from a 16-byte node leaves the header size at 16).
3. **First-fit** search over the free list.
4. **`myfree` inserts at the head and never merges.** Tests 5.1/6.1 need freed neighbors to stay separate, and test 7 needs the just-freed 16-byte node to be seen before the large tail.
5. **Merge only on `coalesce_freelist()`**, and only when `next == node + sizeof(node_t) + size`. The survivor grows by `sizeof(node_t) + neighbor->size`: two 512-byte neighbors become 1040.
6. **Requests round up to 8 bytes.** Every size the suite checks is already a multiple of 8 (test 7's 14 is absorbed by invariant 2).
7. A failed allocation returns `NULL` and leaves the list unchanged (test 2, 3000 bytes).
8. The freelist checker compares the multiset of payload sizes, not link order.

The reasoning, with the arithmetic for each test, is in [DESIGN.md](DESIGN.md).

## Experimental compile-time flags

All flags live in `myalloc_config.h` with a short comment each. A `-D` on the command line overrides the default:

```bash
make clean
make run CFLAGS="-Wall -Wextra -std=c11 -g -DMYALLOC_FIT=1"
```

| Flag | Default | Result on `alligator.c` |
| --- | --- | --- |
| `MYALLOC_FIT=0` first-fit | yes | passes |
| `MYALLOC_FIT=1` best-fit | | passes |
| `MYALLOC_FIT=2` next-fit | | **fails** test 7.3 (sizes 2960 and 16, expected 2992) |
| `MYALLOC_ALIGN=8` | yes | passes |
| `MYALLOC_ALIGN=16` | | **fails** test 5.3 (remainder 16, expected 24) |
| `MYALLOC_AUTO_COALESCE=0` | yes | passes |
| `MYALLOC_AUTO_COALESCE=1` | | **fails** test 5.1 (neighbors already merged) |
| `MYALLOC_ADDR_ORDER=0` insert at head | yes | passes |
| `MYALLOC_ADDR_ORDER=1` address-ordered list | | passes, but by coincidence (see below) |
| `MYALLOC_LOCK=0` | yes | passes, no atomics |
| `MYALLOC_LOCK=1` spinlock | | passes (single thread) |
| `MYALLOC_ATEXIT=1` | yes | passes |
| `MYALLOC_ATEXIT=0` | | passes |
| `MYALLOC_DEBUG=1` trace to stderr | | passes |

`MYALLOC_ADDR_ORDER=1` passes because test 7's freed block sits at a lower address than the tail, so both orderings hand first-fit the same block. It changes which block first-fit sees first in general, so it is not the default.

## Lifetime

`init_heap` runs on the first `myalloc`, is idempotent, and registers `destroy_heap` with `atexit` (unless `MYALLOC_ATEXIT=0`). `destroy_heap` is idempotent and safe before init; the next allocation builds a fresh heap.

## Limitations

- **Unsorted free list.** Insert-at-head is O(1) but coalescing sorts the nodes first (O(n log n)).
- **8-byte alignment**, not 16. Not suitable for types needing stricter alignment.
- **No lock by default.** `MYALLOC_LOCK=1` adds a spinlock, but direct reads of `__head` are unguarded and it is not signal-safe. This is not a thread-safety claim.
- **Process-global, fixed-size heap.** One 4096-byte region, no growth, no per-thread heaps.

## Files

```text
myalloc-master/
├── myalloc.c          # allocator
├── myalloc.h          # records and prototypes
├── myalloc_config.h   # compile-time flags, with pass/fail notes
├── alligator.c        # test suite (tests 0-7)
├── stress.c           # extra harness, not part of make run
├── DESIGN.md          # current status, invariants, and why
├── STATUS.md          # stability statement
└── Makefile
```

## What I learned

- How real-world allocators manage heap memory
- Why consistent data-structure invariants matter
- How small pointer-arithmetic mistakes become corruption and segfaults

## Reference

Operating Systems: Three Easy Pieces (OSTEP), Chapter 17
