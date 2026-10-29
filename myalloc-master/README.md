# Custom Memory Allocator (C)

**Authors:** Rudolf Musika, Alfonso Sainz  

---

## 📌 Overview

This project implements a custom memory allocator in C, similar to `malloc()` and `free()`.  
The allocator manages a fixed-size heap and dynamically allocates and frees memory using a **free list**.

The system was designed and validated using a comprehensive test suite that checks allocation correctness, memory reuse, and fragmentation handling.

---

## 🧠 Key Concepts

This project demonstrates core systems programming concepts:

- Manual heap management
- Pointer arithmetic
- Memory layout design
- Free list data structures
- Block splitting
- Memory coalescing
- Detection of memory corruption

---

## ⚙️ Memory Layout

Allocated and free memory regions are represented differently:

### 🔹 Allocated Block

``
[ header_t ][ user data ]

- `header_t.size` → size of allocated memory
- `header_t.magic` → used to detect corruption or double free

---

### 🔹 Free Block


[ node_t ][ free space ]

- `node_t.size` → size of free region
- `node_t.next` → pointer to next free block

---

## 🔧 Core Functions

### ✅ `myalloc(size_t size)`
- Finds a suitable free block using **first-fit**
- Splits the block if excess space remains
- Returns pointer to usable memory

---

### ✅ `myfree(void *ptr)`
- Converts an allocated block back into a free block
- Inserts it into the free list
- Detects invalid frees using a magic number

---

### ✅ `coalesce_freelist()`
- Combines adjacent free blocks into larger blocks
- Reduces memory fragmentation
- Uses sorting + merging to ensure correctness

---

## 🧪 Test Suite

The allocator is validated using an extensive test suite (`alligator.c`):

| Test | Purpose |
|------|--------|
| Test 0 | Basic allocation |
| Test 1 | Block splitting |
| Test 2 | Allocation failure |
| Test 3 | Freeing memory |
| Test 4 | Double free detection |
| Test 5 | Coalescing free blocks |
| Test 6 | Edge-case coalescing |
| Test 7 | Memory reuse and leak detection |

---

## 🛠️ Build & Run

```bash
make run     # alligator.c, tests 0–7. Does not build stress.c
make san     # the same suite under address and undefined-behavior sanitizers
make stress  # separate init/destroy, >100-node coalesce, and a small benchmark
make clean
```

Experimental switches are in `myalloc_config.h`. A `-D` on the command line overrides a default there. `make run` after a flag change should start from `make clean`, because Make will not rebuild just because `CFLAGS` changed if the binary is already newer than the sources.

```bash
make clean
make run CFLAGS="-Wall -Wextra -std=c11 -g -DMYALLOC_FIT=1"
```

## Project structure

```text
myalloc/
├── myalloc.c          # allocator
├── myalloc.h          # records and prototypes
├── myalloc_config.h   # compile-time experiments and the pass/fail matrix
├── alligator.c        # test suite
├── stress.c           # not part of make run
├── DESIGN.md          # invariants the suite depends on
├── Makefile
└── README.md
```


## Design notes

- First-fit allocation
- Free blocks stored in a linked list
- Coalescing sorts the free list and merges adjacent regions
- Heap integrity is checked with a magic number

## What I learned

- How real-world memory allocators manage heap memory
- The importance of maintaining consistent data structures
- How small pointer arithmetic mistakes can cause major bugs
- Techniques for debugging segmentation faults and memory corruption


The size rules, freelist order, alignment trade-off, and which experiments pass are written up in `DESIGN.md`.

## Experiments

The default path is first-fit, insert-at-head, and an explicit `coalesce_freelist()`. That is what `make run` checks.

Best-fit (`-DMYALLOC_FIT=1`) passes. Next-fit (`-DMYALLOC_FIT=2`) fails test 7.3. Rounding to 16 bytes fails test 5.3. Coalescing inside `myfree` fails test 5.1. An address-ordered free list is available and passes the current size checks; it is not the default, because it changes which block first-fit sees first. A spinlock can be compiled in with `-DMYALLOC_LOCK=1` and is off by default. `init_heap` registers `destroy_heap` with `atexit` unless `-DMYALLOC_ATEXIT=0`.


📚 Reference

Operating Systems: Three Easy Pieces (OSTEP), Chapter 17