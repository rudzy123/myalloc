#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "myalloc.h"

void *alloc_check_2(size_t size, size_t realsize) {
  void *ptr;

  if ((ptr = myalloc(size)) == NULL) {
    printf("!!! Allocation of size %u failed!\n", (unsigned) size);
    return NULL;
  } else {
    printf("Allocation of size %u succeeded @ %p!\n", (unsigned) size, ptr);
    memset((char *) ptr, rand(), realsize);
    return ptr;
  }
}

void *alloc_check(size_t size) {
  return alloc_check_2(size, size);
}

int is_header_good(void *ptr, int size) {
  if (ptr == NULL) return 0;

  header_t *t = (header_t *)((char *)ptr - sizeof(header_t));

  if (t->magic != HEAPMAGIC) {
    printf("Header Magic number not correct.\n");
    return 0;
  }

  if (t->size < (unsigned long)size) {
    printf("Header says size is too small.\n");
    return 0;
  }

  return 1;
}

int is_data_good(char *ptr, int size) {
  if (size < 1) return 1;
  int i = size;
  while (--i > 0 && ptr[i] == ptr[0]);
  return i == 0;
}

int is_free_list_good(node_t *head, int *correct, int size) {
  int left[100];
  memcpy(left, correct, size * sizeof(int));

  node_t *cur = head;

  if (head == NULL) {
    printf("Freelist check: Bad head\n");
    return 0;
  }

  while (cur != NULL) {
    int cur_size = cur->size;
    int found = 0;

    for (int i = 0; i < size; i++) {
      if (left[i] == cur_size) {
        left[i] = -1;
        found = 1;
        break;
      }
    }

    if (!found) {
      printf("Freelist has incorrect size: %d\n", cur_size);
      return 0;
    }

    cur = cur->next;
  }

  for (int i = 0; i < size; i++) {
    if (left[i] != -1) {
      printf("Freelist missing expected node.\n");
      return 0;
    }
  }

  return 1;
}

int complete_state_check(node_t *head, int *freelist, int freelist_size,
                         void **ptr, int ptr_size, int *sizes) {

  if (!is_free_list_good(head, freelist, freelist_size)) {
    printf("Free list is not correct!\n");
    print_freelist_from(__head);
    return 0;
  }

  for (int i = 0; i < ptr_size; i++) {
    if (sizes[i] == -2) {
      continue;
    } else if (ptr[i] == NULL && sizes[i] == -1) {
      continue;
    } else if (ptr[i] == NULL) {
      printf("Allocation failed for ptr[%d]\n", i);
      return 0;
    } else if (!is_header_good(ptr[i], sizes[i])) {
      printf("Header error at ptr[%d]\n", i);
      return 0;
    } else if (!is_data_good(ptr[i], sizes[i])) {
      printf("Data corrupted at ptr[%d]\n", i);
      return 0;
    }
  }

  return 1;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  srand(time(NULL));

  void *ptr[100];
  int freelist[100];
  int sizes[100];

  int headersize = sizeof(header_t);
  int nodesize = sizeof(node_t);

  printf("sizes:\n");
  printf("node_t:\t\t%lu\n", sizeof(node_t));
  printf("header_t:\t%lu\n", sizeof(header_t));

  // TEST 0
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(512);

  if (!is_data_good(ptr[0], 512) || !is_data_good(ptr[1], 512)) {
    printf("Test 0 FAILED\n");
    return 1;
  }

  printf("Test 0 PASSED\n");

  destroy_heap();

  // TEST 1
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(512);

  sizes[0] = 1024;
  sizes[1] = 1024;
  sizes[2] = 512;

  freelist[0] = HEAPSIZE - 1024 - 1024 - 512 - (nodesize) - (headersize * 3);

  if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
    printf("Test 1 PASSED\n");
  } else return 1;

  destroy_heap();

  // TEST 3 (alloc + free)
  ptr[0] = alloc_check(1024);
  ptr[1] = alloc_check(1024);
  ptr[2] = alloc_check(1024);

  myfree(ptr[1]);

  sizes[0] = 1024;
  sizes[1] = -2;
  sizes[2] = 1024;

  freelist[0] = HEAPSIZE - 3 * 1024 - nodesize - 3 * headersize;
  freelist[1] = 1024;

  if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
    printf("Test 3 PASSED\n");
  } else return 1;

  destroy_heap();

  // TEST 5 (coalesce)
  ptr[0] = alloc_check(512);
  ptr[1] = alloc_check(512);
  ptr[2] = alloc_check(512);
  ptr[3] = alloc_check(2048);

  myfree(ptr[1]);
  myfree(ptr[2]);

  coalesce_freelist();

  sizes[0] = 512;
  sizes[1] = -2;
  sizes[2] = -2;
  sizes[3] = 2048;

  freelist[0] = HEAPSIZE - 3*512 - 2048 - nodesize - 4*headersize;
  freelist[1] = 1024 + headersize;

  if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
    printf("Test 5 PASSED\n");
  } else return 1;

  printf("\n🎉 ALL TESTS PASSED 🎉\n");

  return 0;
}