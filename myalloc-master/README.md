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

Compile:

```bash
make

Run tests:
Shellmake runShow more lines
Clean build:
Shellmake cleanShow more lines

📁 Project Structure
myalloc/
├── myalloc.c       # allocator implementation
├── myalloc.h       # data structures + prototypes
├── alligator.c     # test suite
├── Makefile        # build system
└── README.md


🔍 Design Notes

Uses a first-fit allocation strategy
Free blocks are stored in a linked list
Coalescing is implemented by sorting the free list and merging adjacent regions
Heap integrity is verified using a magic number


🚀 What I Learned

How real-world memory allocators manage heap memory
The importance of maintaining consistent data structures
How small pointer arithmetic mistakes can cause major bugs
Techniques for debugging segmentation faults and memory corruption


✅ Future Improvements

Implement best-fit allocation strategy
Add alignment (8/16-byte boundaries like real malloc)
Improve performance by avoiding sorting during coalescing
Add visualization of heap state


📚 Reference

Operating Systems: Three Easy Pieces (OSTEP), Chapter 17