#ifndef USERPROG_AVLTREE_H
#define USERPROG_AVLTREE_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <threads/synch.h>
#include <threads/malloc.h>
#include <threads/thread.h>

struct node {
    tid_t tid;          // Holds the id of thread
    int status;         // Hold status of thread defined by particular tree
    struct node *left;  // Left child of node
    struct node *right; // Right child of node
    int height;         // Height of node
};

struct synch_tree {
    struct node *node;       // Tree containing data
    struct semaphore access; // Semaphore controlling access to the data
};

void synch_tree_init(struct synch_tree *tree);

void add_status(struct synch_tree *tree, tid_t tid, int status);

bool try_remove_status(struct synch_tree *tree, tid_t tid);

int get_status(struct synch_tree *tree, tid_t tid);

bool did_get_status(struct synch_tree *tree, tid_t tid, int *status);

#endif //USERPROG_AVLTREE_H
