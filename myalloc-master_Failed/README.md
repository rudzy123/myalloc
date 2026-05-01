# myalloc

Rudy Musika and Alfonso Sainz



This lab must be completed in groups of two . Both members will receive the same grade . I
recommend that you work on the programming together (two heads are better than one, and all
that). However, you may divide the labor if you feel you must.
Part 1: 
myalloc:
The function myalloc(size) should returns a pointer to a buffer of size bytes, and stores in
memory (just before that buffer) a header that includes the size of the buffer in question and a
magic number.
free:
The free memory regions in the heap are stored using an “embedded” linked list as described in
OSTEP Chapter 17 . The linked-list structure storing the size and pointing to the next free region
is also 16 bytes -- in essence, while the size field of the header remains the same, the magic
number is instead a pointer to the next free region (see code , below). The global variable
__head must always point to the first region in the free list and the last element in the free list
must point to NULL.

Part 2: coalesce_freelist
We implement a function, coalesce_freelist() that takes as its input the global head
pointer. coalesce_freelist() then scans the freelist, combining any neighboring free regions into a
larger free region by updating the size value and next pointer in the appropriate headers. This is
complicated by the fact that the order of addresses on the free list is determined by the order of
calls to myfree(), not sorted by virtual address.
