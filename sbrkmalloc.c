#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <malloc.h> 
#include <stdio.h> 


#define BLOCK_SIZE sizeof(block_t)



#include <unistd.h> //for sbrk
#include <string.h> //for memset...


#include <debug.h> // definition of debug_printf

// https://pages.cs.wisc.edu/~remzi/OSTEP/vm-freespace.pdf
// read here: remember - coalescing and splitting

typedef struct block {
  size_t size;        // How many bytes beyond this metadata have been
                      // allocated for this block
  struct block *next; // Where is the next block in the free list
  int free;           // Is this block freed?
} block_t;


/**
 * If there exists not a block, sbrk a new one!
 */
block_t *allocate_new(block_t* last, size_t s) {
  block_t * block;
  block = sbrk(s + BLOCK_SIZE);
  if(block == (void*) -1) {
    return NULL; //failed!
  }
  if(last != NULL) {
    last->next = block;
  }
  block->size = s;
  block->next = NULL;
  block->free = 0;
  return block;
}

block_t * head = NULL;

void *mymalloc(size_t s) {
  //coalesce();
  block_t * block = head;
  block_t * previous = NULL;
  void *p = (void *) NULL;
  while(1) {
    if(block == NULL) { // ran out of list elements... or just have none
      p = allocate_new(block, s);
      if(head == NULL) {
        head = p;
      }
      if(previous != NULL){
        previous->next = p;
      }
      break;
    }
    if(block->free && block->size >= s) { // a free block is found with enough size
      int old_size = block->size;
      block->size = s;
      block->free = 0;
      if(old_size - s == 0) {
        //keep old block->next
        goto aftercreation; //silly...
      }
      //if there's leftover space, create a new block to hold it
      block_t *newblock;
      newblock->size = old_size - s - BLOCK_SIZE;
      newblock->free = 1;
      newblock->next = block->next;
      block->next = newblock;
      aftercreation:
      p = block; //i don't know if this is ACTUALLY doing anything
      break;
    }
    previous = block;
    block = block->next; // go to the next element, this one is no good
  }

  

  if (!p) {
    return (void *) NULL;
  }
  debug_printf("malloc %zu bytes\n", s);

  return p + BLOCK_SIZE;
}




/**
 * Merges two adjacent blocks if they are both freed!
 * I wrote this before realizing I didn't need it. Sorry :-(
 */
void coalesce(block_t * this) { //this might cause memory errors... don't call this yet!
  if(this->next == NULL) { //end of the list
    return;
  }
  if(this->free && this->next->free) { //these are both freed...
    this->size = this->size + sizeof(*(this->next)) + this->next->size; //add the size of the next block to this one!
    this->next = this->next->next; //set this next element to the previous next's next
    coalesce(this->next); //keep on coalescing
    return;
  }
  coalesce(this->next); //keep on coalescing, this isn't free
  
}

void *mycalloc(size_t nmemb, size_t s) {

  size_t size = nmemb * s;
  void * p = mymalloc(size);

  if (!p) {
    // We are out of memory
    // if we get NULL back from malloc
    return NULL;
  }

  memset(p, 0, size);
  debug_printf("calloc %zu bytes\n", s);

  return p;
}

void myfree(void *ptr) {
  if(!ptr) {
    return;
  }
  
  block_t * b = ptr - BLOCK_SIZE; //this should return the block for that memory?
  b->free = 1;
  debug_printf("Freed some memory\n");
  //coalesce();
}
