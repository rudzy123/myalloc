# Custom Memory Allocator (C)

**Authors:** Rudolf Musika, Alfonso Sainz  

---

## 📌 Overview

This project implements a simplified memory allocator in C, similar to `malloc()` and `free()`.  

The allocator manages a fixed-size heap and uses a **free list** to track available memory regions. It supports allocation, deallocation, and coalescing of adjacent free blocks.

---

## 🧠 Key Concepts

This project demonstrates core systems programming concepts:

- Manual memory management
- Pointer arithmetic
- Free-list data structure
- Block splitting
- Memory coalescing
- Heap integrity checking

---

## ⚙️ Implementation Details

### 🔹 `myalloc(size)`
Allocates a block of memory of the requested size.

Each allocated block contains a header:

[ header_t ][ user data ]

The header stores:
- `size` → size of allocated buffer
- `magic` → used for corruption detection

---

### 🔹 Free List Structure

Free regions are stored as an **embedded linked list**:


[ node_t ][ free space ]

Each node contains:
- `size` → amount of free space
- `next` → pointer to next free block

---

### 🔹 `myfree(ptr)`

Frees a previously allocated block:

1. Converts the header back into a `node_t`
2. Inserts it into the free list (sorted by address)
3. Calls `coalesce_freelist()`

---

### 🔹 `coalesce_freelist()`

Merges adjacent free blocks into larger blocks to reduce fragmentation.

Example:


Before:
[free][free]
After:
[larger free block]

---

## 🧪 Test Suite

The project includes a comprehensive test suite (`alligator.c`) that validates:

| Test | Description |
|------|------------|
| Test 0 | Basic allocation |
| Test 1 | Block splitting |
| Test 2 | Allocation failure |
| Test 3 | Freeing memory |
| Test 4 | Double-free detection |
| Test 5 | Coalescing |
| Test 6 | Edge-case coalescing |
| Test 7 | Memory reuse (no leaks) |

---

## 🛠️ Build & Run

Compile:
```bash
make

Run tests:
Shellmake runShow more lines
Clean build:
Shellmake cleanShow more lines

📦 Project Structure
myalloc/
├── myalloc.c       # allocator implementation
├── myalloc.h       # structures + prototypes
├── alligator.c     # test suite
├── Makefile        # build system
└── README.md       # documentation


🚀 What I Learned

How real-world memory allocators manage heap memory
Importance of maintaining data structure invariants
Debugging memory corruption and segmentation faults
Designing systems that remain correct over time


✅ Future Improvements

Best-fit allocation strategy
Memory alignment (8/16-byte boundaries)
Thread-safe allocator
Performance benchmarking


📚 References

OSTEP (Operating Systems: Three Easy Pieces), Chapter 17