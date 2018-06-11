#define _GNU_SOURCE 1

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

// The minimum size returned by malloc
#define MIN_MALLOC_SIZE 16

// Round a value x up to the next multiple of y
#define ROUND_UP(x,y) ((x) % (y) == 0 ? (x) : (x) + ((y) - (x) % (y)))
// Round a value x down to the next multiple of y
#define ROUND_DOWN(x,y) ((x) - ((x) % (y)))

// The size of a single page of memory, in bytes
#define PAGE_SIZE 0x1000


// USE ONLY IN CASE OF EMERGENCY
bool in_malloc = false;           // Set whenever we are inside malloc.
bool use_emergency_block = false; // If set, use the emergency space for allocations
char emergency_block[1024];       // Emergency space for allocating to print errors



typedef struct node {
  struct node* next;
} Node;

typedef struct header {
  int magicNum;
  size_t size;
  Node* freelist;
  struct header* next;
} Header;

// declare ptrs to headers of the 8 different block sizes.
Header* blockPtrs[8];
// ptr to next available memory chunk
void* p;
// offset is the size of the requested memory chunk. 
size_t offset;




/**
 * Allocate space on the heap.
 * \param size  The minimium number of bytes that must be allocated
 * \returns     A pointer to the beginning of the allocated space.
 *              This function may return NULL when an error occurs.
 */
void* xxmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  int log2;
  
  // Before we try to allocate anything, check if we are trying to print an error or if
  // malloc called itself. This doesn't always work, but sometimes it helps.
  if(use_emergency_block) {
    return emergency_block;
  } else if(in_malloc) {
    use_emergency_block = true;
    puts("ERROR! Nested call to malloc. Aborting.\n");
    for(;;){}
  }
  
  // If we call malloc again while this is true, bad things will happen.
  in_malloc = true;

  //if size is too small, make it the minimum malloc size
  if (size < MIN_MALLOC_SIZE) {
    size = MIN_MALLOC_SIZE;
  }

  // if requested object is larger than 2048 bytes, just give it to them.
  if(size > 2048) {
    // Round the size up to the next multiple of the page size
    size = ROUND_UP(size, PAGE_SIZE);
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    in_malloc = false;
    return p;
  }

  
  // if (# leading zeros + # trailing zeros + a single 1) < (actual # of bits)
  // i.e., if size is NOT a power of 2
  if((__builtin_clzl(size) + __builtin_ctz(size) + 1) < (8 * sizeof(size_t))) {
    // then log base 2 = actual # of bits - # leading zeros
    log2 = 8 * sizeof(size_t) - __builtin_clzl(size);
  }
  else {
    // else log base 2 = # trailing zeros
    log2 = __builtin_ctz(size);
  }

  // log 2 of power of 2 object size - log 2 of offset (min object size)
  int index = log2 - __builtin_ctz(MIN_MALLOC_SIZE);

  //round size up to a power of two
  size = 1 << log2;
  
  // if no block for requested size exists yet, make it.
  // OR if current block of requested size is full, make new one.
  if ((blockPtrs[index] == NULL) || (blockPtrs[index]->freelist == NULL)) {
    // Request memory from the operating system in page-sized chunks
    p = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);


    // declare ptr to most recent block of relevant size
    Header* newBlock;
    
    newBlock = (Header*) p;
    newBlock->next = blockPtrs[index];
    blockPtrs[index] = newBlock;
    blockPtrs[index]->magicNum = 0xF00DFACE;
    blockPtrs[index]->size = size;
    blockPtrs[index]->freelist = NULL;     

    // place header ptr (for block info) and base ptr (start of memory block).
    Header* firstPtr = (Header*) p;
    intptr_t base = (intptr_t) p;

    /* make linked list of ptrs to memory chunks until we've gone through 
       the whole page, starting at base and adding offset each time. */
    for(offset = ROUND_UP(sizeof(Header), size); offset < PAGE_SIZE; offset += size) {
      Node* newNode = (Node*) (base + offset);
      newNode->next = firstPtr->freelist;
      // initialize freelist to new ptr.
      firstPtr->freelist = newNode;
    }
  }

  p = (void *) blockPtrs[index]->freelist;
  blockPtrs[index]->freelist = blockPtrs[index]->freelist->next;

  
  // Check for errors
  if(p == MAP_FAILED) {
    use_emergency_block = true;
    perror("mmap");
    exit(2);
  }
  
  // Done with malloc, so clear this flag
  in_malloc = false;

  char buf[256];
  snprintf(buf, 256, "malloc(%lu) -> %p\n", size, p);
  fputs(buf, stderr);
  
  return p;
}


  
/**
 * Get the available size of an allocated object
 * \param ptr   A pointer somewhere inside the allocated object
 * \returns     The number of bytes available for use in this object
 */
size_t xxmalloc_usable_size(void* ptr) {
  if (ptr == NULL) {
    return 0;
  }
  // round down to find header of current block
  Header* blockHeader = (Header*) (ROUND_DOWN((intptr_t) ptr, PAGE_SIZE));
  if (blockHeader->magicNum != 0xF00DFACE) {
    return PAGE_SIZE;
  }
  return blockHeader->size;
}



/**
 * Free space occupied by a heap object.
 * \param ptr   A pointer somewhere inside the object that is being freed
 */
void xxfree(void* ptr) {
  if (ptr == NULL){
    return;
  }
  // find header of current block
  Header* blockHeader = (Header*) ROUND_DOWN((intptr_t) ptr, PAGE_SIZE);
  size_t size = xxmalloc_usable_size(ptr);

  // if it's not something we recognize (larger than 2048 bytes), forget it.
  if(ptr == NULL || blockHeader->magicNum != 0xF00DFACE) {
      return;
    }
  // start of relevant chunk
  Node* start = (Node*) ROUND_DOWN((intptr_t) ptr, size);
  
  // else insert at start of freelist
  start->next = blockHeader->freelist;
  blockHeader->freelist = start;
}



int main(int argc, char* argv[]) {
  int* ptr = (int*) xxmalloc (sizeof (int));

  *ptr = 6;
  printf("The integer = %d\n", *ptr);
  xxfree(ptr);
  
  return 0;
}
