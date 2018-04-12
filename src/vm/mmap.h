#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <hash.h>


struct mmap_elem{
  uint32_t mapid;
  int fd;
  uint32_t uvaddr;
  uint32_t number_of_pages;
  struct hash_elem _hash_elem;
};

unsigned mmap_hash(const struct hash_elem *, void *aux);

bool mmap_less(const struct hash_elem *, const struct hash_elem *, void *aux);
void mmap_delete(struct thread*t, uint32_t mapid,bool delete);
void mmap_free(struct hash_elem *e, void *aux);
struct mmap_elem* mmap_get(struct thread* t,uint32_t mapid);
struct mmap_elem *mmap_add(struct thread* t,uint32_t uvaddr,uint32_t number_of_pages,
uint32_t fd);

void mmap_munmap(int mapid, const struct thread *t,bool delete);
void mmap_munmap_all(struct thread *t);
#endif /* vm/mmap.h */