# Status

The allocator is in a stable, tested state for the month. The default path (first-fit, insert-at-head, 8-byte alignment, explicit `coalesce_freelist()`, no lock) passes all of `alligator.c` tests 0-7, including under address and undefined-behavior sanitizers, and `make stress` is clean. Further work is intentionally left for later: true address-ordered coalescing without changing first-fit order, 16-byte alignment, and thread safety. Do not change the split/merge formulas or the default insertion order without re-reading `DESIGN.md`.
