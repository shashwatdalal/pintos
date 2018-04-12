#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <threads/vaddr.h>
#include <threads/palloc.h>
#include <vm/frame.h>
#include <vm/swap.h>
#include <devices/block.h>
#include <filesys/file.h>
#include <lib/string.h>
#include <filesys/file.h>
#include <vm/mmap.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "process.h"
#include "syscall.h"
#include "pagedir.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void lazy_load_page(struct file *, void *, void *, uint32_t, bool, off_t, enum page_state);

static void page_fault(struct intr_frame *);
static void handle_lazy_loading_file(void *page_top, void *fault_addr, struct spt_elem *spt_elem);
static void bring_upage_from_swap(void *upage);
static void handle_lazy_loading_exec(void *page_top);

#define OFFSET_MASK 0xfff
#define PAGE_NUM_MASK 0xfffff000
#define STACK_LIMIT 0x8 << 20 // This is 2^20 Bytes
#define MAX_JUMP_SIZE 32

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init(void) {
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int(5, 3, INTR_ON, kill,
                    "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int(7, 0, INTR_ON, kill,
                    "#NM Device Not Available Exception");
  intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int(19, 0, INTR_ON, kill,
                    "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats(void) {
  printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill(struct intr_frame *f) {
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs) {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n",
             thread_name(), f->vec_no, intr_name(f->vec_no));
      intr_dump_frame(f);
      process_filesys_lock_release();
      thread_exit();
      NOT_REACHED();

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC ("Kernel bug - unexpected interrupt in kernel");

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name(f->vec_no), f->cs);
      add_exit_status(thread_current()->tid, PROCESS_WAIT_FAIL);
      thread_exit();
  }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to task 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference".

             Virtual Address format
       31                12 11         0
       +-------------------+-----------+
       | Page Number       | Offset    |
       +-------------------+-----------+
*/

bool
is_valid_stack_jump(void * addr, void *esp) {
  if (addr < esp) {
    // PUSH or PUSHA
    return esp - addr == 0x4 || esp - addr == 0x20;
  } else {
    return addr < PHYS_BASE;
  }
}

static void
page_fault(struct intr_frame *f) {
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  void *fault_page_kvaddr = (uint32_t) fault_addr & PAGE_NUM_MASK;
  void *fault_offset = (uint32_t) fault_addr & OFFSET_MASK;

  void *next_instr = f->eip;
  void *esp = f->esp;

  void *page_top = pg_round_down(fault_page_kvaddr);

  bool page_not_on_disk =
    spt_find_elem(&thread_current()->spt, fault_page_kvaddr) == NULL;

  if (page_not_on_disk && !write) {
    // The thread is attempting to read a page that doesn't exist
    invalid_state();
    NOT_REACHED();
  }


  if (not_present && !page_not_on_disk) {
    struct spt_elem *_spt_elem = spt_find_elem(&thread_current()->spt,
                                               fault_page_kvaddr);
    block_sector_t target_block =
            spt_find_elem(&thread_current()->spt,page_top)->address;
    if (spt_get_state(&thread_current()->spt, page_top) == UNLOADED_EXECUTABLE) {
      handle_lazy_loading_exec(page_top);
      return;
    } else if (_spt_elem->state == IN_SWAP) {
      bring_upage_from_swap(page_top);
    } else if (_spt_elem->state == IN_FILESYS) {
      handle_lazy_loading_file(page_top, fault_addr, _spt_elem);
    }

    return;
  }


  if (write && user) {
    // The user is trying to write to memory it does not have access to
    if (!is_valid_stack_jump(fault_addr, esp)) {
      // Potentially malicious program, shut it down
      invalid_state();

      NOT_REACHED();
    }

    // Check if the stack access is less than the stack size limit
    // (8MB by default) NOTE: This assumes esp is in user space of KVM
    if ((uint32_t) PHYS_BASE - (uint32_t) fault_addr > (uint32_t) STACK_LIMIT) {
      printf("Stack growing too large");
      invalid_state();
    }

    int num_pages_needed =
            ((uint32_t) PHYS_BASE - (uint32_t) page_top) / PGSIZE - thread_current()->num_stack_pages;

    void *new_page = palloc_get_multiple(PAL_USER, num_pages_needed);
    for (int i = 0; i < num_pages_needed; i++) {
      void *page_uaddr = page_top + i * PGSIZE;
      void *page_kaddr = new_page + i * PGSIZE;

      if (spt_find_elem(&thread_current()->spt, page_uaddr) != NULL) {
        PANIC("Stack growth overlapping with mmap'd file.");
      }

      if (!process_install_page(page_uaddr, page_kaddr, true, STACK)) {
        PANIC("Installing page failed : exception.c -> page_fault()");
      }
    }

    thread_current()->num_stack_pages += num_pages_needed;

    return;
    NOT_REACHED();
  }

  bool is_zero_p = spt_is_zero_page(&thread_current()->spt, page_top);
  if (!write && is_zero_p) {
    // Load result
    f->eax = 0;
    // Jump to *next* instruction
    f->esp = f->eip;
    return;
    NOT_REACHED();
  }

  if (user) {
    invalid_state();
  }

  f->eip = f->eax;
  f->eax = 0xffffffff;
}

static
void lazy_load_page(struct file * f, void * page_top, void * kpage,
                    uint32_t read_bytes, bool writable, off_t ofs, enum page_state state) {
  if (pagedir_get_page(thread_current()->pagedir, page_top) == NULL
      && pagedir_set_page(thread_current()->pagedir, page_top, kpage, writable)) {

    off_t actual_read_bytes =
            file_read_at(f, kpage, read_bytes, ofs);

    memset(kpage + actual_read_bytes, 0, (uint32_t) PGSIZE - actual_read_bytes);

    frame_add(page_top, kpage);

    spt_set_state(&thread_current()->spt, page_top, state);

    pagedir_set_dirty(thread_current()->pagedir, page_top, 0);

  } else {
    PANIC("Unable to add page to directory");
  }
}

// Lazy loading executables
static void
handle_lazy_loading_exec(void *page_top) {
  /* Get a page of memory. */
  void *kpage = palloc_get_page(PAL_USER);
  if (kpage == NULL)
    PANIC ("Page allocation failed : expection.c -> page_fault");

  /* Load this page. */
  struct file *file = thread_current()->loaded_file;

  bool writable = spt_get_writable(&thread_current()->spt, page_top);

  uint32_t read_bytes = spt_get_read_bytes(&thread_current()->spt, page_top);

  off_t ofs = spt_get_offset(&thread_current()->spt, page_top);

  lazy_load_page(file,page_top,kpage, read_bytes, writable, ofs, LOADED_EXECUTABLE);

  return;
}

// Bring page from swap
static void
bring_upage_from_swap(void *upage) {
  block_sector_t target_block = spt_get_swap_sector(&thread_current()->spt, upage);

  if (frame_is_full()) {
    evict_and_swap_page(upage, target_block);
  } else {
    void *dst_kvaddr = find_free_user_frame();
    swap_bring_sector_to_frame(dst_kvaddr, target_block);
  }
}

// Lazy loading files
static void
handle_lazy_loading_file(void *page_top, void *fault_addr, struct spt_elem *spt_elem) {
  //find frame to replace
  void *kpage = palloc_get_page(PAL_USER);

  if (kpage == NULL)
    PANIC ("Page allocation failed : expection.c -> page_fault");


  bool writable = spt_get_writable(&thread_current()->spt, page_top);
  struct mmap_elem *_mmap_elem = mmap_get(thread_current(),
                                          spt_elem->address);
  int fd = _mmap_elem->fd;

  struct file *file = thread_get_file(thread_current(), fd);
  int page_number =
          ((unsigned) (pg_round_down(fault_addr) - _mmap_elem->uvaddr) / PGSIZE) + 1;
  uint32_t read_bytes = (uint32_t) (page_number == _mmap_elem->number_of_pages ?
                          file_length(file) % PGSIZE : PGSIZE);
  off_t ofs = (off_t) (page_top - _mmap_elem->uvaddr);

  lazy_load_page(file,page_top, kpage, read_bytes, writable, ofs, MMAP_IN_FRAME);
}
