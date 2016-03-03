#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <hash.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "lib/string.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"

/*! The (file) system lock from syscall.c. */
extern struct lock sys_lock;

/*! Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/*! Registers handlers for interrupts that can be caused by user programs.

    In a real Unix-like OS, most of these interrupts would be passed along to
    the user process in the form of signals, as described in [SV-386] 3-24 and
    3-25, but we don't implement signals.  Instead, we'll make them simply kill
    the user process.

    Page faults are an exception.  Here they are treated the same way as other
    exceptions, but this will need to change to implement virtual memory.

    Refer to [IA32-v3a] section 5.15 "Exception and Interrupt Reference" for a
    description of each of these exceptions. */
void exception_init(void) {
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

/*! Prints exception statistics. */
void exception_print_stats(void) {
    printf("Exception: %lld page faults\n", page_fault_cnt);
}

/*! Handler for an exception (probably) caused by a user process. */
static void kill(struct intr_frame *f) {
    /* This interrupt is one (probably) caused by a user process.
       For example, the process might have tried to access unmapped
       virtual memory (a page fault).  For now, we simply kill the
       user process.  Later, we'll want to handle page faults in
       the kernel.  Real Unix-like operating systems pass most
       exceptions back to the process via signals, but we don't
       implement them. */
     
    /* The interrupt frame's code segment value tells us where the
       exception originated. */
    thread_current()->voluntarily_exited = 0;
    switch (f->cs) {
    case SEL_UCSEG:
        /* User's code segment, so it's a user exception, as we
           expected.  Kill the user process.  */
        printf("%s: dying due to interrupt %#04x (%s).\n",
               thread_name(), f->vec_no, intr_name(f->vec_no));
        intr_dump_frame(f);
        exit(-1);
        break;
    case SEL_KCSEG:
        /* Kernel's code segment, which indicates a kernel bug.
           Kernel code shouldn't throw exceptions.  (Page faults
           may cause kernel exceptions--but they shouldn't arrive
           here.)  Panic the kernel to make the point.  */
        intr_dump_frame(f);        
        PANIC("Kernel bug - unexpected interrupt in kernel"); 
        break;
    default:
        /* Some other code segment?  Shouldn't happen.  Panic the
           kernel. */
        printf("Interrupt %#04x (%s) in unknown segment %04x\n",
               f->vec_no, intr_name(f->vec_no), f->cs);        
        exit(-1);
    }
}

/*! Simple debug message. */
static void debug_helper(void *fault_addr, bool not_present,
		bool write, bool user) {
    printf("Page fault at %p: %s error %s page in %s context.\n",
           fault_addr,
           not_present ? "not present" : "rights violation",
           write ? "writing" : "reading",
           user ? "user" : "kernel");
}

/*! Page fault handler. At entry, the address that faulted is in CR2 (Control
    Register 2) and information about the fault, formatted as described in
    the PF_* macros in exception.h, is in F's error_code member.  The
    example code here shows how to parse that information.  You
    can find more information about both of these in the
    description of "Interrupt 14--Page Fault Exception (#PF)" in
    [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void page_fault(struct intr_frame *f) {
    bool not_present;  /* True: not-present page, false: writing r/o page. */
    bool write;        /* True: access was write, false: access was read. */
    bool user;         /* True: access by user, false: access by kernel. */
    void *fault_addr;  /* Fault address. */    

    /* Obtain faulting address, the virtual address that was accessed to cause
       the fault.  It may point to code or to data.  It is not necessarily the
       address of the instruction that caused the fault (that's f->eip).
       See [IA32-v2a] "MOV--Move to/from Control Registers" and
       [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception (#PF)". */
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

    //------------------------ Virtual memory code ----------------------------

    if (!is_user_vaddr(fault_addr)) {
    	exit(-1);
    }

    pg_lock_pd();

    // Get the stack pointer. Apply a heuristic to see if this address faulted
    // because we need to allocate a new stack page. If so then allocate a
    // new page for the stack and install it, then exit the page fault handler.
    if (pg_is_valid_stack_addr(fault_addr, f->esp)) {
    	void *base_of_page = (void *)((uint32_t)fault_addr & 0xFFFFF000);
    	void *kpage = fr_alloc_page(base_of_page, OTHER_PG);
    	if (!install_page(base_of_page, kpage, true, false)) {
    		PANIC("Couldn't install a new stack page.");
    		NOT_REACHED();
    	}
    	else {
    		fr_unpin(kpage);
    		pg_release_pd();
    		return;
    	}
    }

    // Get the supplemental page corresponding to the faulting address.
    struct thread *t = thread_current();
    struct spgtbl_elem *s =
    		(struct spgtbl_elem *) pagedir_get_page(t->pagedir, fault_addr);

    // If magic is missing then we couldn't find the supplemental page table
    // entry. Exit, since there's nothing else we can do.
    if (s == NULL || s->magic != PG_MAGIC) {
    	pg_release_pd();
    	exit(-1);
    }

    if (not_present) {
    	void *kpage = fr_alloc_page(
    			(void *)(((uint32_t) fault_addr) & 0xFFFFF000),
    			EXECD_FILE_PG);

		if (kpage == NULL) {
			PANIC("Couldn't alloc user page in page fault handler.");
			NOT_REACHED();
		}

		// Handle the page based on its type.
		if (s->type == EXECD_FILE_PG || s->type == MMAPD_FILE_PG) {

			struct file *src = s->src_file;

			// If the file-descriptor isn't -1 we assume it was
			// memory-mapped. Then we must look up info about the file using
			// a syscall.
			if(s->fd != -1) {
				struct list_elem *l_dummy;
				struct mmap_element *m;
				pg_release_pd();
				m = find_matching_mmapped_file(s->mid, &l_dummy);
				if (m == NULL) {
					PANIC("what?");
				}
				pg_lock_pd();
				src = m->file;
			}

			// Seek to correct offset in file and read.
			lock_acquire(&sys_lock);
			file_seek(src, s->offset);

			if (file_read(src, kpage, PGSIZE - s->trailing_zeroes)
					!= (int) (PGSIZE - s->trailing_zeroes)) {
				PANIC("Couldn't read page from file.");
				NOT_REACHED();
			}
			lock_release(&sys_lock);

			memset(kpage + (PGSIZE - s->trailing_zeroes),
					0, s->trailing_zeroes);

			// Overwrite the old page table entry.
			if (!pagedir_set_page(
					t->pagedir, s->vaddr, kpage, s->writable, false)) {
				PANIC("Couldn't install page in page fault handler.");
				NOT_REACHED();
			}

			fr_unpin(kpage);
		}
		else if (s->type == ZERO_PG) {
			memset(kpage, 0, PGSIZE);

			// Overwrite the old page table entry.
			if (!pagedir_set_page(
					t->pagedir, s->vaddr, kpage, s->writable, false)) {
				debug_helper(fault_addr, not_present, write, user);
				PANIC("Couldn't install page in page fault handler.");
				NOT_REACHED();
			}
		}
		else if (s->type == OTHER_PG) {
			// TODO
		}
		else {
			PANIC("Impossible page type.");
			NOT_REACHED();
		}
    }

    pg_release_pd();
    // debug_helper(fault_addr, not_present, write, user);
}

