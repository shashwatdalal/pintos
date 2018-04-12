#ifndef VM_MMAP_C
#define VM_MMAP_C


#include <threads/malloc.h>
#include <threads/thread.h>
#include <threads/vaddr.h>
#include <filesys/file.h>
#include <userprog/pagedir.h>
#include <threads/palloc.h>
#include "mmap.h"
#include "frame.h"

unsigned mmap_hash(const struct hash_elem *entry_, void *aux) {
  const struct mmap_elem *entry = hash_entry(entry_, struct mmap_elem,
                                             _hash_elem);
  return hash_bytes(&entry->mapid, sizeof entry->mapid);
}


bool
mmap_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux) {
  const struct mmap_elem *a = hash_entry(a_, struct mmap_elem, _hash_elem);
  const struct mmap_elem *b = hash_entry(b_, struct mmap_elem, _hash_elem);
  return a->mapid < b->mapid;
}

struct mmap_elem *
mmap_add(struct thread *t, uint32_t uvaddr, uint32_t number_of_pages,
         uint32_t fd) {
  struct mmap_elem *_mmap_elem = malloc(sizeof(struct mmap_elem));
  if (_mmap_elem == NULL) {
    //TODO: MALLOC PRINT ERROR
    PANIC ("Malloc failed: thread.c:thread_get_new_mmap_elem()");
  }
  // set up unique mapid
  _mmap_elem->mapid = t->highest_mmap_id++;

  _mmap_elem->uvaddr = uvaddr;
  _mmap_elem->number_of_pages = number_of_pages;
  _mmap_elem->fd = fd;

  struct hash_elem *inserted = hash_insert(&t->mmap, &_mmap_elem->_hash_elem);
  if (inserted != NULL) {
    PANIC ("Failed to insert into mmap table");
  }
  return _mmap_elem;
}

void mmap_delete(struct thread *t, uint32_t mapid, bool delete) {
  if (mapid < 0 || mapid >= t->highest_mmap_id) {
    PANIC("Invalid mapid trying to be freed");
  }
  struct mmap_elem *found = mmap_get(t, mapid);
  if (found == NULL) {
    PANIC("No mapid found");
  }
  if (delete) {
    hash_delete(&t->mmap, &found->_hash_elem);
    free(found);
  }
}

void mmap_free(struct hash_elem *e, void *aux) {
  free(hash_entry(e, struct mmap_elem, _hash_elem));
}

struct mmap_elem *mmap_get(struct thread *t, uint32_t mapid) {
  struct mmap_elem to_find;
  to_find.mapid = mapid;
  return hash_entry(hash_find(&t->mmap, &to_find._hash_elem), struct mmap_elem,
                    _hash_elem);
}

void mmap_munmap_all(struct thread *t) {
  struct hash_iterator i;
  hash_first(&i, &t->mmap);
  while (hash_next(&i)) {
    struct mmap_elem *_map_elem = hash_entry (hash_cur(&i), struct mmap_elem,
                                              _hash_elem);
    mmap_munmap(_map_elem->mapid, t, false);
  }
  hash_destroy(&t->mmap, mmap_free);
}

void mmap_munmap(int mapid, const struct thread *t, bool delete) {
  struct mmap_elem *_mmap = mmap_get(t, (uint32_t) mapid);
  struct hash *_spt = &t->spt;
  for (int i = 0; i < _mmap->number_of_pages; i++) {
    void *upage = (void *) _mmap->uvaddr + PGSIZE * i;
    struct spt_elem *entry = spt_find_elem(_spt, upage);
    enum page_state _page_state = entry->state;
    if (_page_state == IN_FILESYS) {
      //remove from supplementary page table
      spt_delete_elem(_spt, upage);
    } else if (_page_state == MMAP_IN_FRAME) {
      //remove or modify from frame_table
      struct file *mapped_file = thread_get_file(t, _mmap->fd);
      void *kpage = pagedir_get_page(t->pagedir, upage);
      if (pagedir_is_dirty(t->pagedir, upage)) {
        if (i == _mmap->number_of_pages - 1) {
          file_write_at(
            mapped_file, kpage, file_length(mapped_file) % PGSIZE,
            PGSIZE * i);
        } else {
          file_write_at(mapped_file, kpage, PGSIZE, PGSIZE * i);
        }
      }
      struct frame *_frame = frame_get(kpage);

      //remove frame from palloc bitmap
      palloc_free_page(_frame->kvaddr);

      //remove from supplementary page table
      spt_delete_elem(_spt, upage);

      //remove from page_table
      pagedir_clear_page(t->pagedir, upage);
      //remove or modify from shared_files
    } else {
      PANIC("Page in unauthorized location");
    }
  }
  mmap_delete(t, mapid, delete);
}

#endif /* vm/mmap.c */