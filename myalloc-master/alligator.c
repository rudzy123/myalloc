#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "myalloc.h"


void *alloc_check_2(size_t size, size_t realsize) {
  /* Like alloc_check except you can give a realsize which
   * will put exactly realsize number of bytes into memory
   * starting at ptr.
   */
  void *ptr; /* temp ptr for allocation */

  if ((ptr = myalloc(size)) == NULL) {
    printf("!!! Allocation of size %u failed!\n", (unsigned) size);
    return NULL;
  } else {
    printf("Allocation of size %u succeeded @ %p!\n", (unsigned) size, ptr);
    // Fill ptr with data
    memset((char *) ptr, rand(), realsize);
    return ptr;
  }
}

void *alloc_check(size_t size) {
  /* this is helper function that verifies whether the
   * allocation succeeded or failed and prints out some useful
   * information
   */
  return alloc_check_2(size, size);
}

// If good returns 1, else 0.
int is_header_good(void *ptr, int size) {
  if (ptr == NULL) {
    return 0;
  }
  header_t *t = (header_t *) (ptr - sizeof(header_t));
  if (t->magic != HEAPMAGIC) {
    printf("Header Magic number not correct.\n");
    // Magic is not right.
    return 0;
  }
  if (t->size < size) {
    printf("Header says size is too small.\n");
    // Size is too small.
    return 0;
  }
  return 1;
}

// If good returns 1, else 0.
int is_data_good(char *ptr, int size) {
  if (size < 1) return 1;
  int i = size;
  while (--i > 0 && ptr[i] == ptr[0]);
  return i == 0;
}

// If good returns 1, else 0
int is_free_list_good(node_t *head, int *correct, int size) {
  int cur_size;
  int left[size];
  memcpy(&left, correct, size * sizeof(int));
  node_t *cur = head;
  if (head == NULL) {
    printf("Freelist check: Bad head\n");
    return 0;
  }
  while (cur != NULL) {
    cur_size = cur->size;
    int found = 0;
    for (int i = 0; i < size; i++) {
      if (left[i] == cur_size) {
        left[i] = -1;
        found = 1;
        break;
      }
    }
    if (found == 0) {
      printf("Freelist check: Freelist node contains incorrect size.\n");
      printf("Freelist check: size is %d\n", cur_size);
      return 0;
    }
    cur = cur->next;
  }
  for (int i = 0; i < size; i++) {
    if (left[i] != -1) {
      printf("Freelist check: Freelist did not contain enough nodes.\n");
      return 0;
    }
  }
  return 1;
}

// If good returns 1, else 0.
int complete_state_check(node_t *head, int *freelist, int freelist_size, void **ptr, int ptr_size, int *sizes) {
  if (!is_free_list_good(head, freelist, freelist_size)) {
    printf("Free list is not correct!\nFreelist trace:");
    print_freelist_from(__head);
    printf("However it should have the following sizes\n[");
    if (freelist_size > 0)
      printf("%d", freelist[0]);
    for (int i = 1; i < freelist_size; i++) {
      printf(", %d", freelist[i]);
    }
    printf("]\n");
    return 0;
  }
  for (int i = 0; i < ptr_size; i++) {
    if (sizes[i] == -2) {
      // Good
    } else if (ptr[i] == NULL && sizes[i] == -1) {
      // Good
    } else if (ptr[i] == NULL) {
      printf("You program failed to alloc ptr[%d]\n", i);
    } else if (!is_header_good(ptr[i], sizes[i])) {
      printf("Found Incorrect Header @ ptr[%d]\n", i);
      return 0;
    } else if (!is_data_good(ptr[i], sizes[i])) {
      printf("Data Corrupted in ptr[%d]\n", i);
      return 0;
    }
  }
  return 1;
}

int main(int argc, char *argv[]) {
  // Set up random number generator.
  srand(time(NULL));

  void *ptr[100];
  int freelist[100];
  int sizes[100];
  int headersize = sizeof(header_t);
  int nodesize = sizeof(node_t);
  int list1[100] = {[0] = 4096 - 16 - 1024 - 1024 - 512 - (16 * 3)};

  /* let's print some sizes of types so we know what they are */
  printf("sizes:\n");
  printf("void *:\t\t%lu\n", sizeof(void *));
  printf("long unsigned:\t%lu\n", sizeof(long unsigned));
  printf("node_t:\t\t%lu\n", sizeof(node_t));
  printf("header_t:\t%lu\n", sizeof(header_t));

  // Test 0: Super simple alloc
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(512);
  for (int i=0; i < 2; i++) {
    if (ptr[i] == NULL || !is_data_good(ptr[i], 512)) {
      printf("Test 0: Failed, Super Simple Allocation is not working :(\n");
      return 1;
    }
  }
  printf("Test 0: Super Simple Allocation is Good :)\n");

  // Reset heap
  destroy_heap();
  // NEW CODE
  // TEST 1: Test simple alloc
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(512);
  sizes[0] = 1024;
  sizes[1] = 1024;
  sizes[2] = 512;

  //int list1[1]
  freelist[0] = HEAPSIZE - 1024 - 1024 - 512 - (nodesize * 1) - (headersize * 3);
  if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
    printf("Test 1: Simple Allocation is Good :)\n");
  } else {
    printf("Test 1: Failed, Allocation is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 2: Test advanced alloc
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(3000);
  ptr[3] = alloc_check(512);
  sizes[0] = 1024;
  sizes[1] = 1024;
  sizes[2] = -1;   // NULL
  sizes[3] = 512;

  freelist[0] = HEAPSIZE - 1024 - 1024 - 512 - (nodesize * 1) - (headersize * 3);
  if (complete_state_check(__head, freelist, 1, ptr, 4, sizes)) {
    printf("Test 2: Advanced Allocation is Good :)\n");
  } else {
    printf("Test 2: Failed, Allocation is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 3: Test alloc and free
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(1024);
  myfree(ptr[1]);
  sizes[0] = 1024;
  sizes[1] = -2;   // Don't check, not allocated.
  sizes[2] = 1024;

  freelist[0] = HEAPSIZE - 1024 - 1024 - 1024 - (nodesize * 1) - (headersize * 3);
  freelist[1] = 1024;
  if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
    printf("Test 3: Simple Free is Good :)\n");
  } else {
    printf("Test 3: Failed, Free is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 4: Test double free
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(1024);
  myfree(ptr[1]);
  myfree(ptr[1]);
  sizes[0] = 1024;
  sizes[1] = -2;   // Don't check, not allocated.
  sizes[2] = 1024;

  freelist[0] = HEAPSIZE - 1024 - 1024 - 1024 - (nodesize * 1) - (headersize * 3);
  freelist[1] = 1024;
  if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
    printf("Test 4: If you saw the message '...The heap is corrupt!' above then,\n");
    printf("Test 4: Double Free is Good :)\n");
  } else {
    printf("Test 4: Failed, Double Free is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 5: Test alloc, free, coalesce, alloc
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(512);
  ptr[2] = alloc_check(512);
  ptr[3] = alloc_check(2048);
  myfree(ptr[1]);
  myfree(ptr[2]);
  sizes[0] = 512;
  sizes[1] = -2;   // Don't check, not allocated.
  sizes[2] = -2;   // Don't check, not allocated.
  sizes[3] = 2048;

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 512;
  freelist[2] = 512;
  if (complete_state_check(__head, freelist, 3, ptr, 4, sizes)) {
    printf("Test 5.1: Coalesce is Good so far :)\n");
  } else {
    printf("Test 5.1: Failed, Coalesce is not working :(\n");
    return 1;
  }

  coalesce_freelist();

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 1024 + headersize;
  if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
    printf("Test 5.2: Coalesce is Good so far :)\n");
  } else {
    printf("Test 5.2: Failed, Coalesce is not working :(\n");
    return 1;
  }

  ptr[4] = alloc_check(1000);

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 24;
  if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
    printf("Test 5.3: Coalesce is Good :)\n");
  } else {
    printf("Test 5.3: Failed, Coalesce is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 6: Test coalesce and have node of size 0.
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(512);
  ptr[2] = alloc_check(512);
  ptr[3] = alloc_check(2048);
  myfree(ptr[1]);
  myfree(ptr[2]);
  sizes[0] = 512;
  sizes[1] = -2;   // Don't check, not allocated.
  sizes[2] = -2;   // Don't check, not allocated.
  sizes[3] = 2048;

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 512;
  freelist[2] = 512;
  if (complete_state_check(__head, freelist, 3, ptr, 4, sizes)) {
    printf("Test 6.1: Advanced Coalesce is Good so far :)\n");
  } else {
    printf("Test 6.1: Failed, Coalesce is not working :(\n");
    return 1;
  }

  coalesce_freelist();

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 1024 + headersize;
  if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
    printf("Test 6.2: Advanced Coalesce is Good so far :)\n");
  } else {
    printf("Test 6.2: Failed, Coalesce is not working :(\n");
    return 1;
  }

  ptr[4] = alloc_check(1024);

  freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
  freelist[1] = 0;
  if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
    printf("Test 6.3: Advanced Coalesce is Good :)\n");
  } else {
    printf("Test 6.3: Failed, Coalesce is not working :(\n");
    return 1;
  }

  // Reset heap
  destroy_heap();
  // TEST 7: Test for memory leak.
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(16);
  ptr[2] = alloc_check(512);
  sizes[0] = 512;
  sizes[1] = 16;
  sizes[2] = 512;

  freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
  if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
    printf("Test 7.1: Memory Leak is Good so far :)\n");
  } else {
    printf("Test 7.1: You have a memory leak! This is bad :(\n");
    return 1;
  }
  myfree(ptr[1]);
  sizes[1] = -2;
  freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
  freelist[1] = 16;
  if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
    printf("Test 7.2: Memory Leak is Good so far :)\n");
  } else {
    printf("Test 7.2: You have a memory leak! This is bad :(\n");
    return 1;
  }
  ptr[1] = alloc_check_2(14, 16);
  sizes[1] = 16;
  freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
  if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
    printf("Test 7.3: Passed. I could not find a Memory Leak :)\n");
  } else {
    printf("Test 7.3: You have a memory leak! Or you are not using memory optimally. This is bad :(\n");
    return 1;
  }

  return 0;
}
