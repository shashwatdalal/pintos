#include <vm/swap.h>
#include <userprog/pagedir.h>
#include <threads/malloc.h>
#include <lib/string.h>
#include <lib/stdio.h>
#include <threads/vaddr.h>


static struct swap_table _swap_table;


static struct swap *swap_add(struct swap *swap);

unsigned
swap_hash(const struct hash_elem *entry_, void *aux) {
  const struct swap *entry = hash_entry(entry_, struct swap, hash_elem);
  return hash_bytes(&entry->sector, sizeof entry->sector);
}

bool
swap_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux) {
  const struct swap *a = hash_entry(a_, struct swap, hash_elem);
  const struct swap *b = hash_entry(b_, struct swap, hash_elem);
  return a->sector < b->sector;
}

void
swap_init() {
  hash_init(&_swap_table.table, swap_hash, swap_less, NULL);
  struct bitmap *_bitmap = bitmap_create(MAX_SLOTS);
  _swap_table.used_map = _bitmap;
  lock_init(&_swap_table.lock);
};

void
swap_lock_acquire() {
 lock_acquire(&_swap_table.lock);
}

void
swap_lock_release() {
 lock_release(&_swap_table.lock);
}

struct swap *
swap_find(block_sector_t sector) {
  struct swap to_find;
  to_find.sector = sector;
  return hash_entry(hash_find(&_swap_table.table, &to_find.hash_elem),
                    struct swap, hash_elem);
}

static struct swap *
swap_add(struct swap *swap) {
  return hash_entry(hash_insert(&_swap_table.table, &swap->hash_elem),
                    struct swap, hash_elem);
}

block_sector_t
swap_write_frame_to_swap_slot(struct frame* frame_to_swap) {
  swap_lock_acquire();
  _swap_table.swap_partition = block_get_role(BLOCK_SWAP);
  struct swap *swap_slot = malloc(sizeof(struct swap));
  if (swap_slot == NULL) {
    PANIC (
            "malloc failure: src/vm/swap.c -> add_and_write_swap_slot (1)");
  }
  swap_slot->user_pages = frame_to_swap->user_pages;
  block_sector_t sector = bitmap_scan_and_flip(_swap_table.used_map, 0, 1, 0);
  swap_slot->sector = sector;
  swap_add(swap_slot);

  for (struct list_elem *e = list_begin(swap_slot->user_pages);
       e != list_end(swap_slot->user_pages); e = list_next(e)) {
    // Remove the previous frame occupant from any page tables
    struct page_info *page_info = list_entry(e, struct page_info, elem);
    pagedir_clear_page(page_info->owner->pagedir, page_info->upage);
    // Alert the SPT containing CP that it is in swap
    spt_set_swap_sector(&page_info->owner->spt, page_info->upage, sector);
  }

  for (int i = 0; i < NUMBER_OF_SECTORS_PER_PAGE; i++) {
    block_write(_swap_table.swap_partition, sector*NUMBER_OF_SECTORS_PER_PAGE + i, frame_to_swap->kvaddr + BLOCK_SECTOR_SIZE * i);
  }


  swap_lock_release();
  return sector;
}

void
swap_replace_swap_slot_with_frame(void *frame_addr, block_sector_t sector) {
  //TODO needs testing
  //get swap_slot from sector
  struct swap *swap_slot = swap_find(sector);
  ASSERT(swap_slot != NULL);
  //write to frame_addr
  struct frame *_frame = frame_get((void *) frame_addr);
  _frame->user_pages = swap_slot->user_pages;
  //read sector into frame and write sector into frame
  for (int i = 0; i < NUMBER_OF_SECTORS_PER_PAGE; i++) {
    unsigned buffer[BLOCK_SECTOR_SIZE];
    unsigned temp[BLOCK_SECTOR_SIZE];
    memcpy(frame_addr + BLOCK_SECTOR_SIZE * i, temp, BLOCK_SECTOR_SIZE);
    block_read(_swap_table.swap_partition, sector + i, buffer);
    memcpy(buffer, frame_addr + BLOCK_SECTOR_SIZE * i, BLOCK_SECTOR_SIZE);
    block_write(_swap_table.swap_partition, sector + i, temp);
  }
}

void
swap_free_swap_slot(block_sector_t sector) {
  lock_acquire(&_swap_table.lock);
  //TODO needs testing
  //remove from hash-map
  struct swap *_swap = swap_find(sector);
  swap_delete(_swap);

  //free each element in list
  struct list_elem *e = NULL;
  for (e = list_begin(_swap->user_pages);
       e != list_end(_swap->user_pages);) {
    struct page_info *_page_info =
            list_entry(e, struct page_info, elem);
    e = list_remove(e);
    free(_page_info);
  }

  //free list
  free(_swap->user_pages);
  //free hash-elem
  free(_swap);

  //free from bitmap
  for (int i = 0; i < NUMBER_OF_SECTORS_PER_PAGE; i++) {
    bitmap_set(_swap_table.used_map, sector + i, 0);
  }
  lock_release(&_swap_table.lock);
}

int swap_hash_size() {
  return hash_size(&_swap_table.table);
}

bool
swap_is_full() {
  return false;
  return bitmap_size(_swap_table.used_map) ==
         hash_size(&_swap_table.table);
}

struct swap *
swap_delete(struct swap *swap) {
  return hash_entry(hash_delete(&_swap_table.table, &swap->hash_elem),
                    struct swap, hash_elem);
}

void
swap_bring_sector_to_frame(struct frame* frame, block_sector_t sector) {
  _swap_table.swap_partition = block_get_role(BLOCK_SWAP);
  lock_acquire(&_swap_table.lock);
  struct swap *swap_slot = swap_find(sector);
  ASSERT(swap_slot != NULL);
  frame->user_pages = swap_slot->user_pages;
  for (int i = 0; i < NUMBER_OF_SECTORS_PER_PAGE; i++) {
    block_read(_swap_table.swap_partition,sector*NUMBER_OF_SECTORS_PER_PAGE+i, frame->kvaddr+BLOCK_SECTOR_SIZE*i);
  }
  swap_delete(swap_slot);
  free(swap_slot);
  bitmap_set(_swap_table.used_map,sector,1);
  lock_release(&_swap_table.lock);
}
