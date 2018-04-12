#include "userprog/process.h"
#include <inttypes.h>
#include <round.h>
#include <string.h>
#include <vm/frame.h>
#include <vm/page.h>
#include <vm/mmap.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

//Magic numbers
#define PROCESS_WAIT_FAIL -1
#define MAX_NUM_ARGUMENTS 64

struct lock filesys_lock;            /* A lock for the file system */
struct synch_tree exited_tree;       /* A tree containing the exit statuses of exitted threads */
struct synch_tree loaded_tree;       /* A tree containing the load statuses of loaded threads */

//helper methods
static void **parse_and_copy_arguments(const char *_arguments);

static void start_process(void *args);

/* Initialise the trees to be used by processes and the lock used by the file
   system. */
void process_init(void) {
  synch_tree_init(&exited_tree);
  synch_tree_init(&loaded_tree);
  lock_init(&filesys_lock);
}

/* Acquire the file system lock. */
void process_filesys_lock_acquire(void) {
  lock_acquire(&filesys_lock);
}

/* Release the file system lock. */
void process_filesys_lock_release(void) {
  //this method will get called in every exit call, thus only release lock
  //if someone is holding a lock
  if (filesys_lock.holder != NULL) {
    ASSERT(filesys_lock.holder == thread_current());
    lock_release(&filesys_lock);
  }
}

/* Add an exit status for thread with id == tid to the exit statuses tree. */
void add_exit_status(int tid, int exit_status) {
  add_status(&exited_tree, tid, exit_status);
}

/* Try remove exit status for tid when the value is no longer of any use */
void remove_exit_status(int tid) {
  try_remove_status(&exited_tree, tid);
}

int get_exit_status(int tid) {
  return get_status(&exited_tree, tid);
};

/* Return true if the exit status was found within the exit status tree. A
   side effect of this method is that the value stored at the 'exit_status'
   memory location gets set to the found exit status. */
bool did_get_exit_status(int tid, int *exit_status) {
  return did_get_status(&exited_tree, tid, exit_status);
};

/* Add a load status for the thread with id == tid to the load statuses tree. */
void add_load_status(int tid, int load_status) {
  add_status(&loaded_tree, tid, load_status);
}

/* Return true if the laod status was found within the load status tree. A
   side effect of this method is that the value stored at the 'load_status'
   memory location gets set to the found load status. */
bool did_get_load_status(int tid, int *load_status) {
  return did_get_status(&loaded_tree, tid, load_status);
};

/* Return the value of the load status corresponding to the thread with
   id == tid. */
int get_load_status(int tid) {
  return get_status(&loaded_tree, tid);
};

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute(const char *arguments) {
  /* Ensure the input string is not longer than the page size. */
  if (sizeof(arguments) >= PHYS_BASE) {
    PANIC ("Input is too large.\nInput = %s", arguments);
  }
  /* Parse and make copy of arguments */
  void **args_intermediate = parse_and_copy_arguments(arguments);

  /* Create a new thread to execute FILE_NAME. */
  tid_t tid;
  tid = thread_create(args_intermediate[1], PRI_DEFAULT, start_process,
                      args_intermediate);

  if (tid == TID_ERROR) {
    printf("TID ERROR\n");
    free(args_intermediate);
  }

  return tid;
}

/* Split the input string into its tokens and make a copy of them to the
   returned argument array. */
static void **parse_and_copy_arguments(const char *_arguments) {
  /* Allocate an upper-bound of 64 arguments (max) and an extra entry for the
     number of arguments. */
  int string_size = strlen(_arguments) + 1;
  char *arguments = malloc(string_size * sizeof(char));
  if (arguments == NULL) {
    PANIC (
      "malloc failure: src/userprog/process.c -> parse_and_copy_arguments (1)");
  }

  strlcpy(arguments, _arguments, string_size);
  void **args_intermediate = malloc((MAX_NUM_ARGUMENTS + 1) * sizeof(void *));

  if (args_intermediate == NULL) {
    PANIC (
      "malloc failure: src/userprog/process.c -> parse_and_copy_arguments (2)");
  }

  //parse arguments and put argument[i] in args_intermediate[i]
  int index = 1;
  char *token, *save_ptr;
  int cumulative_size = 0;

  for (token = strtok_r(arguments, " ", &save_ptr); token != NULL;
       token = strtok_r(NULL, " ", &save_ptr)) {
    int string_size = strlen(token) + 1;
    cumulative_size += string_size;

    args_intermediate[index] = malloc(string_size);
    if (args_intermediate[index] == NULL) {
      PANIC (
        "malloc failure: src/userprog/process.c -> parse_and_copy_arguments (3)");
    }


    strlcpy(args_intermediate[index++], token, strlen(token) + 1);
  }

  // Make index reflect the number of args (inialised to 1)
  index--;
  int *index_ptr = malloc(sizeof(int));
  if (index_ptr == NULL) {
    PANIC (
      "malloc failure: src/userprog/process.c -> parse_and_copy_arguments (4)");
  }

  // Check we have enough space on the page to copy all the arguments, their
  // pointers and the additional stack data
  int size_of_pointer_to_args = sizeof(void *);
  int size_of_sentinel = sizeof(void *);
  int size_of_return_address = sizeof(void *);
  int size_of_arg_pointers = index * sizeof(char *); // including argc
  int total_stack_setup_size = size_of_pointer_to_args + size_of_sentinel
                               + size_of_return_address +
                               size_of_arg_pointers + cumulative_size;
  if (total_stack_setup_size >= PHYS_BASE) {
    PANIC ("Not enough stack space.");
  }


  *index_ptr = index;
  args_intermediate[0] = (void *) (index_ptr);

  free(arguments);
  return args_intermediate;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process(void *args) {
  void **args_list = args;
  int *argc_p = ((int *) args_list[0]);
  int argc = *argc_p;
  free(argc_p);
  char *file_name = args_list[1];
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load(file_name, &if_.eip, &if_.esp);

  add_load_status(thread_current()->tid, success);
  // Thread waiting on load to complete can now check the status
  sema_up(&thread_current()->load_sema);

  /* If load failed, quit. */
  if (!success) {
    printf("%s: exit(-1)\n", thread_current()->name);
    // Free args
    for (int i = 1; i <= argc; i++) {
      free(args_list[i]);
    }
    free(args_list);

    add_exit_status(thread_current()->tid, PROCESS_WAIT_FAIL);
    thread_exit();
  }

  //deny writes to executables
  process_filesys_lock_acquire();
  struct file *opened_file = filesys_open(file_name);
  process_filesys_lock_release();
  thread_current()->loaded_file = opened_file;
  file_deny_write(opened_file);

  char **word_pointers = malloc(sizeof(char *) * (argc));
  if (word_pointers == NULL) {
    PANIC ("malloc failure: src/userprog/process.c -> start_process");
  }

  /* Copy arguments to page in reverse order.  */
  for (int i = argc; i >= 1; i--) {
    int string_len = strlen(args_list[i]) + 1;
    if_.esp -= string_len;
    //Push args_list[i]
    strlcpy(if_.esp, args_list[i], string_len);
    //Store a pointer to it
    word_pointers[argc - i] = if_.esp;
    free(args_list[i]);
  }

  /*word alignment*/
  uint32_t stack = if_.esp;
  uint32_t word_alignment_bytes = stack % 4;
  if_.esp -= word_alignment_bytes;
  memset(if_.esp, 0, word_alignment_bytes);

  /* Push sentinel.  */
  int sentinel_num_bytes = 4;
  if_.esp -= sentinel_num_bytes;
  memset(if_.esp, 0, sentinel_num_bytes);

  /* Push pointers to string args.  */
  for (int i = 0; i < argc; i++) {
    if_.esp -= sizeof(char *);
    memcpy(if_.esp, &word_pointers[i], sizeof(char *));
  }
  free(word_pointers);

  /* Push pointer to first arg pointer.  */
  void *argv = if_.esp;
  if_.esp -= sizeof(void *);
  memcpy(if_.esp, &argv, sizeof(char *));

  /* Push number of args (argc).  */
  if_.esp -= sizeof(void *);
  memcpy(if_.esp, &argc, sizeof(char *));

  /* Push fake return address.  */
  if_.esp -= sizeof(void *);
  memset(if_.esp, 0, sizeof(void *));

  free(args_list);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait(tid_t child_tid) {
  // Search current thread's children for the child thread with id == child_tid
  struct list *list_children = &thread_current()->children;
  struct child_thread *child = NULL;

  for (struct list_elem *e = list_begin(list_children);
       e != list_end(list_children); e = list_next(e)) {
    struct child_thread *thread =
      list_entry(e, struct child_thread, child_elem);
    if (thread->tid == child_tid) {
      child = thread;
      break;
    }
  }

  if (child == NULL) {
    // child_tid is not a child of current thread or we have already waited for
    // this child before
    return PROCESS_WAIT_FAIL;
  }

  // Remove the child from list of children as it can no longer be waited for
  list_remove(&child->child_elem);

  struct semaphore *exit_sema = child->exit_sema;
  free(child);

  int child_exit_status;

  // Disable interrupts to prevent the thread we are waiting for from changing
  // state from running to exitted after we have checked whether it has exitted but
  // before we attempt to down its semaphore
  enum intr_level old_level = intr_disable();

  /* Search the exited threads tree for the thread with child_tid to see if it
     has already exited using exit()*/
  if (!did_get_exit_status(child_tid, &child_exit_status)) {

    if (get_thread_in_all_list(child_tid) == NULL) {
      // Kernel already exitted thread
      child_exit_status = PROCESS_WAIT_FAIL;
    } else {
      /* If arriving here then we need to wait for the child process to terminate
      before attempting to get it's exit status */
      sema_down(exit_sema);

      /* Try to get exit status if we fail to then the thread must have been
         exited by the kernel and therefore did not post its exit status
         therefore we return PROCESS_WAIT_FAIL */
      if (!did_get_exit_status(child_tid, &child_exit_status)) {
        child_exit_status = PROCESS_WAIT_FAIL;
      }
    }
  }

  intr_set_level(old_level);
  return child_exit_status;
}

/* Free the current process's resources. */
void
process_exit(void) {
  struct thread *cur = thread_current();
  uint32_t *pd;

  //allow executed file to be written to again
  if (cur->loaded_file != NULL) {
    file_allow_write(cur->loaded_file);
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  //free frames
  struct list frames;
  list_init(&frames);
  frame_remove_all(thread_current(),&frames);
  frame_free_all(&frames);

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate(void) {
  struct thread *t = thread_current();

  /* Activate thread's page tables. */
  pagedir_activate(t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printrf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack(void **esp);

static bool validate_segment(const struct Elf32_Phdr *, struct file *);

static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load(const char *file_name, void (**eip)(void), void **esp) {
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create();
  if (!hash_init(&t->spt, spt_hash, spt_less, NULL)){
    PANIC("Supplementary Page Table init failed");
  }
  if(!hash_init(&t->mmap,mmap_hash,mmap_less,NULL)) {
    PANIC("Memory Map Table init failed");
  }

  if (t->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  process_filesys_lock_acquire();
  file = filesys_open(file_name);
  process_filesys_lock_release();

  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof(struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  thread_current()->loaded_file = file;

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
               Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                          - read_bytes);
          } else {
            /* Entirely zero.
               Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void *) mem_page,
                            read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void)) ehdr.e_entry;

  success = true;

  done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
             uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs(upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  off_t file_offset = ofs;

  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */

    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    spt_add_new_elem(&thread_current()->spt, upage, UNLOADED_EXECUTABLE, writable);

    if (read_bytes == 0) {
      spt_set_zero_page(&thread_current()->spt, upage);
    }

    spt_set_executable(&thread_current()->spt, upage, file_offset, (uint32_t) page_read_bytes);

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    file_offset += page_read_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp) {
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = process_install_page(((uint8_t *) PHYS_BASE) - PGSIZE, kpage,
                                   true, STACK);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
process_install_page(void *upage, void *kpage, bool writable,enum page_state state) {
  struct thread *t = thread_current();

  ASSERT(pg_ofs(kpage) == 0);

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  if (pagedir_get_page(t->pagedir, upage) == NULL
      && pagedir_set_page(t->pagedir, upage, kpage, writable)) {

    //add to frame table
    frame_add(upage, kpage);

    //add to supp page table
    spt_add_new_elem(&thread_current()->spt, upage,state,writable);
    spt_set_stack(&thread_current()->spt, upage);
    return true;
  } else {
    return false;
  }
}
