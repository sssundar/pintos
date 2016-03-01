#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <hash.h>
#include <stdlib.h>
#include "lib/string.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "lib/user/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp);

/*! Lock for the executing_files list. */
struct lock eflock;

/*! Check these before writing to files! */
struct list executing_files;

extern struct lock sys_lock;

/*! Starts a new thread running a user program loaded from FILENAME.  The new
    thread may be scheduled (and may even exit) before process_execute()
    returns.  Returns the new process's thread id, or TID_ERROR if the thread
    cannot be created. */
tid_t process_execute(const char *file_name) {
    char *fn_copy;
    tid_t tid;
    char *progname;
    int i = 0;
    bool found = false;

    /* Make a copy of FILE_NAME.
       Otherwise there's a race between the caller and load(). */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);

    // Find the program name, which is the first token.
    progname = (char *) palloc_get_page(0);
	if (progname == NULL)
		return -1;
	strlcpy(progname, file_name, PGSIZE);
	while (progname[i] != ' ' && progname[i] != '\0') {
		i++;
	}
	progname[i] = '\0';

    // Create a new thread to execute FILE_NAME, and make sure it knows it's
    // our child
    tid = thread_create(progname, PRI_DEFAULT, start_process, fn_copy, 1,
    		&thread_current()->child_list, thread_current());
    // Wait for child to be loaded.
    sema_down(&thread_current()->load_child);

    // Search the list of children for the one with the matching
    // tid. Check its exit status. If bad, return -1. Otherwise return tid.
    struct thread *chld_t;
    struct list_elem *l;
	for (l = list_begin(&thread_current()->child_list); 
			l != list_end(&thread_current()->child_list);
			l = list_next(l)) {
		chld_t = list_entry(l, struct thread, chld_elem);

		if(chld_t->tid == tid) {
			if(!chld_t->loaded) {
				tid = -1;
			}
			found = true;
			break;
		}
	}
	if (!found)
		tid = -1;

    if (tid == TID_ERROR)
        palloc_free_page(fn_copy);

    if (progname != NULL)
    	palloc_free_page((void *) progname);
    return tid; // changed from current_thread()->tid
}

/*! A thread function that loads a user process and starts it running. */
static void start_process(void *file_name_) {
    char *file_name = file_name_;
    struct intr_frame if_;
    bool success;

    /* Initialize interrupt frame and load executable. */
    memset(&if_, 0, sizeof(if_));
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(file_name, &if_.eip, &if_.esp);

    if (!success) { 
    	thread_current()->loaded = false; 
    }
    else {
    	thread_current()->loaded = true; 
    }

    if (thread_current()->parent != NULL) {
		sema_up(&thread_current()->parent->load_child); 
	}

    /* If load failed, quit. */
    if (!success) {
    	if (thread_current()->tfile.filename != NULL) {
    		list_remove(&thread_current()->tfile.f_elem);
    		palloc_free_page((void *) thread_current()->tfile.filename);
    	    thread_current()->tfile.filename = NULL;
    	}
    	thread_exit(); 
    }

    /* Start the user process by simulating a return from an
       interrupt, implemented by intr_exit (in
       threads/intr-stubs.S).  Because intr_exit takes all of its
       arguments on the stack in the form of a `struct intr_frame',
       we just point the stack pointer (%esp) to our stack frame
       and jump to it. */
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
    NOT_REACHED();
}

/*! Returns true if the given file descriptor matches the one assigned to
    the file that was loaded into a running process.
 */
bool process_fd_matches(int fd) {
	struct fd_element *r;
	struct list_elem *l;
	lock_acquire(&eflock);
	for (l = list_begin(&executing_files);
			 l != list_end(&executing_files);
			 l = list_next(l)) {
		r = list_entry(l, struct fd_element, f_elem);
		if (fd == r->fd) {
			lock_release(&eflock);
			return true;
		}
	}
	lock_release(&eflock);
	return false;
}

/*! Returns the matching file's semaphore. */
struct semaphore *file_match_sema(const char *filename) {
	struct fd_element *r;
	struct list_elem *l;
	lock_acquire(&eflock);
	for (l = list_begin(&executing_files);
			 l != list_end(&executing_files);
			 l = list_next(l)) {
		r = list_entry(l, struct fd_element, f_elem);
		if (strcmp(filename, r->filename) == 0) {
			lock_release(&eflock);
			return &r->multfile_sema;
		}
	}
	lock_release(&eflock);
	return NULL;
}

/*! Returns the matching file descriptor if the given file name matches
    one used to load one (or more) of the running process(es). Otherwise
    return -1.
 */
int process_filename_matches(const char *filename) {
	struct fd_element *r;
	struct list_elem *l;
	lock_acquire(&eflock);
	for (l = list_begin(&executing_files);
			 l != list_end(&executing_files);
			 l = list_next(l)) {
		r = list_entry(l, struct fd_element, f_elem);
		if (strcmp(filename, r->filename) == 0) {
			lock_release(&eflock);
			return r->fd;
		}
	}
	lock_release(&eflock);
	return -1;
}

/*! Waits for thread TID to die and returns its exit status.  If it was
    terminated by the kernel (i.e. killed due to an exception), returns -1.
    If TID is invalid or if it was not a child of the calling process, or if
    process_wait() has already been successfully called for the given TID,
    returns -1 immediately, without waiting.

    This function will be implemented in problem 2-2.  For now, it does
    nothing.
*/
int process_wait(tid_t child_tid) {
    /* Search my child list for this child. If it exists, great. If not,
       return -1. */

    struct thread *t = thread_current();
    struct list_elem *elem = list_begin(&t->child_list);

    bool found_tid = false;
    struct thread *mychild;
    enum intr_level old_level;
    int result; 

    tid_t this_childs_tid; 

    while (elem != list_end(&t->child_list)) {        
        mychild = list_entry(elem, struct thread, chld_elem);
        this_childs_tid = mychild->tid;
        if (this_childs_tid == child_tid) {
            found_tid = true;
            break;
        }
        elem = list_next(elem);
    }


    if (!found_tid) {
    	// Possibly because of TID_ERROR, Child Termination, Child
    	// Already Waited Upon
        return -1;
    }

    // Down the sema of the child, i_am_done. Wait for it to call us back.
    // Disable interrupts so this process can't be terminated if we return.
    old_level = intr_disable();
    sema_down(&mychild->i_am_done);

    // Check the status of the child
    result = mychild->status_on_exit;

    // Snip out the child using pointers to both sides of child_list around
    // the child, then allow the child to destroy itself at will.
    list_remove(elem);
    sema_up(&mychild->may_i_die);    

    intr_set_level(old_level);    

    return result;
}

/*! Free the current process's resources. */
void process_exit(void) {
    struct thread *cur = thread_current();
    uint32_t *pd;

    if (thread_current()->tfile.filename != NULL) {
    	list_remove(&thread_current()->tfile.f_elem);
    	thread_current()->tfile.filename = NULL;
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
}

/*! Sets up the CPU for running user code in the current thread.
    This function is called on every context switch. */
void process_activate(void) {
    struct thread *t = thread_current();

    /* Activate thread's page tables. */
    pagedir_activate(t->pagedir);

    /* Set thread's kernel stack for use in processing interrupts. */
    tss_update();
}

/*! We load ELF binaries.  The following definitions are taken
    from the ELF specification, [ELF1], more-or-less verbatim.  */

/*! ELF types.  See [ELF1] 1-2. @{ */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;
/*! @} */

/*! For use with ELF types in printf(). @{ */
#define PE32Wx PRIx32   /*!< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /*!< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /*!< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /*!< Print Elf32_Half in hexadecimal. */
/*! @} */

/*! Executable header.  See [ELF1] 1-4 to 1-8.
    This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
};

/*! Program header.  See [ELF1] 2-2 to 2-4.  There are e_phnum of these,
    starting at file offset e_phoff (see [ELF1] 1-6). */
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/*! Values for p_type.  See [ELF1] 2-3. @{ */
#define PT_NULL    0            /*!< Ignore. */
#define PT_LOAD    1            /*!< Loadable segment. */
#define PT_DYNAMIC 2            /*!< Dynamic linking info. */
#define PT_INTERP  3            /*!< Name of dynamic loader. */
#define PT_NOTE    4            /*!< Auxiliary info. */
#define PT_SHLIB   5            /*!< Reserved. */
#define PT_PHDR    6            /*!< Program header table. */
#define PT_STACK   0x6474e551   /*!< Stack segment. */
/*! @} */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. @{ */
#define PF_X 1          /*!< Executable. */
#define PF_W 2          /*!< Writable. */
#define PF_R 4          /*!< Readable. */
/*! @} */

static bool setup_stack(void **esp, const char *file_name);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/*! Loads an ELF executable from FILE_NAME into the current thread.  Stores the
    executable's entry point into *EIP and its initial stack pointer into *ESP.
    Returns true if successful, false otherwise. */
bool load(const char *file_name, void (**eip) (void), void **esp) {
    struct thread *t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i = 0;
    char *progname = NULL;

    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL) 
        goto done;
    process_activate();

    /* Open executable file. */

    // Find the program name, which is the first token.
    progname = (char *) palloc_get_page(0);
	if (progname == NULL)
		goto done;
	strlcpy(progname, file_name, PGSIZE);
	while (progname[i] != ' ' && progname[i] != '\0') {
		i++;
	}
	progname[i] = '\0';

    // Store the filename in the thread struct
	lock_acquire(&eflock);
    if (thread_current()->tfile.filename != NULL) {
    	strlcpy(thread_current()->tfile.filename, progname,
    			strlen(progname) + 1);
    	// Store in the list of executing files.
    	list_push_back(&executing_files, &thread_current()->tfile.f_elem);
    }
    lock_release(&eflock);

    lock_acquire(&sys_lock);
    file = filesys_open(progname);
    if (file == NULL) {
        printf("load: %s: open failed\n", progname);
        lock_release(&sys_lock);
        goto done; 
    }
    lock_release(&sys_lock);

    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
        memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
        ehdr.e_machine != 3 || ehdr.e_version != 1 ||
        ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
        printf("load: %s: error loading executable\n", file_name);
        goto done; 
    }

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
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE)
                    		- read_bytes);
                }
                else {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *) mem_page,
                                  read_bytes, zero_bytes, writable))
                    goto done;
            }
            else {
                goto done;
            }
            break;
        }
    }

    /* Set up stack. */
    if (!setup_stack(esp, file_name))
        goto done;

    /* Start address. */
    *eip = (void (*)(void)) ehdr.e_entry;

    success = true;

done:
	if(progname != NULL) {
		palloc_free_page((void *) progname);
	}
    /* We arrive here whether the load is successful or not. */
    file_close(file);
    return success;
}

/* load() helpers. */

/*! Checks whether PHDR describes a valid, loadable segment in
    FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr *phdr,
		struct file *file) {
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
       Not only is it a bad idea to map page 0, but if we allowed it then user
       code that passed a null pointer to system calls could quite likely panic
       the kernel by way of null pointer assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* It's okay. */
    return true;
}

/*! Loads a segment starting at offset OFS in FILE at address UPAGE.  In total,
    READ_BYTES + ZERO_BYTES bytes of virtual memory are initialized, as
    follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

    The pages initialized by this function must be writable by the user process
    if WRITABLE is true, read-only otherwise.

    Return true if successful, false if a memory allocation error or disk read
    error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);
    int i = 0;

    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0) {
        /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* Allocate a supplemental page table entry. */
        // TODO just added this
        struct spgtbl_elem *s;

        // TODO ofs needs to travel with
        s = pg_put(-1, ofs + PGSIZE * i++, NULL, upage,file, page_zero_bytes,
        		writable, page_read_bytes == 0 ? ZERO_PG : EXECD_FILE_PG);

        if (!install_page(upage, (void *) s, writable, true)) {
			free(s);
			return false;
		}

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/*! Create a minimal stack by mapping a zeroed page at the top of
    user virtual memory. */
static bool setup_stack(void **esp, const char *file_name) {
    uint8_t *kpage;
    int argc = 0;
    bool success = false;
    char *fncopy, *token, *save_ptr;
    char *start = (char *) PHYS_BASE;
    char *ptr = (char *) PHYS_BASE;
    int i;

    // Make a copy of FILE_NAME. Need a non-const one for tok'ing to work.
    fncopy = (char *) palloc_get_page(0);
    if (fncopy == NULL)
        return false;
    strlcpy(fncopy, file_name, PGSIZE);

    /* Setup the stack. */

    kpage = fr_alloc_page(PHYS_BASE - PGSIZE, OTHER_PG);
    // Replaced this with call to fr_alloc_page, which was confirmed to work
    // before modification of load_segment.
    // kpage = palloc_get_page(PAL_USER | PAL_ZERO);

    if (kpage != NULL) {
        success = install_page(((uint8_t *) PHYS_BASE) - PGSIZE, kpage,
        		true, false);
        if (success) {

            // Copy argv elements onto the stack as they're parsed out.
            for (token = strtok_r(fncopy, " ", &save_ptr);
                 token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {

                start = ptr - strlen(token) - 1;
                if ((void *)start < PHYS_BASE - PGSIZE) {
                	palloc_free_page((void *) fncopy);
                	return false;
                }
                strlcpy(start, token, strlen(token) + 1);
                ptr = start;
                argc++;
            }

            // Perform word alignment (round address down to nearest multiple
            // of 4).
            ptr = (char *) ((((unsigned int) ptr) / 4) * 4);

            // Add in the pointers to the strings in the argv array.
            char **vptr = (char **) ptr;
            vptr--;
            vptr--; // This one is for the null-terminator of argv
            for (i = 0; i < argc; i++) {
            	if ((void *) vptr < PHYS_BASE - PGSIZE) {
					palloc_free_page((void *) fncopy);
					return false;
				}
                *vptr = start;
                start += strlen(start) + 1;
                vptr--;
            }

            // Set up argv pointer, which is char **argv, and argc
            if ((void *) vptr < PHYS_BASE - PGSIZE) {
				palloc_free_page((void *) fncopy);
				return false;
			}
            *vptr = (char *) (vptr + 1);
            vptr--;
            if ((void *) vptr < PHYS_BASE - PGSIZE) {
				palloc_free_page((void *) fncopy);
				return false;
			}
            *vptr = (char *) argc;

            // The bogus return address.
            vptr--;

            *esp = (void *) vptr;
        }
        else
            palloc_free_page(kpage);
    }

    palloc_free_page((void *) fncopy);
    return success;
}

/*! Adds a mapping from user virtual address UPAGE to kernel
    virtual address KPAGE to the page table.
    If WRITABLE is true, the user process may modify the page;
    otherwise, it is read-only.
    UPAGE must not already be mapped.
    KPAGE should probably be a page obtained from the user pool
    with palloc_get_page().
    Returns true on success, false if UPAGE is already mapped or
    if memory allocation fails. */
bool install_page(void *upage, void *kpage, bool writable,
		bool supplemental) {
    struct thread *t = thread_current();

    /* Verify that there's not already a page at that virtual
       address, then map our page there. */
    return (pagedir_get_page(t->pagedir, upage) == NULL &&
            pagedir_set_page(t->pagedir, upage, kpage, writable,
            		supplemental));
}
