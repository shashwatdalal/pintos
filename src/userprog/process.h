#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <vm/page.h>
#include "threads/thread.h"
#include "userprog/avltree.h"

#define PROCESS_WAIT_FAIL -1
#define MAX_NUM_ARGUMENTS 64

tid_t process_execute(const char *arguments);

int process_wait(tid_t);

void process_exit(void);

void process_activate(void);

void process_init(void);

void process_filesys_lock_acquire(void);

void process_filesys_lock_release(void);

void add_exit_status(int tid, int exit_status);

int get_exit_status(int tid);

bool did_get_exit_status(int tid, int *exit_status);

void remove_exit_status(int tid);

void add_load_status(int tid, int load_status);

bool did_get_load_status(int tid, int *load_status);

int get_load_status(int tid);

bool
load(const char *file_name, void (**eip)(void), void **esp);

bool
process_install_page(void *upage, void *kpage,
                     bool writable, enum page_state state);

#endif /* userprog/process.h */
