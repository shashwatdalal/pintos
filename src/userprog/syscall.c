#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/vaddr.h>
#include <devices/shutdown.h>
#include <filesys/filesys.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <lib/user/syscall.h>
#include "threads/interrupt.h"
#include <threads/thread.h>
#include <vm/mmap.h>
#include <vm/swap.h>
#include <threads/palloc.h>
#include "pagedir.h"
#include "process.h"

/** Macros for magic numbers **/
#define WORD_SIZE 4
#define FILE_ERROR -1
#define PROCESS_EXEC_FAIL -1
#define PROCESS_WAIT_FAIL -1
#define FIRST_FILESYS_SYSCALL 4
#define LAST_FILESYS_SYSCALL 12

/**
 * Macros for extracting arguments from stack
 * based on number of arguments and type of args
 **/
#define syscall1(FUNCTION, TYPE1)                   \
    ({if(is_valid_pointer(f->esp + WORD_SIZE)) {    \
    FUNCTION((*(TYPE1 *) (f->esp + WORD_SIZE)),f);  \
    }})

#define syscall2(FUNCTION, TYPE1, TYPE2)            \
    ({if(is_valid_memory_block(f->esp + WORD_SIZE,  \
                                    2*WORD_SIZE)) { \
    FUNCTION(*(TYPE1 *) (f->esp + WORD_SIZE),       \
            (*(TYPE2 *) (f->esp + 2*WORD_SIZE)),f); \
    }})

#define syscall3(FUNCTION, TYPE1, TYPE2, TYPE3)     \
    ({if(is_valid_memory_block(f->esp + WORD_SIZE,  \
                                    3*WORD_SIZE)) { \
    FUNCTION(*(TYPE1 *) (f->esp + WORD_SIZE),       \
            (*(TYPE2 *) (f->esp + 2*WORD_SIZE)),    \
            (*(TYPE3 *) (f->esp + 3*WORD_SIZE)),f); \
    }})

/******************* Helper Method Prototypes *******************/

static void sys_halt(struct intr_frame *f) NO_RETURN;

static void sys_exit(int status, struct intr_frame *f) NO_RETURN;

static void sys_exec(const char *file, struct intr_frame *f);

static void sys_wait(pid_t, struct intr_frame *f);

static void
sys_create(const char *file, unsigned initial_size, struct intr_frame *f);

static void sys_remove(const char *file, struct intr_frame *f);

static void sys_open(const char *file, struct intr_frame *f);

static void sys_filesize(int fd, struct intr_frame *f);

static void
sys_read(int fd, void *buffer, unsigned length, struct intr_frame *f);

static void
sys_write(int fd, const void *buffer, unsigned length, struct intr_frame *f);

static void sys_seek(int fd, unsigned position, struct intr_frame *f);

static void sys_tell(int fd, struct intr_frame *f);

static void sys_close(int fd, struct intr_frame *f);

static void sys_mmap(int fd, void *uvaddr, struct intr_frame *f);

static void sys_munmap(int mapid, struct intr_frame *f);


static void syscall_handler(struct intr_frame *f UNUSED);

static bool is_valid_pointer(uint8_t *pointer);

static bool is_valid_fd(int fd);

static bool is_non_std_file(int fd);

static bool is_filesys_syscall(int syscall);

static bool is_valid_memory_block(void *star_addr, unsigned size);

/*********************** Helper Predicates ***********************/

/**
 * Gets called when system is put into an invalid state
 * and as a result, exits with status -1
 **/
bool invalid_state() {
  printf("%s: exit(-1)\n", thread_current()->name);
//  PANIC("INA");
  //release lock
  process_filesys_lock_release();
  //add_exit_status(thread_current()->tid, PROCESS_WAIT_FAIL);
  thread_exit();
  NOT_REACHED();
  return false;
}

/**
 *
 * @param fd - file-descriptor that is being checked
 * @return whether the given file-descriptor exists in the current
 *          thread's file-descriptor table
 */
static bool is_valid_fd(int fd) {
  if (fd < thread_current()->highest_fd &&
      thread_current()->fds[fd - DEFAULT_FD] != NULL) {
    return true;
  } else {
    return invalid_state();
  }
}

/**
 *
 * @param fd - file-descriptor that is being checked
 * @return checks if file-descriptor is not standard input/output
 */
static bool is_non_std_file(int fd) { return fd >= DEFAULT_FD; }

/**
 *
 * @param sycall - syscall number currently being handled
 * @return whether syscall deals with the file-system
 */
static bool is_filesys_syscall(int sycall) {
  return FIRST_FILESYS_SYSCALL <= sycall && sycall <= LAST_FILESYS_SYSCALL;
}

/**
 *
 * @param pointer - pointer to be validated
 * @returns true if the given pointer exists in a mapped page and if
 *          the pointer does not overflow into kernel space.
 */
static bool is_valid_pointer(uint8_t *pointer) {
  return is_valid_memory_block(pointer, WORD_SIZE);
}

/**
 *
 * @param star_addr - start address of memory block
 * @param size of memory block
 * @returns true if all contiguous pages in block are mapped and
 *          if they do not over-flow into kernel memory
 */
static bool is_valid_memory_block(void *star_addr, unsigned size) {
/**
   *    |<--------BLOCK------->|
   *                1. check if these two segments overlap
   *                       |<---- KERNEL MEMORY---->|
   * +-------+-------+-------+-------+
   * |   p1  |   p2  |   p3  |   p4  |   Virtual Memory
   * +-------+-------+-------+-------+
   *    |<------------------>| 2.  check every full page is mapped
   *                          |<->| 3. if extra boundary cross, check last byte
   *                                    is mapped
   */
  // (1) check if block is overflowing into kernel memory or is a null pointer
  if (!is_user_vaddr(star_addr + size - 1) || star_addr == NULL) {
    return invalid_state();
  }
  uint32_t *pagedir = thread_current()->pagedir;
  unsigned i = star_addr;
  unsigned end_addr = star_addr + size;
  //extra boundary cross defined as:
  bool extra_boundary_cross = (unsigned) pg_ofs(star_addr) >
                              pg_ofs(end_addr);

  // (2) check every full page in memory block is mapped
  while (i < end_addr - (extra_boundary_cross) * pg_ofs(end_addr)) {
    if (spt_is_page_mapped(&thread_current()->spt, i) == NULL) {
      return invalid_state();
    }
    i += PGSIZE;
  }

  //(3) check final byte is mapped when there is an extra_boundary_cross
  if (extra_boundary_cross) {
    if (spt_is_page_mapped(&thread_current()->spt, end_addr) == NULL) {
      return invalid_state();
    }
  }
  return true;
}

/********************* System Call Dispatcher *********************/


void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f UNUSED) {
  if (is_valid_pointer(f->esp)) {
    int syscall_number = *(int *) f->esp;
    //acquire filesys lock
    if (is_filesys_syscall(syscall_number)) {
      process_filesys_lock_acquire();
    }
    switch (syscall_number) {
      case SYS_HALT:
        sys_halt(f);
        break;
      case SYS_EXIT:
        syscall1(sys_exit, int);
        break;
      case SYS_EXEC:
        syscall1(sys_exec, const char*);
        break;
      case SYS_WAIT:
        syscall1(sys_wait, pid_t);
        break;
      case SYS_CREATE:
        syscall2(sys_create, const char*, unsigned);
        break;
      case SYS_REMOVE:
        syscall1(sys_remove, const char*);
        break;
      case SYS_OPEN:
        syscall1(sys_open, const char*);
        break;
      case SYS_FILESIZE:
        syscall1(sys_filesize, int);
        break;
      case SYS_READ:
        syscall3(sys_read, int, void*, unsigned);
        break;
      case SYS_WRITE:
        syscall3(sys_write, int, const void*, unsigned);
        break;
      case SYS_SEEK:
        syscall2(sys_seek, int, unsigned);
        break;
      case SYS_TELL:
        syscall1(sys_tell, int);
        break;
      case SYS_CLOSE:
        syscall1(sys_close, int);
        break;
      case SYS_MMAP:
        syscall2(sys_mmap, int, void *);
        break;
      case SYS_MUNMAP:
        syscall1(sys_munmap, mapid_t);
        break;
      default:
        //unimplemented system calls
        add_exit_status(thread_current()->tid, PROCESS_WAIT_FAIL);
        thread_exit();
    }
    //release filesys lock
    if (is_filesys_syscall(syscall_number)) {
      process_filesys_lock_release();
    }
  }
}

/************** IMPLEMENTATIONS OF SYSTEM CALLS **************/

static void sys_halt(struct intr_frame *f UNUSED) {
  shutdown_power_off();
  NOT_REACHED();
}

/* Handle the exit system call by exitting the current thread and recording
   the exit status of this thread in the exit statuses tree. */
static void sys_exit(int status, struct intr_frame *f UNUSED) {
  add_exit_status(thread_current()->tid, status);
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}

/* Handle the exec system call by running the file given. */
static void sys_exec(const char *file, struct intr_frame *f) {
  if (is_valid_pointer(file)) {
    f->eax = process_execute(file);

    if (f->eax == TID_ERROR) {
      f->eax = PROCESS_EXEC_FAIL;
      return;
    }

    // Disable interrupts to avoid process created altering state of all_list
    enum intr_level old_level = intr_disable();
    struct thread *child = get_thread_in_all_list(f->eax);
    if (child == NULL) {
      /* In this case the process created terminated before process_execute
         returned to check load status */
      if (!get_load_status(f->eax)) {
        f->eax = PROCESS_EXEC_FAIL;
      }
    } else {
      /* Child was in the all_list so we need to wait for the load to complete to
         check status */
      sema_down(&child->load_sema);
      if (!get_load_status(f->eax)) {
        f->eax = PROCESS_EXEC_FAIL;
      }
    }
    intr_set_level(old_level);
  }
}

/* Handle the wait system call by passing on the responsibility to process_wait
   (see process_wait for more detail). */
static void sys_wait(pid_t thread_id, struct intr_frame *f) {
  f->eax = process_wait(thread_id);
}

static void
sys_create(const char *file, unsigned initial_size, struct intr_frame *f) {
  f->eax = is_valid_pointer(file) && filesys_create(file, initial_size);
}

static void sys_remove(const char *file, struct intr_frame *f) {
  /**
   * removing a file does not close it, and can be closed regardless
   * of whether is already open or not.
   **/
  f->eax = is_valid_pointer(file) && filesys_remove(file);
}

static void sys_open(const char *file, struct intr_frame *f) {
  if (is_valid_pointer(file)) {
    ASSERT(file != NULL);
    struct file *file_struct = filesys_open(file);
    f->eax = (file_struct == NULL) ? FILE_ERROR :
             thread_put_file(thread_current(), file_struct);
  }
}

static void sys_filesize(int fd, struct intr_frame *f) {
  struct file *current_file = thread_get_file(thread_current(), fd);
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    f->eax = file_length(current_file);
  }
}

static void sys_read(int fd, void *buffer, unsigned length,
                     struct intr_frame *f) {
  struct file *current_file = thread_get_file(thread_current(), fd);
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    //read regular file
    if (is_valid_memory_block(buffer, length)) {
      f->eax = file_read(current_file, buffer, length);
    }
  } else if (fd == STDIN_FILENO) {
    //read to standard input
    f->eax = input_getc();
  } else {
    invalid_state();
  }
}

static void sys_write(int fd, const void *buffer, unsigned length,
                      struct intr_frame *f) {
  if (is_valid_memory_block(buffer, length)) {
    if (is_non_std_file(fd) && is_valid_fd(fd)) {
      // write to regular file
      struct file *current_file = thread_get_file(thread_current(), fd);
      f->eax = file_write(current_file, buffer, length);
    } else if (fd == STDOUT_FILENO) {
      //write to standard output
      putbuf(buffer, length);
    } else {
      invalid_state();
    }
  } else {
    invalid_state();
  }
}

static void sys_seek(int fd, unsigned position, struct intr_frame *f UNUSED) {
  struct file *current_file = thread_get_file(thread_current(), fd);
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    file_seek(current_file, position);
  } else {
    //should not try to seek standard input/output
    invalid_state();
  }
}

static void sys_tell(int fd, struct intr_frame *f) {
  struct file *current_file = thread_get_file(thread_current(), fd);
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    f->eax = file_tell(current_file);
  } else {
    //should not try to tell standard input/output
    invalid_state();
  }
}

static void sys_close(int fd, struct intr_frame *f UNUSED) {
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    struct file *current_file = thread_get_file(thread_current(), fd);
    thread_close_file(current_file, fd);
  } else {
    //should not try to close standard input/output
    invalid_state();
  }
}

static void sys_mmap(int fd, void *uvaddr, struct intr_frame *f) {
  if (is_non_std_file(fd) && is_valid_fd(fd)) {
    struct thread *cur = thread_current();
    struct file *mapfile = thread_get_file(cur, fd);
    if (file_length(mapfile) == 0 || uvaddr == NULL ||
        (unsigned) uvaddr % PGSIZE != 0) {
      f->eax = -1;
      //return early
      return;
    }


    int new_fd = thread_put_file(thread_current(),
                                 file_reopen(
                                   thread_get_file(thread_current(), fd)));
    int number_of_pages;

    if (file_length(mapfile) % PGSIZE == 0) {
      number_of_pages = (file_length(mapfile) / PGSIZE);
    } else {
      number_of_pages = (file_length(mapfile) / PGSIZE) + 1;
    }

    for (int i = 0; i < number_of_pages; i++) {
      if (spt_find_elem(&thread_current()->spt, uvaddr + PGSIZE * i) != NULL) {
        //return early
        f->eax = -1;
        return;
      }
    }

    // add entry to mmap table
    struct mmap_elem *_mmap = mmap_add(thread_current(), uvaddr,
                                       number_of_pages, new_fd);
    //add entry to frame-table
    for (int i = 0; i < number_of_pages; i++) {
      // set status of files to lazy-load
      spt_add_new_elem(&thread_current()->spt, uvaddr + PGSIZE * i,
                       IN_FILESYS,true);
    spt_set_filesys(&thread_current()->spt, uvaddr + PGSIZE * i,
                      _mmap->mapid);
    }
    f->eax = _mmap->mapid;
  } else {
    invalid_state();
  }
}

static void sys_munmap(int mapid, struct intr_frame *f) {
  mmap_munmap(mapid, thread_current(),true);
}
