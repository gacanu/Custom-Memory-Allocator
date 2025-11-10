#include <malloc.h> 
#include <stdio.h> 
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include <debug.h> // definition of debug_printf

#include <assert.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE) // size of the page in bytes
#define PAGE_SIZE_BLOCK sysconf(_SC_PAGESIZE) - sizeof(block_t)

typedef struct block {
  size_t size;         // the size of user-accessible bytes. The actual size is sizeof(block_t) + size
  struct block *next;  // the next free block after this one, if it's freed.
  int hasbuddy;        // If this block has been split from, it has a 'buddy' block.
} block_t;

static block_t *blocks = NULL; // the linked-list of freed blocks.

void insert_block(block_t* block); 

// Prints the list of free blocks.
void print_blocks() {
  block_t *curr = blocks;
  while (curr) {
    debug_printf(
        "%p\n"
        "      size = %lu\n"
        "      next = %p\n",
        curr,
        curr->size,
        curr->next);
    curr = curr->next;
  }
}


// Initializes a small block (a block below PAGE_SIZE - sizeof(block_t)). 
// Small blocks are placed on the free list to be reused when freed.
block_t *init_block_small(size_t s) {
  debug_printf("malloc: block of size %zu not found - calling mmap\n", sizeof(block_t) + s);
  block_t* b = mmap(NULL, PAGE_SIZE, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  if (b == (void *) -1) {
    perror("mmap failed!");
    return NULL;
  }

  b->size = PAGE_SIZE_BLOCK; //I wonder...
  b->next = NULL;

  return b;
}

// Initializes a large block (larger than PAGE_SIZE - sizeof(block_t)).
// Large blocks are unmapped when freed, rather than being put as useable free blocks on the free list.
block_t *init_block_large(size_t s) {
  
  int pages = (s + sizeof(block_t)) / PAGE_SIZE;
  if((s + sizeof(block_t)) % PAGE_SIZE != 0) {  // there's probably a smarter way to do this, isn't there
    pages += 1;
  }
  block_t* b =
      mmap(NULL, PAGE_SIZE * pages, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

  if (b == (void *) -1) {
    perror("Making big block failed!");
    return NULL;
  }
  debug_printf("malloc: large block - mmap region of size %zu\n", PAGE_SIZE * pages);

  b->size = (PAGE_SIZE * pages) - sizeof(block_t);
  b->next = NULL;

  return b;
}


// Gets the next free block and removes it from the free list.
block_t *get_free(block_t *first, size_t s) {

  block_t *prev = NULL;
  while (first != NULL && first->size < s) {
    prev = first;
    first = first->next;
  }

  if (first) {
    if (prev) {
      prev->next = first->next;
    }
    else {
      blocks = first->next;
    }
    first->next = NULL;
  }

  return first;
}

// Splits one block into two; and the second block is placed in the free list to be reused later.
void split(block_t * newblock, size_t s) {
  newblock->hasbuddy = 1;
  block_t* splitpiece = ((void*)newblock + sizeof(block_t) + s);  // start this in the right place memory-wise TODO: FIX!
  splitpiece->hasbuddy = 0;
  splitpiece->size = newblock->size - s - sizeof(block_t);  // set the size correctly - user memory should be at
                                                            // least the size of a header
  splitpiece->next = newblock->next; //probably null
  assert(splitpiece->size >= sizeof(block_t));
  insert_block(splitpiece);  // place this new split off piece into the free list
  newblock->size = newblock->size - (splitpiece->size + sizeof(block_t));  // shrink by however much space splitpiece takes up
  debug_printf("malloc: splitting - blocks of size %zu and %zu created\n",
  sizeof(block_t) + newblock->size, sizeof(block_t) + splitpiece->size);
}


// Allocates a block of memory to be used, and returns the pointer to that block.
void *mymalloc(size_t s) {
  block_t * newblock;
  if(s <= PAGE_SIZE_BLOCK) { //SMALL BLOCK
    newblock = get_free(blocks, s);
    if(!newblock) {
      newblock = init_block_small(PAGE_SIZE_BLOCK);
      newblock->hasbuddy = 0;
    }
    else {
      debug_printf("malloc: block of size %zu found\n", sizeof(block_t) + s);
      if(newblock->size - s > (2 * sizeof(block_t))) {  // SPLIT CASE
        split(newblock, s);
      }
    }
  }
  else { //LARGE BLOCK
    newblock = init_block_large(s);
  }
  if (!newblock) {
    return NULL;
  }

  debug_printf("malloc %zu bytes\n", sizeof(block_t) + newblock->size);

  return newblock + 1; // the user accessible memory, beyond the header.
}

// Returns user-accesible memory that has been zero-ed out first.
void *mycalloc(size_t nmemb, size_t s) {

  void *p = (void *) mymalloc(nmemb * s);

  if (!p) {
    return NULL;
  }

  debug_printf("calloc %zu bytes\n", sizeof(block_t) + s);

  return p;
}

// Inserts a block into the (sorted) free list, maintaining order.
// Elements are sorted by their memory addresses.
void insert_block(block_t *block) {
  if(blocks == NULL) {
    blocks = block;
    return;
  }
  block_t * current = blocks;
  block_t * previous = NULL;
  while(current != NULL && ((long long) current) < ((long long)block)) { //I'm not sure why doing this makes it work...
    previous = current;
    current = current->next;
  }

  if(previous != NULL) { 
    previous->next = block;
  }
  else{
    blocks = block; //This is the first element in the list.
  }
  block->next = current;
}



// Combines two adjacent areas of freed memory.
void combine(block_t* A, block_t* B) {
  debug_printf("free: coalesce blocks of size %zu and %zu to new block of size %zu\n",
               sizeof(block_t) + A->size, sizeof(block_t) + B->size,
               B->size + (2 * sizeof(block_t)) + A->size);
  A->next = B->next;
  A->size += B->size + sizeof(block_t);
  B->size = 0;
  A->hasbuddy = B->hasbuddy;
  
}

// If the next element after the given block is a 'buddy node' and is freed, then combine that free block of memory with this one.
// Only call this on freed blocks!
void coalesce(block_t * first) {
  if(!first) {
    return;
  }
  if(first->next != NULL && first->hasbuddy) {
    combine(first, first->next);
    coalesce(first);
    return;
  }
  coalesce(first->next);
}

// Iterates through the free list, and unmaps any extra page-sized blocks. There can exist a maximum of two.
void purge(block_t * prev, block_t * block, size_t count) {
  if(!block) {
    return;
  }
  if(block->size < PAGE_SIZE_BLOCK) { //keep looking!
    purge(block, block->next, count);
    return;
  }
  if(count < 2) {
    purge(block, block->next, count + 1);
    return;
  }
  block_t * next = block->next;
  if(!prev) {
    blocks = block->next;
  }
  else {
    prev->next = block->next;
  }
  munmap(block, PAGE_SIZE);
  debug_printf("Purged block!\n");
  purge(prev, next, count + 1);

  
}

// Free a given region of memory, allowing it to be reused.
void myfree(void *ptr) {

  //print_blocks();
  block_t *block = ((block_t *) ptr) - 1;

  size_t freed_bytes = block->size;
  debug_printf("Freed %lu bytes of memory\n", sizeof(block_t) + freed_bytes);

  if(block->size > PAGE_SIZE_BLOCK) {
    munmap(block, block->size + sizeof(block_t));
  }
  else{
    insert_block(block);
    coalesce(blocks);
  }

  purge(NULL, blocks, 0);




}
