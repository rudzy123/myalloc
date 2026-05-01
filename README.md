# Custom Memory Allocator (C)# Custom Memory Allocator Musika
**Project Type:** Systems Programming / Memory Management  
**Language:** C  

---

## 📌 Overview

This project implements a **custom dynamic memory allocator** in C, functionally similar to `malloc()` and `free()`.

It is designed as a systems-level exploration of:

- manual heap management  
- memory layout design  
- free list data structures  
- pointer arithmetic  
- fragmentation and coalescing  

The repository includes **multiple implementations**, demonstrating both:

- ✅ a correct, fully validated allocator  
- 🧪 experimental designs that explore alternative approaches  

---

## 🧠 Project Goals

- Understand how memory allocators work internally  
- Implement allocation and deallocation from scratch  
- Maintain correctness across complex state transitions  
- Validate behavior using a strict test suite  
- Explore trade-offs between allocator designs  

---

## 🏗️ Repository Structure

.
├── myalloc-master/              ✅ Final working implementation
├── myalloc-master_Failed/      ⚠️ Earlier incorrect attempt
├── myalloc-master_LLM_Help/    🧪 LLM-assisted experimental version
├── LICENSE
├── README.md

---

## ✅ 1. Final Implementation (`myalloc-master/`)

This is the **correct allocator** that passes all tests.

### Key Features

- First-fit allocation strategy
- Embedded free list
- Block splitting
- Explicit coalescing via sorting + merging
- Memory integrity verification (magic number)
- Full compliance with test suite

---

### 📦 Memory Model

#### Allocated Block


[ header_t ][ user data ]

#### Free Block


[ node_t ][ free region ]

---

### 🔧 Design Details

- Free list is **unsorted**
- Insertion into free list is **O(1)**
- Coalescing uses:
  - sorting (via `qsort`)
  - merging adjacent blocks

This design was chosen to match the **assignment's strict validation model**.

---

### ✅ Test Coverage

Validated using `alligator.c`, which checks:

| Test | Description |
|------|------------|
| Test 0 | Basic allocation |
| Test 1 | Block splitting |
| Test 2 | Allocation failure |
| Test 3 | Freeing memory |
| Test 4 | Double free detection |
| Test 5 | Coalescing |
| Test 6 | Edge-case coalescing |
| Test 7 | Memory reuse |

✔ All tests pass in this implementation

---

## ⚠️ 2. Failed Attempt (`myalloc-master_Failed/`)

This version represents an earlier iteration.

### Issues Encountered

- Incorrect free list state
- Broken coalescing logic
- Memory corruption
- Invalid pointer traversal
- Test failures

---

### Value of This Version

This folder is intentionally preserved to demonstrate:

- iterative debugging  
- understanding of allocator invariants  
- evolution from incorrect → correct design  

---

## 🧪 3. LLM-Assisted Implementation (`myalloc-master_LLM_Help/`)

This version was developed with AI assistance to explore alternative designs.

---

### Design Characteristics

- Attempts at sorted free list
- Linear coalescing (instead of sorting)
- More "textbook" allocator logic

---

### Why It Failed

The implementation:

- Was logically valid as a general allocator  
- Did **not match assignment-specific invariants**  

Key mismatches:

- different size accounting
- different freelist structure
- incompatible assumptions in test suite

---

### Key Insight

> ✅ A system can be logically correct but still fail if it does not match the **expected model**

---

## 🧠 Key Learnings

### 1. Internal Consistency > Generic Correctness

Memory allocators must maintain **strict invariants**:

- pointer layout
- block size accounting
- correct transitions between states

---

### 2. Test Suites Encode Design Assumptions

The provided tests assume:

- specific freelist structure
- precise size calculations
- exact memory layout

---

### 3. Multiple Valid Designs Exist

| Strategy | Characteristics |
|----------|----------------|
| Unsorted list + sort during coalesce | ✅ Used here |
| Always-sorted free list | ❌ Failed under this test suite |
| Best-fit / next-fit | Not implemented |

---

### 4. Subtle Errors Have Large Impact

Common failure sources:

- off-by-one pointer arithmetic
- incorrect size subtraction
- mixing memory models
- inconsistent structure reuse

---

## 🛠️ Build & Run

Navigate to the working implementation:

```bash
cd myalloc-master
make
make run


📊 Example Output
Test 0: Super Simple Allocation is Good :)
Test 1: Simple Allocation is Good :)
...
Test 7.3: Passed. I could not find a Memory Leak :)


🚀 Future Work

Best-fit allocation strategy
Memory alignment (8/16 byte boundaries)
Performance benchmarking
Visualization of heap layout
Thread-safe allocator


📚 References

Operating Systems: Three Easy Pieces (OSTEP)
Heap management and memory allocation concepts


✅ Summary
This project demonstrates:
✅ Low-level systems programming
✅ Memory management fundamentals
✅ Debugging complex pointer-based systems
✅ Reasoning about data structure invariants
✅ Comparing multiple allocator designs

👤 Author
Rudolf Musika

🧠 Final Note
This repository includes both successful and failed implementations to demonstrate not just the final result, but the engineering process required to achieve it.
---