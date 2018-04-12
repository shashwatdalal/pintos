#ifndef PINTOS_TASK1_READY_LIST_H
#define PINTOS_TASK1_READY_LIST_H
#define EMPTY 0

#include "../lib/kernel/list.h"
#include "../lib/stdint.h"
#include "thread.h"
#include "synch.h"

//struct
struct ready_list {
    struct list queues[64]; //array of queues of elements
    int size; //sum of elements in each queue
    int highest_priority; //index of highest priority currently in the list
};

//methods
void ready_list_init(struct ready_list *list);

struct thread *ready_list_peek_top(struct ready_list *list);

struct thread *ready_list_pop_top(struct ready_list *list);

void ready_list_insert(struct ready_list *list, struct thread *thread);

void ready_list_remove(struct ready_list *list, struct thread *thread);

bool ready_list_empty(struct ready_list *list);

int ready_list_get_size(struct ready_list *list);

#endif //PINTOS_TASK1_READY_LIST_H
