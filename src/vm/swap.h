
#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <lib/kernel/hash.h>
#include <lib/stdint.h>
#include <vm/frame.h>
#include <devices/block.h>

#define MAX_SLOTS 512
#define NUMBER_OF_SECTORS_PER_PAGE 8

struct swap_table {
    struct hash table;
    struct bitmap * used_map;
    struct block *swap_partition;
    struct lock lock;
};

struct swap {
    struct hash_elem hash_elem;
    block_sector_t sector;
    struct list *user_pages;
};



int swap_hash_size();

void swap_init();

void swap_add_swap(void *vaddr);

void swap_allocate_block(struct frame *frame);

void swap_lock_acquire();

void swap_lock_release();

block_sector_t swap_write_frame_to_swap_slot(struct frame* frame_to_swap);

void swap_bring_sector_to_frame(struct frame* frame, block_sector_t sector);

bool swap_is_full();

struct swap *swap_delete(struct swap *swap);

#endif //PINTOS_TASK0_FRAME_TABLE_H
