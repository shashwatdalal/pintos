#include <vm/frame.h>
#include <userprog/pagedir.h>
#include <threads/malloc.h>
#include <threads/thread.h>
#include <lib/stdio.h>
#include <threads/vaddr.h>
#include <string.h>
#include <devices/block.h>
#include <userprog/process.h>
#include <threads/interrupt.h>
#include "swap.h"
#include "mmap.h"
#include <filesys/file.h>
#include <lib/user/syscall.h>

static struct frame_table _frame_table;

static struct page_info *init_page_info(void *upage);

void store_executable_in_filesys(const struct frame *frame_to_evict);

void store_mmap_back_in_filesys(const struct frame *frame_to_evict);

unsigned
frame_hash(const struct hash_elem *entry_, void *aux) {
  const struct frame *entry = hash_entry(entry_, struct frame, hash_elem);
  return hash_bytes(&entry->kvaddr, sizeof entry->kvaddr);
}

bool
frame_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux) {
  const struct frame *a = hash_entry(a_, struct frame, hash_elem);
  const struct frame *b = hash_entry(b_, struct frame, hash_elem);

  return a->kvaddr < b->kvaddr;
}

void
frame_init(size_t max_size) {
  hash_init(&_frame_table.frame_hash_table, frame_hash, frame_less, NULL);
  list_init(&_frame_table.frame_order);
  _frame_table.max_size = max_size;
  lock_init(&_frame_table.lock);
};

void
frame_table_lock_acquire() {
  lock_acquire(&_frame_table.lock);
}

void
frame_table_lock_release() {
  lock_release(&_frame_table.lock);
}

/* Function sets all pages accessed bits to false and returns bool
 * which states whether any of the access bits were 1 prior */
bool
frame_accessed(struct frame *frame) {
  struct list_elem *e;
  bool frame_was_accessed = false;
  for (e = list_begin(frame->user_pages);
       e != list_end(frame->user_pages); e = list_next(e)) {
    struct page_info *info = list_entry(e, struct page_info, elem);
    frame_was_accessed = frame_was_accessed ||
                         pagedir_is_accessed(info->owner->pagedir,
                                             info->upage);
    pagedir_set_accessed(info->owner->pagedir, info->upage, 0);
  }
  return frame_was_accessed;
}

struct frame *
frame_get(void *vaddr) {
  struct frame to_find;
  to_find.kvaddr = vaddr;
  return hash_entry(
    hash_find(&_frame_table.frame_hash_table, &to_find.hash_elem),
    struct frame, hash_elem);
}

struct frame *
frame_remove_from_table(void *vaddr) {
  struct frame to_find;
  to_find.kvaddr = vaddr;
  struct hash_elem *to_remove = hash_find(&_frame_table.frame_hash_table,
                                          &to_find.hash_elem);
  struct frame *removed = hash_entry(to_remove, struct frame, hash_elem);
  hash_delete(&_frame_table.frame_hash_table, to_remove);
  list_remove(&removed->list_elem);
  return removed;
}

void
frame_free_frame(struct frame* _frame){
  ASSERT(_frame != NULL);
    struct list_elem *e = NULL;
    for (e = list_begin(_frame->user_pages);
         e != list_end(_frame->user_pages);) {
      //free each element in list
      struct page_info *_page_info =
        list_entry(e, struct page_info, elem);
      e = list_remove(e);
      free(_page_info);
    }
    //free list
    free(_frame->user_pages);
    //free the frame struct
    free(_frame);
}

struct frame *
frame_add(void *upage, void *kpage) {
  ASSERT (pg_ofs(upage) == 0);
  ASSERT (pg_ofs(kpage) == 0);

  struct frame *new_frame = malloc(sizeof(struct frame));
  if (new_frame == NULL) {
    PANIC ("malloc failure: src/vm/frame.c -> frame_add_entry (1)");
  }

  struct list *user_pages = malloc(sizeof(struct list));
  if (user_pages == NULL) {
    PANIC ("malloc failure: src/vm/frame.c -> frame_add_entry (2)");
  }
  list_init(user_pages);
  new_frame->user_pages = user_pages;
  new_frame->kvaddr = kpage;
  new_frame->pinned = false;
  struct page_info *new_page_info = init_page_info(upage);
  list_push_back(user_pages, &new_page_info->elem);
  list_insert(list_tail(&_frame_table.frame_order), &new_frame->list_elem);
  hash_insert(&_frame_table.frame_hash_table, &new_frame->hash_elem);
  return new_frame;
}

void
frame_append_entry(void *upage, void *kpage) {
  struct page_info *p_info = init_page_info(upage);
  list_push_back(frame_get(kpage)->user_pages, p_info);
}

static struct page_info *
init_page_info(void *upage) {
  struct page_info *p_info = malloc(sizeof(struct page_info));
  if (p_info == NULL) {
      PANIC ("malloc failure: src/vm/frame.c -> init_page_info");
  }
  p_info->owner = thread_current();
  p_info->upage = upage;
  return p_info;
}

size_t
frame_size() {
  return hash_size(&_frame_table.frame_hash_table);
}

bool
frame_is_full() {
  return frame_size() == _frame_table.max_size;
}

struct list *
frame_remove_all(struct thread* t, struct list* l) {
  struct hash_iterator i;
  hash_first (&i, &_frame_table.frame_hash_table);
  while (hash_next (&i)) {
    struct frame *cur_frame = hash_entry (hash_cur (&i), struct frame, hash_elem);
    if(list_size(cur_frame->user_pages) == 1) {
      //not shared
      if(list_entry(list_begin(cur_frame->user_pages),struct page_info, elem)
           ->owner->tid == t->tid) {
        //remove from hashmap and add to return list
        advance_candidate(&cur_frame->list_elem);
        list_remove(&cur_frame->list_elem);
        list_push_back(l,&cur_frame->list_elem);
        hash_delete(&_frame_table.frame_hash_table, &cur_frame->hash_elem);
      }
    } else {
      //shared
      struct list_elem* e;
      //remove mentions of thread
      for (e = list_begin (l); e != list_end (l); ) {
        if(list_entry(e,struct page_info, elem)
          ->owner->tid == t->tid) {
          e = list_next(e);
          list_remove(e->prev);
        } else {
          e = list_next(e);
        }
      }
    }
  }
  return l;
}

void
frame_free_all(struct list* l) {
  struct list_elem* e;
  //free each frame in list
  for (e = list_begin (l); e != list_end (l);) {
    e = e->next;
    frame_free_frame(list_entry(e->prev,struct frame, list_elem));
  }
}


/* --------------------------------------------------------------------------
                                 Eviction Methods
   -------------------------------------------------------------------------- */

/* Using next_to_evict as a reference, return the struct frame for the next
   frame to evict. NOTE: the frame table is locked for the duration of this */
struct frame *
choose_eviction_candidate(void) {

  // Disable interrupts to preserve the accessed bit of pages

  struct list_elem *e = list_begin(&_frame_table.frame_order);

  while (true) {
    struct frame *frame = list_entry(e, struct frame, list_elem);
    if (!frame_accessed(frame)) {
      // Candidate for eviction found
      return frame;
    } else {
      struct list_elem *prev = e;
      e = list_next(e);
      list_remove(prev);
      list_push_back(&_frame_table.frame_order, prev);
    }

  }

  NOT_REACHED();
}

/* Move the next_to_evict pointer to its next element. */
void
advance_candidate(struct list_elem *to_be_deleted) {
  if (_frame_table.next_to_evict == to_be_deleted) {
    _frame_table.next_to_evict = list_next(_frame_table.next_to_evict);
  }
}

int get_frame_page_type(struct frame *frame) {
  struct page_info *_page_info = list_entry(list_begin(frame->user_pages),struct page_info,elem);
  return spt_get_state(&_page_info->owner->spt,_page_info->upage);
}

/* Evict a frame (clear its memory).
   Naively free n frames starting at the next eviction candidate after
   next_to_evict.
   TODO: Find longest chain of free pages and start there. */
void *
evict_pages(int n) {
  ASSERT(n > 0);

  // Lock the swap table and the frame table
  swap_lock_acquire();

  if (n == 1) {
    // Ensure a call palloc_get_page delegates here only when RAM is full
    ASSERT (frame_is_full());
  }


  if (swap_is_full()) {
    // RAM has ran out of available frames and swap is full
    PANIC ("palloc_get_page: out of pages and swap partition full");
  }

  // Find first candidate page for eviction
  struct frame *frame_to_evict = choose_eviction_candidate();
  void *start_page = frame_to_evict->kvaddr;
  bool frame_free = false;
  struct frame *next_frame;
  ASSERT(pg_ofs(start_page) == 0);

  for (int i = 0; i < n; i++) {
    ASSERT(pg_ofs(frame_to_evict->kvaddr) == 0);
    if (frame_free) {
      // This frame is already free so do nothing
      frame_to_evict = find_immediately_next_page(frame_to_evict, &frame_free);
      continue;
    }
    struct list_elem* e;
    for (e = list_begin(frame_to_evict->user_pages);
         e != list_end(frame_to_evict->user_pages); e = list_next(e)) {
    }


      // Obtain the kernel virtual address of the page (kernel space)
    struct list *user_pages = frame_to_evict->user_pages;
    void*page = frame_to_evict->kvaddr;
    //Writes frame to available swap slot and records changes in pagedir and spt
    if(get_frame_page_type(frame_to_evict) == LOADED_EXECUTABLE) {
      store_executable_in_filesys(frame_to_evict);
    } else if(get_frame_page_type(frame_to_evict) == MMAP_IN_FRAME) {
      store_mmap_back_in_filesys(frame_to_evict);
    } else {
      //regular swap
      //move and edit information regarding old frame
      block_sector_t sector = swap_write_frame_to_swap_slot(frame_to_evict);
      //set up frame and spt for new frame
    }



    // Set the previously occupied frame to 0s
    // Move the the frame directly after this;
    next_frame = find_immediately_next_page(frame_to_evict, &frame_free);
    frame_free_frame(frame_remove_from_table(frame_to_evict->kvaddr));
    frame_to_evict = next_frame;
  }

  // 4) Return the frame
  ASSERT(pg_ofs(start_page) == 0);
  swap_lock_release();
  return start_page;
}

void store_mmap_back_in_filesys(const struct frame *frame_to_evict) {//load back to memory
  struct list_elem* e;
  for(e = list_begin(frame_to_evict->user_pages) ; e != list_end(frame_to_evict->user_pages) ; e = list_next(e)) {
        struct page_info *info = list_entry(e, struct page_info, elem);
        struct spt_elem *_spt_elem = spt_find_elem(&thread_current()->spt, info->upage);
        struct mmap_elem *_mmap_elem = mmap_get(info->owner, _spt_elem->address);
        if (pagedir_is_dirty(info->owner->pagedir, _spt_elem->user_addr)) {
          struct file *_file = thread_get_file(info->owner, _mmap_elem->fd);
          int page_number = (((unsigned) info->upage - _mmap_elem->uvaddr) / PGSIZE);
          if (page_number + 1 == _mmap_elem->number_of_pages) {
            //final page
            file_write_at(_file, frame_to_evict->kvaddr, file_length(_file) % PGSIZE,
                          PGSIZE * page_number);
          } else {
            //any other page
            file_write_at(_file, frame_to_evict->kvaddr, PGSIZE, PGSIZE * page_number);
          }
          _spt_elem->state = IN_FILESYS;
        }
  }
}

void store_executable_in_filesys(const struct frame *frame_to_evict) {//load executable back to memory
  struct list_elem* e;
  for(e = list_begin(frame_to_evict->user_pages) ; e != list_end(frame_to_evict->user_pages) ; e = list_next(e)) {
        struct page_info *info = list_entry(e, struct page_info, elem);
        struct spt_elem* _spt_elem = spt_find_elem(&info->owner->spt, info->upage);
        if (pagedir_is_dirty(thread_current()->pagedir, _spt_elem->user_addr)) {
          struct file* file = info->owner->loaded_file;
          uint32_t offset = spt_get_offset(&info->owner->spt, info->upage);
          file_write_at(file, frame_to_evict->kvaddr, _spt_elem->read_bytes, offset);
        }
        _spt_elem->state = UNLOADED_EXECUTABLE;
      }
}

/* Find the next frame for eviction and swap its contents with the supplied
   block sector. */
struct frame *
evict_and_swap_page(void *upage, block_sector_t sector) {
 lock_acquire(&_frame_table.lock);
  ASSERT (frame_is_full());

  // 1) Find candidate pages for eviction using the frame table (and linked
  //    supplementary page tables) - referred to as CP

  void *page = evict_pages(1);
  struct frame* evicted_frame = frame_add(upage,page);
  // 2) Move CP to the swap partition and swap the page with the specified block;
  swap_bring_sector_to_frame(evicted_frame, sector);


  // 3) Record the change in the page and supplementary table
  struct list *user_pages = evicted_frame->user_pages;
  for (struct list_elem *e = list_begin(user_pages);
       e != list_end(user_pages); e = list_next(e)) {
    struct page_info* _page_info = list_entry(e,struct page_info,elem);
    bool writable = spt_find_elem(&_page_info->owner->spt,_page_info->upage)->writable;
    process_install_page(_page_info->upage,evicted_frame->kvaddr,writable,NORMAL_IN_FRAME);
  }

  // 4) Return the frame
 lock_release(&_frame_table.lock);
  return evicted_frame;
}

/* Find the address of the page directly after the given page and set the value
   of the passed in bool to true iff the page is free. */
struct frame *
find_immediately_next_page(struct frame *f, bool *is_free) {
  void *target_kvaddr = f->kvaddr + PGSIZE;
  // Look up target_kvaddr in frame table
  *is_free = frame_get(target_kvaddr) == NULL;
  return target_kvaddr;
}
