#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <bitmap.h>
#include <hash.h>
#include <devices/block.h>
#include <filesys/off_t.h>

enum page_state {
    NORMAL_IN_FRAME,
    MMAP_IN_FRAME,
    IN_SWAP,
    IN_FILESYS,
    ZEROPAGE,
    STACK,
    UNLOADED_EXECUTABLE,
    LOADED_EXECUTABLE
};


struct spt_elem {
    enum page_state state;
    void *user_addr;
    uint32_t address; //TODO (Marcel/Andy): sure this is the right data-type?
    uint32_t read_bytes;
    bool writable;
    struct hash_elem hash_elem;
};

unsigned spt_hash(const struct hash_elem *, void *aux);

bool spt_less(const struct hash_elem *, const struct hash_elem *, void *aux);

struct spt_elem *spt_find_elem(struct hash *spt, void* upage);

void spt_set_swap_sector(struct hash *spt, void *upage, block_sector_t sector);

block_sector_t spt_get_swap_sector(struct hash *spt, void *upage);

void spt_set_executable(struct hash *spt, void *upage, off_t file_offset, uint32_t read_bytes);

off_t spt_get_offset(struct hash *spt, void *upage);

bool spt_get_writable(struct hash *spt, void *upage);

void *spt_get_filesys(struct hash *spt, void *upage);

void spt_set_filesys(struct hash *spt, void *upage, void *file_location);

uint32_t spt_get_read_bytes(struct hash *spt, void *upage);

void spt_set_zero_page(struct hash *spt, void *upage);

bool spt_is_zero_page(struct hash *spt, void *upage);

void spt_set_loaded_executable(struct hash *spt, void *upage);

enum page_state spt_get_state(struct hash *spt, void *upage);

struct spt_elem *spt_add_new_elem(struct hash *spt, void *upage, enum page_state state,bool writable);

void spt_delete_elem(struct hash *spt, void *upage);

void spt_set_state(struct hash *spt, void *upage, enum page_state state);

void spt_set_stack(struct hash *spt, void *upage);

struct spt_elem *spt_is_page_mapped(struct hash *spt, void *upage);

#endif /* vm/page.h */
