#ifndef VM_FRAME_TABLE_H
#define VM_FRAME_TABLE_H

#include "threads/thread.h"
#include "lib/stdint.h"

struct frame_table {
    struct hash frame_hash_table;
    //TODO: add atomic integer to increase index
    struct list frame_order;
    struct list_elem *next_to_evict;
    size_t max_size;
    struct lock lock;
};

struct frame {
    struct hash_elem hash_elem;
    struct list_elem list_elem;
    void *kvaddr;
    struct list *user_pages;
    bool pinned;
};

struct page_info {
    struct thread *owner;  // The thread which owns this page
    void *upage;           // User page number
    struct list_elem elem; // Added to list of user_pages within frame struct
};

/* --------------------------------------------------------------------------
                               Frame Table Methods
   -------------------------------------------------------------------------- */

void frame_init(size_t max_size);

void frame_table_lock_acquire();

void
frame_table_lock_release();


struct frame* frame_add(void *upage, void *kpage);

struct frame *frame_get(void *vaddr);

struct frame *frame_remove_from_table(void *kpage);

void frame_free_frame(struct frame* _frame);

struct list* frame_remove_all(struct thread* t, struct list* l);

void frame_free_all(struct list* l);

void frame_append_entry(void *upage, void *kpage);


size_t frame_size(void);

bool frame_is_full(void);

/* --------------------------------------------------------------------------
                                 Eviction Methods
   -------------------------------------------------------------------------- */
struct frame *choose_eviction_candidate(void);

void advance_candidate(struct list_elem *to_be_deleted);

void *evict_pages(int n);

struct frame *evict_and_swap_page(void *upage, block_sector_t sector);

struct frame *find_immediately_next_page(struct frame *f, bool *is_free);

#endif //PINTOS_TASK0_FRAME_TABLE_H
