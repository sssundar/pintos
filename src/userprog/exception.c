#include <inttypes.h>
#include <stdio.h>
#include <bitmap.h>
#include <hash.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "lib/string.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

/*! The (file) system lock from syscall.c. */
extern struct lock sys_lock;

/*! Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);
static void debug_helper(void *fault_addr, bool not_present,
		bool write, bool user) UNUSED;

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
    bool write UNUSED; /* True: access was write, false: access was read. */
    bool user UNUSED;  /* True: access by user, false: access by kernel. */
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

    //printf("==> faulting address=%p\n", fault_addr);

    if (!is_user_vaddr(fault_addr)) {

    	// printf("==> why am i dying here??? faulting addr is %p\n", fault_addr);

    	//PANIC("Died here lol\n");
    	exit(-1);
    }

    pg_lock_pd();
    struct thread *t = thread_current();
    // Get the supplemental page corresponding to the faulting address.
    struct spgtbl_elem *s =
        (struct spgtbl_elem *) pagedir_get_page(t->pagedir, fault_addr);

    /* Get the stack pointer. Apply a heuristic to see if this address faulted
      because we need to allocate a new stack page. If so then allocate a
      new page for the stack and install it.
      
      Check whether the page above me (up till PHYS_BASE) is also a stack 
      page. If it isn't, allocate it as such. E.g. we might have a program
      allocate a giant buffer on the stack and skip a few pages in its
      virtual demand-paged pointer, and we need to makes sure, if syscalls
      fault on them, they show up in the page directory.

      Then exit the page fault handler.
    */
    if (pg_is_valid_stack_addr(fault_addr, f->esp)) {

    	void *base_of_page = (void *)((uint32_t)fault_addr & 0xFFFFF000);

      /* Loop over the pages above us till we reach phys-base or an 
        allocated page (stack is contiguous from there) */
      void *base_of_next_page;
      struct spgtbl_elem* growing_stack;
      base_of_next_page = (void *)( ((uint32_t) base_of_page) + PGSIZE);
      while (base_of_next_page < PHYS_BASE) {
        /* Is it in the page table in some form? */              
        if (pagedir_get_page(t->pagedir, 
          (const void *) base_of_next_page) == NULL) {
          /* If not, set up supplemental page table entry */
          pg_release_pd();
          growing_stack = pg_put(-1, -1, 0, NULL, base_of_next_page, NULL, 
            PGSIZE, true, OTHER_PG, BITMAP_ERROR);                    
          pg_lock_pd();

          if (!pagedir_set_page(t->pagedir, base_of_next_page,
              growing_stack, true, true)) {
            PANIC("Tried to fill in holes in stack, but couldn't.");
            NOT_REACHED();
          }

        } else {
          break;
        }

        base_of_next_page = (void *)( ((uint32_t) base_of_next_page) + PGSIZE);
      } 

      // If there's a supplemental page table entry already then it was
      // swapped to disk. Get it.
      if (s != NULL && s->swap_idx != BITMAP_ERROR) {

        //printf("  --> i am a stack addr and i was swapped to disk\n");

        pg_release_pd();
        void *kpage = fr_alloc_page(
          (void *)(((uint32_t) fault_addr) & 0xFFFFF000),
          OTHER_PG, s->writable, -1, 0);
        pg_lock_pd();
    		
        if (kpage == NULL) {
    			PANIC("Couldn't alloc user page in page fault handler.");
    			NOT_REACHED();
    		}

    		//printf("--> right before spget\n");
      	if (!sp_get(s->swap_idx, kpage)) {
      		PANIC("Couldn't get page from swap.");
      		NOT_REACHED();
      	}
      	//printf("--> right after spget\n");
        if (!pagedir_set_page(thread_current()->pagedir, base_of_page,
            kpage, true, false)) {
          PANIC("Couldn't associate a swapped in stack page.");
          NOT_REACHED();
        }
  			fr_unpin(kpage);
  			pg_release_pd();

  			//printf("--> done with page fault handler too!\n\n");

	    	return;
      } 
      else {
      	/* if s!=NULL We have a supplemental page table entry but no swap slot 
          for a stack page, therefore it was an inferred extension.
          Alternatively, if s==NULL we're just extending the stack normally.
          */      	      
        	
  			pg_release_pd();
  			void *kpage = fr_alloc_page(base_of_page, OTHER_PG, true, -1, 0);
  			pg_lock_pd();
  			if (!pagedir_set_page(thread_current()->pagedir, base_of_page,
  					kpage, true, false)) {
  				PANIC("Couldn't install a new stack page.");
  				NOT_REACHED();
  			}
  			else {
  				fr_unpin(kpage);
  				pg_release_pd();
  				return;
  			}  			        
      }
    }


    /* Not Stack Address */
    // If magic is missing then we couldn't find the supplemental page table
    // entry. Exit, since there's nothing else we can do.
    if (s == NULL || s->magic != PG_MAGIC) {

    	// TODO REMOVE
    	//PANIC("Died here megalol\n");

    	pg_release_pd();
    	exit(-1);
    }

    if (not_present) {
    	pg_release_pd();
    	void *kpage = fr_alloc_page(
    			(void *)(((uint32_t) fault_addr) & 0xFFFFF000),
    			s->type, s->writable, s->type == MMAPD_FILE_PG ? s->mid : -1,
    			s->trailing_zeroes);
    	pg_lock_pd();

		if (kpage == NULL) {
			PANIC("Couldn't alloc user page in page fault handler.");
			NOT_REACHED();
		}

		/*
		switch(s->type) {
		case EXECD_FILE_PG:
		case MMAPD_FILE_PG:
			printf("I'm exec'd or mmapped\n");
			break;
		case OTHER_PG:
			printf("I'm other page\n");
			break;
		case ZERO_PG:
			printf("I'm zero page\n");
			break;
		default:
			printf("i'm ugly");
			break;
		}
		*/

		// If this address's page was swapped to disk then read it from there.
		if (s->swap_idx != BITMAP_ERROR) { // TODO comment initially...
	    	if (!sp_get(s->swap_idx, kpage)) {
	    		PANIC("Couldn't get page from swap.");
	    		NOT_REACHED();
	    	}
		}


		// Handle the page based on its type.
		else if (s->type == EXECD_FILE_PG || s->type == MMAPD_FILE_PG) {

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
		}
		else if (s->type == ZERO_PG) {
			memset(kpage, 0, PGSIZE);
		}
		else if (s->type == OTHER_PG) {
			// TODO
		}
		else {
			PANIC("Impossible page type.");
			NOT_REACHED();
		}

		// Overwrite the old page table entry.
		if (!pagedir_set_page(
				t->pagedir, s->vaddr, kpage, s->writable, false)) {
			PANIC("Couldn't install page in page fault handler.");
			NOT_REACHED();
		}

		fr_unpin(kpage);
    }

    //printf("--> we're done with page fault handler\n\n");

    pg_release_pd();
    // debug_helper(fault_addr, not_present, write, user);
}

