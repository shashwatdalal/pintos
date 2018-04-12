#ifndef VM_PAGE_C
#define VM_PAGE_C

#include <devices/block.h>
#include <lib/debug.h>
#include <threads/malloc.h>
#include <lib/stdio.h>
#include <threads/thread.h>
#include <threads/vaddr.h>
#include "page.h"
#include <filesys/off_t.h>

// Hash table functions

unsigned spt_hash(const struct hash_elem *entry_, void *aux) {
  const struct spt_elem *entry = hash_entry(entry_, struct spt_elem,
                                            hash_elem);
  return hash_bytes(&entry->user_addr, sizeof entry->user_addr);
}

bool
spt_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux) {
  const struct spt_elem *a = hash_entry(a_, struct spt_elem, hash_elem);
  const struct spt_elem *b = hash_entry(b_, struct spt_elem, hash_elem);
  return a->user_addr < b->user_addr;
}

// Helper functions

struct spt_elem *spt_find_elem(struct hash *spt, void* upage) {
  struct spt_elem to_find;
  to_find.user_addr = upage;
  struct hash_elem *_hash_elem = hash_find(spt, &to_find.hash_elem);
  if(_hash_elem == NULL) {
    return NULL;
  } else {
    return hash_entry(_hash_elem, struct spt_elem, hash_elem);
  }
}

// Global Functions
void spt_set_swap_sector(struct hash *spt, void *upage, block_sector_t block_sector) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  spt_elem->state = IN_SWAP;
  spt_elem->address = block_sector;
};

block_sector_t spt_get_swap_sector(struct hash *spt, void *upage){
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  ASSERT(spt_elem->state == IN_SWAP);
  return spt_elem->address;
};

void spt_set_executable(struct hash *spt, void *upage, off_t offset, uint32_t read_bytes) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  spt_elem->state = UNLOADED_EXECUTABLE;
  spt_elem->address = offset;
  spt_elem->read_bytes = read_bytes;
}

off_t spt_get_offset(struct hash *spt, void *upage) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  ASSERT (spt_elem->state == LOADED_EXECUTABLE || spt_elem->state == UNLOADED_EXECUTABLE || spt_elem->state == MMAP_IN_FRAME);
  return spt_elem->address;
}

bool spt_get_writable(struct hash *spt, void *upage) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  return spt_elem->writable;
}

void *spt_get_filesys(struct hash *spt, void *upage) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  ASSERT (spt_elem->state == IN_FILESYS);
  return spt_elem->address;
};

void spt_set_zero_page(struct hash *spt, void *upage){
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  spt_elem->state = ZEROPAGE;
};

bool spt_is_zero_page(struct hash *spt, void *upage) {
    struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  return spt_elem->state == ZEROPAGE;
};

enum page_state spt_get_state(struct hash *spt, void *upage) {
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  return spt_elem->state;
}

uint32_t spt_get_read_bytes(struct hash *spt, void *upage){
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  ASSERT(spt_elem->state == UNLOADED_EXECUTABLE);
  return spt_elem->read_bytes;
};

void spt_set_filesys(struct hash *spt, void *upage, void *file_location){
  struct spt_elem *spt_elem = spt_find_elem(spt, upage);
  spt_elem->state = IN_FILESYS;
  spt_elem->address = file_location;
};


struct spt_elem *spt_is_page_mapped(struct hash *spt, void *upage){
  void *page_top = (((uint32_t) upage / (uint32_t) PGSIZE)) * (uint32_t) PGSIZE;
  return spt_find_elem(spt, page_top);

}


//TODO: (MARCEL) delete spt elem [every malloc should have an associated free]
struct spt_elem *spt_add_new_elem(struct hash *spt, void *upage, enum page_state state, bool writable) {
  // Initialise the supplementary page table
  // Perform check for whether the supplementary page table has been set up
  // for this given frame table
  struct spt_elem *spt_elem = malloc(sizeof(struct spt_elem));
  if (spt_elem == NULL) {
    PANIC ("Failed to malloc for spt_elem");
  }
  spt_elem->user_addr = upage;
  spt_elem->state = state;
  spt_elem->writable = writable;
  if (hash_insert(spt, &spt_elem->hash_elem) != NULL) {
    PANIC ("Failed to insert into supplemental page table");
  }
  return spt_elem;
}

void spt_delete_elem(struct hash *spt, void *upage) {
  struct spt_elem  entry;
  entry.user_addr = upage;
  struct hash_elem* _hash_elem = hash_delete(spt, &entry.hash_elem);
  if ( _hash_elem == NULL) {
    PANIC ("Failed to delete from into supplemental page table");
  }
  free(hash_entry(_hash_elem,struct spt_elem,hash_elem));
}

void spt_set_state(struct hash *spt, void *upage, enum page_state state){
  struct spt_elem *sptElem = spt_find_elem(spt, upage);
  sptElem->state = state;
}

void spt_set_stack(struct hash *spt, void *upage) {
  struct spt_elem *sptElem = spt_find_elem(spt, upage);
  sptElem->state = STACK;
}

void spt_set_loaded_executable(struct hash *spt, void *upage) {
  struct spt_elem *sptElem = spt_find_elem(spt, upage);
  sptElem->state = LOADED_EXECUTABLE;
}



#endif /* vm/page.h */
