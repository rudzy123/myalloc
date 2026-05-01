# Memory Allocator – LLM Comparison Experiment

## Overview

This folder contains an alternative implementation of a custom memory allocator written with assistance from an LLM (ChatGPT).

The purpose of this version was to explore different allocator design strategies and compare them against the original working implementation.

---

## Goal

The experiment aimed to:

- Implement a first-fit memory allocator
- Compare different freelist strategies
- Evaluate correctness against an existing test suite (`alligator.c`)

---

## Key Differences from Original Implementation

| Aspect | Original (Working) | LLM Version |
|--------|------------------|------------|
| Free list ordering | Unsorted | Attempted sorted / mixed |
| Coalescing | Sort + merge | Linear / hybrid attempts |
| Split logic | Assignment-specific | More "textbook" allocator |
| Test compatibility | ✅ Passes all tests | ❌ Fails strict checks |

---

## Result

While the LLM implementation was logically valid as a general-purpose allocator, it did not pass the provided test suite due to:

- Differences in memory accounting
- Different freelist structure expectations
- Strict validation requirements in tests

---

## Key Takeaways

- Correctness depends on **internal consistency**, not just logic
- Memory layout assumptions must match test expectations exactly
- Different allocator designs can be valid but incompatible with specific test suites

---

## Conclusion

The original implementation remains the correct solution for this assignment.

This version serves as a **learning exercise** demonstrating:

- alternative allocator designs
- debugging memory systems
- understanding allocator invariants

---

## Build & Run

```bash
make
make run

Files
myalloc.c      # LLM-based allocator attempt
alligator.c    # test suite
Makefile       # build system
myalloc.h      # structures and definitions

Author
Rudolf Musika
(LLM-assisted exploration)

---

