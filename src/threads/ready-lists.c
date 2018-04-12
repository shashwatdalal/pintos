#include <stdio.h>
#include "threads/ready-lists.h"

//methods
void ready_list_init(struct ready_list *list) {
  ASSERT(list != NULL);
  list->size = 0;
  list->highest_priority = 0;
  for (int i = 0; i <= PRI_MAX; i++) {
    list_init(&(list->queues[i]));
  }
}

struct thread *ready_list_peek_top(struct ready_list *list) {
  ASSERT(!ready_list_empty(list));
  ASSERT(list_size(&(list->queues[list->highest_priority])) != 0);

  struct list_elem *thread_elem =
    list_begin(&(list->queues[list->highest_priority]));
  return list_entry(thread_elem, struct thread, readyelem);
}

struct thread *ready_list_pop_top(struct ready_list *list) {
  ASSERT(!ready_list_empty(list));
  ASSERT(list_size(&(list->queues[list->highest_priority])) != 0);

  struct list_elem *thread_elem =
    list_pop_front(&(list->queues[list->highest_priority]));

  //update highest_priority
  while (list->highest_priority > PRI_MIN
         && list_size(&(list->queues[list->highest_priority])) == 0) {
    list->highest_priority--;
  }
  list->size--;
  return list_entry(thread_elem, struct thread, readyelem);
}

void ready_list_insert(struct ready_list *list, struct thread *thread) {
  ASSERT(thread->priority <= PRI_MAX);
  ASSERT(thread->priority >= PRI_MIN);

  int priority = 0;

  if (thread_mlfqs) {
    priority = thread->priority;
  } else {
    priority = thread_get_donated_priority(thread);
  }

  if (priority > list->highest_priority) {
    list->highest_priority = priority;
  }

  list_push_back(&(list->queues[priority]), &thread->readyelem);
  list->size++;
}


void ready_list_remove(struct ready_list *list, struct thread *thread) {
  list_remove(&thread->readyelem);
  while (list->highest_priority > PRI_MIN
         && list_size(&(list->queues[list->highest_priority])) == 0) {
    list->highest_priority--;
  }
  list->size--;
}

bool ready_list_empty(struct ready_list *list) {
  return list->size == EMPTY;
}

int ready_list_get_size(struct ready_list *list) {
  return list->size;
}
