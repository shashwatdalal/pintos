#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <hash.h>
#include <stdint.h>
#include <vm/page.h>
#include "synch.h"
#include "fixed_point.h"

/* States in a thread's life cycle. */
enum thread_status {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Userprog. */
#define MAX_OPEN_FILES 128              /* Maximum number of open files per process. */
#define DEFAULT_FD 2                    /* First FD that is allocated */
/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Used in timer.c */
    int64_t wake_time;                  /* Used to sleep threads. */
    struct semaphore sema;              /* Blocks threads. */
    struct list_elem sleepelem;         /* List element for all threads list. */

    /* Used by advanced scheduler when thread_mlfqs is enabled */
    int niceness;                       /* Niceness of thread */
    fp_t recent_cpu;                    /* Moving average of recent allocated cpu time */
    struct list_elem readyelem;

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    struct list donations;              /* List of donations applied to this thread.  */

    uint32_t *pagedir;                  /* Page directory. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    struct file * fds[MAX_OPEN_FILES];  /* Array of file descriptors */
    int highest_fd;                     /* Current highest file descriptor */
    struct file* loaded_file;           /* File the thread is executing */
#endif

    struct hash spt;                    /* Supplementary page table */

    int num_stack_pages;               /* Number of stack pages owned by this thread */

    struct hash mmap;                  /* table containing mmap mappings */
    uint32_t highest_mmap_id;           /* containing current highest mapid for mmap table */

    struct semaphore exit_sema;         /* Semaphore used by parent to wait for this process to exit */

    struct semaphore load_sema;         /* Semaphore used by creator to find out whether the load process for
                                           this thread has completed */

    struct list children;               /* List of threads created by this thread which are yet to be waited on */

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* ------------------------------------------------------------------------- */

struct child_thread {
    tid_t tid;                    /* Thread id of this child thread */
    struct list_elem child_elem;  /* Allows to be inserted into a threads list of children */
    struct semaphore *exit_sema;  /* Reference to the child's exit_sema so the parent thread
                                      can wait for the child to finish executing */
};

struct donation {
    struct thread *from_thread;           /* The thread from which the donation came.  */
    struct lock *lock;                    /* The lock which triggered the donation.  */

    struct list_elem elem;               /* List element.  */
};

struct thread *get_thread_in_all_list(tid_t thread_id);

/* ------------------------------------------------------------------------- */

void thread_init(void);

void thread_start(void);

void thread_tick(void);

void thread_print_stats(void);

typedef void thread_func(void *aux);

tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);

void thread_unblock(struct thread *);

struct thread *thread_current(void);

tid_t thread_tid(void);

const char *thread_name(void);

void thread_exit(void) NO_RETURN;

void thread_yield(void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);

void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);

int thread_get_donated_priority(const struct thread *t);

void thread_set_priority(int);

int thread_get_nice(void);

void thread_set_nice(int);

int thread_get_recent_cpu(void);

int thread_get_load_avg(void);


void update_priority(struct thread *t);

void update_load_avg(void);

void update_recent_cpu(struct thread *t);

void increment_current_recent_cpu(void);

void thread_preempt(void);

void thread_preempt_eq(void);

int thread_put_file(struct thread *t, struct file *_file);

struct file *thread_get_file(struct thread *t, int fd);

void thread_close_file(struct file *_file, int fd);

#endif /* threads/thread.h */
