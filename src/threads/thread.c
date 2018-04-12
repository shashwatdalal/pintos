#include "threads/thread.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <devices/timer.h>
#include <filesys/filesys.h>
#include <filesys/file.h>
#include <userprog/process.h>
#include <vm/page.h>
#include <vm/mmap.h>
#include <vm/frame.h>
#include <userprog/pagedir.h>
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/vaddr.h"
#include "threads/ready-lists.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
struct ready_list ready_lists;
static struct list priority_queues[64];

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;
static struct lock all_list_lock;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

static fp_t load_avg;

#define DEFAULT_CPU 0
#define DEFAULT_NICE 0
/* Stack frame for kernel_thread(). */
struct kernel_thread_frame {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */


bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);

static struct thread *running_thread(void);

static struct thread *next_thread_to_run(void);

static void
init_thread(struct thread *, const char *name, int priority, int niceness,
            fp_t recent_cpu);

static bool is_thread(struct thread *) UNUSED;

static void *alloc_frame(struct thread *, size_t size);

static void schedule(void);

void thread_schedule_tail(struct thread *prev);

static tid_t allocate_tid(void);

/* ------------------------------------------------------------------------- */
/* Remove and reinsert the thread into the ready list (used for changing
   priority in priority donation). */
void reinsert_in_ready_list(struct thread *thread) {
  ready_list_remove(&ready_lists, thread);
  ready_list_insert(&ready_lists, thread);
}

/* Return the maximum priority value from the ready list. */
int get_ready_list_maximum_priority(void) {
  return ready_lists.highest_priority;
}

struct thread *get_thread_in_all_list(tid_t thread_id) {
  lock_acquire(&all_list_lock);
  for (struct list_elem *e = list_begin(&all_list);
       e != list_end(&all_list); e = list_next(e)) {
    struct thread *thread = list_entry(e, struct thread, allelem);
    if (thread->tid == thread_id) {
      lock_release(&all_list_lock);
      return thread;
    }
  }

  lock_release(&all_list_lock);
  return NULL;
};



/* ------------------------------------------------------------------------- */


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init(void) {
  ASSERT (intr_get_level() == INTR_OFF);
  lock_init(&tid_lock);

  for (int i = 0; i < 64; i++) {
    ready_lists.queues[i] = priority_queues[i];
  }
  ready_list_init(&ready_lists);

  list_init(&all_list);
  lock_init(&all_list_lock);

  process_init();
  /* Initialize load_avg to 0 on thread_start */
  load_avg = 0;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT, DEFAULT_NICE,
              DEFAULT_CPU);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start(void) {
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick(void) {
  struct thread *t = thread_current();
  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
  user_ticks++;
#endif
  else {
    kernel_ticks++;
    // Increment current thread's recent_cpu
    t->recent_cpu = FP_INT_ADD(t->recent_cpu, 1);
  }

  int64_t current_ticks = timer_ticks();
  /* Enforce preemption. */

  if (thread_mlfqs) {
    if (current_ticks % TIMER_FREQ == 0) {
      // Every second:
      // (1) update system-wide load-avg value.
      update_load_avg();
      // (2) update every thread's recent-cpu value
      thread_foreach(update_recent_cpu, NULL);
      // (3) update every thread's priority
      thread_foreach(update_priority, NULL);
      thread_preempt();
    } else if (current_ticks % TIME_SLICE == 0) {
      // Every time slice:
      // (1) update current thread priority
      update_priority(t);
      thread_preempt();
    }
  }

  if (++thread_ticks >= TIME_SLICE) {
    intr_yield_on_return();
  }
}

void thread_preempt(void) {
  if (!ready_list_empty(&ready_lists)) {
    if (thread_get_priority() < ready_lists.highest_priority) {
      intr_yield_on_return();
    }
  }
}

/* Prints thread statistics. */
void
thread_print_stats(void) {

  printf("Thread %s : %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
         thread_current()->name, idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create(const char *name, int priority,
              thread_func *function, void *aux) {
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  if (thread_mlfqs) {
    struct thread *t_cur = thread_current();
    int niceness;
    // If main thread, default niceness
    if (t == initial_thread)
      niceness = DEFAULT_NICE;
    else
      niceness = t_cur->niceness;
    init_thread(t, name, priority, niceness, t_cur->recent_cpu);
  } else {
    init_thread(t, name, priority, DEFAULT_NICE, DEFAULT_CPU);

  }
  tid = t->tid = allocate_tid();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack'
     member cannot be observed. */
  old_level = intr_disable();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add this new thread to it's parent's (thread_current) list of children */
  struct child_thread *child_thread = malloc(sizeof(child_thread));
  if (child_thread == NULL) {
    PANIC ("malloc failure: src/threads/thread.c -> thread_create");
  }

  child_thread->tid = t->tid;
  child_thread->exit_sema = &t->exit_sema;
  list_push_back(&thread_current()->children, &child_thread->child_elem);
  thread_current();

  intr_set_level(old_level);

  /* Add to run queue. */
  thread_unblock(t);

  /* Checks whether the created thread is higher priority than the current and
   yields the current thread if necessary.  */
  if (thread_get_priority() < priority) {
    thread_yield();
  }
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block(void) {
  ASSERT (!intr_context());
  ASSERT (intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;

  schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock(struct thread *t) {
  enum intr_level old_level;

  ASSERT (is_thread(t));

  old_level = intr_disable();
  ASSERT (t->status == THREAD_BLOCKED);
  t->status = THREAD_READY;
  ready_list_insert(&ready_lists, t);
  intr_set_level(old_level);
}


/* Returns the name of the running thread. */
const char *
thread_name(void) {
  return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void) {
  struct thread *t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread(t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid(void) {
  return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit(void) {
  ASSERT (!intr_context());


#ifdef USERPROG
  thread_close_all_files();
  process_exit ();
#endif
  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable();

  //Ups exit_sema so that parent process no longer waits on this thread
  sema_up(&thread_current()->exit_sema);
  list_remove(&thread_current()->allelem);
  struct list *list_children = &thread_current()->children;
  struct list *list_donation = &thread_current()->donations;
  struct child_thread *child = NULL;

  // Free all donation records
  for (struct list_elem *e = list_begin(list_donation);
       e != list_end(list_donation);) {
    struct donation *donation =
      list_entry(e, struct donation, elem);
    e = list_next(e);
    free(donation);
  }

  // Free all the child entries and the exit status tree entries
  for (struct list_elem *e = list_begin(list_children);
       e != list_end(list_children);) {
    struct child_thread *child_thread =
      list_entry(e, struct child_thread, child_elem);
    e = list_next(e);
    remove_exit_status(child_thread->tid);
    free(child_thread);
  }

  // Close all files that this thread has open for lazy loading.
  file_close(thread_current()->loaded_file);

  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield(void) {
  struct thread *cur = thread_current();
  enum intr_level old_level;

  ASSERT (!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread) {
    ready_list_insert(&ready_lists, cur);
  }
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach(thread_action_func *func, void *aux) {
  struct list_elem *e;

  ASSERT (intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list);
       e = list_next(e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    if (t != idle_thread)
      func(t, aux);
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. Unused in MLFQS.*/
void
thread_set_priority(int new_priority) {
  thread_current()->priority = new_priority;

  if (!ready_list_empty(&ready_lists)) {
    struct thread *t = ready_list_peek_top(&ready_lists);
    if (new_priority < thread_get_donated_priority(t)) {
      thread_yield();
    }
  }

}

/* Returns the given thread's priority. Unused in MLFQS.*/
int
thread_get_donated_priority(const struct thread *t) {
  enum intr_level old_level = intr_disable();
  int max_donation = t->priority;

  for (struct list_elem *e = list_begin(&t->donations);
       e != list_end(&t->donations); e = list_next(e)) {
    int other_priority =
      thread_get_donated_priority(
        list_entry(e, struct donation, elem)->from_thread);
    if (other_priority > max_donation) {
      max_donation = other_priority;
    }
  }

  intr_set_level(old_level);

  return max_donation;
}


/* Returns the current thread's priority. Unused in MLFQS.*/
int
thread_get_priority(void) {
  if (!thread_mlfqs) {
    return thread_get_donated_priority(thread_current());
  } else {
    return thread_current()->priority;
  }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice(int nice) {
  struct thread *t = thread_current();
  enum intr_level old_level = intr_disable();
  thread_current()->niceness = nice;
  update_recent_cpu(t);
  update_priority(t);
  thread_yield();
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice(void) {
  return thread_current()->niceness;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg(void) {
  return FP_TO_INT_NEAREST(100 * load_avg);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu(void) {
  return FP_TO_INT_NEAREST(100 * (thread_current()->recent_cpu));
}

void
update_priority(struct thread *t) {
  int new_priority =
    PRI_MAX - FP_TO_INT_NEAREST(t->recent_cpu / 4) - t->niceness * 2;

  /* Makes sure that thread priority is in the interval
   * [PRI_MIN, PRI_MAX] */
  if (new_priority > PRI_MAX) {
    new_priority = PRI_MAX;
  } else if (new_priority < PRI_MIN) {
    new_priority = PRI_MIN;
  }

  t->priority = new_priority;

  //If in ready_list, remove and reinsert
  if (t->status == THREAD_READY) {
    ready_list_remove(&ready_lists, t);
    ready_list_insert(&ready_lists, t);
  }
}

void
update_load_avg(void) {
  int ready_threads = ready_list_get_size(&ready_lists)
                      + (thread_current() != idle_thread);
  load_avg = ((59 * load_avg) / 60)
             + (INT_TO_FP(ready_threads) / 60);
}

void update_recent_cpu(struct thread *t) {
  fp_t double_load_avg = 2 * load_avg;

  fp_t coefficient = FP_DIV(double_load_avg, FP_INT_ADD(double_load_avg, 1));
  t->recent_cpu = FP_INT_ADD(FP_MULT(coefficient, t->recent_cpu), t->niceness);
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED) {
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;) {
    /* Let someone else run. */
    intr_disable();
    thread_block();

    /* Re-enable interrupts and wait for the next one.

       The `sti' instruction disables interrupts until the
       completion of the next instruction, so these two
       instructions are executed atomically.  This atomicity is
       important; otherwise, an interrupt could be handled
       between re-enabling interrupts and waiting for the next
       one to occur, wasting as much as one clock tick worth of
       time.

       See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
       7.11.1 "HLT Instruction". */
    asm volatile ("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux) {
  ASSERT (function != NULL);

  intr_enable();       /* The scheduler runs with interrupts off. */
  function(aux);       /* Execute the thread function. */
  thread_exit();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread(void) {
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread(struct thread *t) {
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority, int niceness,
            fp_t recent_cpu) {
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->highest_fd = DEFAULT_FD;
  t->highest_mmap_id = 1;
  t->magic = THREAD_MAGIC;
  t->num_stack_pages = 1;
  // Initialise the list of processes created by this process
  list_init(&t->children);

  /* Initialise semaphores used to wait processes which monitor the exit status
     and load status respectively */
  sema_init(&t->exit_sema, 0);
  sema_init(&t->load_sema, 0);

  if (!thread_mlfqs) {
    list_init(&t->donations);
    t->priority = priority;
  } else {
    t->niceness = niceness;
    t->recent_cpu = recent_cpu;
    t->priority = PRI_DEFAULT;
  }
  old_level = intr_disable();
  list_push_back(&all_list, &t->allelem);
  intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame(struct thread *t, size_t size) {
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread(t));
  ASSERT (size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void) {
  if (ready_list_empty(&ready_lists)) {
    return idle_thread;
  } else {
    return ready_list_pop_top(&ready_lists);
  }
}


/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail(struct thread *prev) {
  struct thread *cur = running_thread();

  ASSERT (intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING &&
      prev != initial_thread) {
    ASSERT (prev != cur);
    palloc_free_page(prev);
  }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule(void) {
  struct thread *cur = running_thread();
  struct thread *next = next_thread_to_run();
  struct thread *prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));
  if (cur != next) {
    prev = switch_threads(cur, next);
  }
  thread_schedule_tail(prev);
}

/**
 *
 * @param t - thread to put file to
 * @param _file - file to put
 * @return file-descriptor associated with the added file
 */
int thread_put_file(struct thread *t, struct file *_file) {
  t->fds[t->highest_fd - DEFAULT_FD] = _file;
  return t->highest_fd++;
}

/**
 *
 * @param t - thread to search
 * @param fd - file-descriptor to search
 * @return file-struct of file-descriptor fd in thread t,
 *          if does not exist, return NULL.
 */
struct file *thread_get_file(struct thread *t, int fd) {
  if (fd - DEFAULT_FD <= t->highest_fd && fd >= 0) {
    return t->fds[fd - DEFAULT_FD];
  }
  return NULL;
}

/**
 *
 * @param _file - file to close
 * @param fd - fd associated with the file
 */
void thread_close_file(struct file *_file, int fd) {
  file_close(_file);
  thread_current()->fds[fd - DEFAULT_FD] = NULL;
}

/**
 * close all file descriptors and files
 */
void thread_close_all_files() {
  mmap_munmap_all(thread_current());
  //iterate through whole file descriptor table
  for (int i = 0; i < thread_current()->highest_fd - DEFAULT_FD; i++) {
    //if not already closed, close the file at index i
    if (thread_current()->fds[i] != NULL) {
      file_close(thread_current()->fds[i]);
      thread_current()->fds[i] = NULL;
    }
  }
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void) {
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */

uint32_t thread_stack_ofs = offsetof (struct thread, stack);
