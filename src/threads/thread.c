#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "lib/user/syscall.h"
#include "list.h"
#include "threads/thread.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

// ------------------------------ Definitions ---------------------------------

#define TIME_SLICE 4                 /*!< # of timer ticks for each thread. */

/*! Random value for struct thread's `magic' member. Used to detect stack
    overflow.  See the big comment at the top of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

// ---------------------------- Global variables ------------------------------

extern struct list executing_files;  /*!< List of all executing files. */
extern struct lock eflock;           /*!< Extern'd lock from process.c */
extern struct semaphore crude_time;  /*!< Wakes up write-behind in filesys.c */
extern long long total_ticks;        /*!< # ticks. Used as crude timer. */

bool timer_initd;                    /*!< True if crude timer is init'd. */
int max_fd = 3;						 /*!< Maximum file-desc assigned so far. */

static struct thread *idle_thread;   /*!< Idle thread. */
static struct lock tid_lock;		 /*!< Lock used by allocate_tid(). */
static long long idle_ticks;         /*!< # timer ticks spent idle. */
static long long kernel_ticks;       /*!< # timer ticks in kernel threads. */
static long long user_ticks;         /*!< # timer ticks in user programs. */
static unsigned thread_ticks;   /*!< # of timer ticks since last yield. */

/*! Private pointer to the initial thread. */
static volatile struct thread *the_init_thread;

/*!< Is init, the thread running init.c:main(). */
static struct thread *initial_thread;

/*! List of all processes.  Processes are added to this list
    when they are first scheduled and removed when they exit. */
static struct list all_list;

/*! List of processes in THREAD_READY state, that is, processes
    that are ready to run but not actually running. */
static struct list ready_list;

/*! If false (default), use round-robin scheduler.
    If true, use multi-level feedback queue scheduler.
    Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

// ------------------------------ Structures ----------------------------------

/*! Stack frame for kernel_thread(). */
struct kernel_thread_frame {
    void *eip;                  /*!< Return address. */
    thread_func *function;      /*!< Function to call. */
    void *aux;                  /*!< Auxiliary data for function. */
};

// ------------------------------ Prototypes ----------------------------------

static void kernel_thread(thread_func *, void *aux);
static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
static tid_t allocate_tid(void);
static void init_thread(struct thread *t, const char *name, int priority,
		uint8_t flag_child, block_sector_t pcwd,
		struct list *parents_child_list);
void thread_schedule_tail(struct thread *prev);

// -------------------------------- Bodies ------------------------------------

/*! Initializes the threading system by transforming the code
    that's currently running into a thread.  This can't work in
    general and it is possible in this case only because loader.S
    was careful to put the bottom of the stack at a page boundary.

    Also initializes the run queue and the tid lock.

    After calling this function, be sure to initialize the page allocator
    before trying to create any threads with thread_create().

    It is not safe to call thread_current() until this function finishes. 

    The initial thread, depending on user arguments, might need to 
    pretend to be a process so that it can wait for the user program running */
void thread_init(void) {
    ASSERT(intr_get_level() == INTR_OFF);

    lock_init(&tid_lock);
    list_init(&ready_list);
    list_init(&all_list);
    list_init(&executing_files);
    lock_init(&eflock);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread();    

    init_thread(initial_thread, "main", PRI_DEFAULT, 0, BOGUS_SECTOR, NULL);
    initial_thread->status = THREAD_RUNNING;    
    initial_thread->tid = allocate_tid();
    the_init_thread = initial_thread;

    timer_initd = false;
}

/*! Sets the initial thread's current working directory. Cannot be called
    before the file system has been initialized. */
void thread_set_initial_thread_cwd(void) {

    /* Get the root directory's inode pointer and set the init thread's CWD. */
    struct inode* root_dir_inode = inode_open(ROOT_DIR_SECTOR);
    ASSERT(root_dir_inode->is_dir);
    root_dir_inode->parent_dir = BOGUS_SECTOR; // No parent for init thread.
    the_init_thread->cwd_sect = root_dir_inode->sector;
}

/*! Starts preemptive thread scheduling by enabling interrupts.
    Also creates the idle thread. */
void thread_start(void) {
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started, 0, NULL, NULL);

    /* Start preemptive thread scheduling. */
    intr_enable();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down(&idle_started);
}

/*! Called by the timer interrupt handler at each timer tick.
    Thus, this function runs in an external interrupt context. */
void thread_tick(void) {
    struct thread *t = thread_current();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    total_ticks++;

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return();
}

/*! Prints thread statistics. */
void thread_print_stats(void) {
    printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
           idle_ticks, kernel_ticks, user_ticks);
}

/*! Creates a new kernel thread named NAME with the given initial PRIORITY,
    which executes FUNCTION passing AUX as the argument, and adds it to the
    ready queue.  Returns the thread identifier for the new thread, or
    TID_ERROR if creation fails.

    If thread_start() has been called, then the new thread may be scheduled
    before thread_create() returns.  It could even exit before thread_create()
    returns.  Contrariwise, the original thread may run for any amount of time
    before the new thread is scheduled.  Use a semaphore or some other form of
    synchronization if you need to ensure ordering.

    The code provided sets the new thread's `priority' member to PRIORITY. */
tid_t thread_create(const char *name, int priority, thread_func *function,
                    void *aux, uint8_t flag_child,
					struct list *parents_child_list,
					struct thread *parent) {

    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;

    ASSERT(function != NULL);

    /* Allocate thread. */
    t = palloc_get_page(PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread(t, name, priority, flag_child,
    		parent == NULL ? BOGUS_SECTOR : parent->cwd_sect,
    		parents_child_list);
    t->parent = parent;
    tid = t->tid = allocate_tid();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame(t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame(t, sizeof *ef);
    ef->eip = (void (*) (void)) kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame(t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    /* Add to list of children of parent. */ 
    list_push_back(&thread_current()->child_list,
    		&t->chld_elem);

    /* Add to run queue. */
    thread_unblock(t);

    return tid;
}

/*! Puts the current thread to sleep.  It will not be scheduled
    again until awoken by thread_unblock().

    This function must be called with interrupts turned off.  It is usually a
    better idea to use one of the synchronization primitives in synch.h. */
void thread_block(void) {
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);

    thread_current()->status = THREAD_BLOCKED;
    schedule();
}

/*! Transitions a blocked thread T to the ready-to-run state.  This is an
    error if T is not blocked.  (Use thread_yield() to make the running
    thread ready.)

    This function does not preempt the running thread.  This can be important:
    if the caller had disabled interrupts itself, it may expect that it can
    atomically unblock a thread and update other data. */
void thread_unblock(struct thread *t) {
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    list_push_back(&ready_list, &t->elem);
    t->status = THREAD_READY;
    intr_set_level(old_level);
}

/*! Returns the name of the running thread. */
const char * thread_name(void) {
    return thread_current()->name;
}

/*! Returns the running thread. This is running_thread() plus a couple of
    sanity checks. See the big comment at the top of thread.h for details. */
struct thread * thread_current(void) {
    struct thread *t = running_thread();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_RUNNING);

    return t;
}

/*! Returns the running thread's tid. */
tid_t thread_tid(void) {
    return thread_current()->tid;
}

/*! Deschedules the current thread and destroys it.  Never
    returns to the caller. */
void thread_exit(void) {
    ASSERT(!intr_context());
    struct list_elem *l;
    struct thread *chld_t;

#ifdef USERPROG

	/* Close the open files.*/
	struct list_elem *l2;
	struct fd_element *r2;
	for (l2 = list_begin(&thread_current()->files);
			 l2 != list_end(&thread_current()->files);
			 l2 = list_next(l2)) {
		r2 = list_entry(l2, struct fd_element, f_elem);
		file_close(r2->file);
	}

    /* Tell parent function that I'm dying. Remove from its child list. */
    if (thread_current()->parent != NULL) {
		for (l = list_begin(&thread_current()->parent->child_list);
				l != list_end(&thread_current()->parent->child_list);
				l = list_next(l)) {
			chld_t = list_entry(l, struct thread, chld_elem);
			if (chld_t->tid == thread_current()->tid) {
				list_remove(l);
				break;
			}
		}
    }

    /* Iterate over all children, setting their parent pointers to NULL. */
	for (l = list_begin(&thread_current()->child_list);
			l != list_end(&thread_current()->child_list);
			l = list_next(l)) {
		chld_t = list_entry(l, struct thread, chld_elem);
		chld_t->parent = NULL;
	}

	if (thread_current()->tfile.filename != NULL)
	    palloc_free_page((void *) thread_current()->tfile.filename);

    process_exit();
#endif

    intr_disable();
    list_remove(&thread_current()->allelem);
    thread_current()->status = THREAD_DYING;
    schedule();
    NOT_REACHED();
}

/*! Yields the CPU.  The current thread is not put to sleep and
    may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void) {
    struct thread *cur = thread_current();
    enum intr_level old_level;

    ASSERT(!intr_context());

    old_level = intr_disable();
    if (cur != idle_thread) 
        list_push_back(&ready_list, &cur->elem);
    cur->status = THREAD_READY;
    schedule();
    intr_set_level(old_level);
}

/*! Invoke function 'func' on all threads, passing along 'aux'.
    This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux) {
    ASSERT(intr_get_level() == INTR_OFF);
    struct list_elem *e;
    for (e = list_begin(&all_list); e != list_end(&all_list);
         e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, allelem);
        func(t, aux);
    }
}

/*! Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) {
    thread_current()->priority = new_priority;
}

/*! Returns the current thread's priority. */
int thread_get_priority(void) {
    return thread_current()->priority;
}

/*! Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED) {
    /* Not yet implemented. */
}

/*! Returns the current thread's nice value. */
int thread_get_nice(void) {
    /* Not yet implemented. */
    return 0;
}

/*! Returns 100 times the system load average. */
int thread_get_load_avg(void) {
    /* Not yet implemented. */
    return 0;
}

/*! Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
    /* Not yet implemented. */
    return 0;
}

/*! Idle thread.  Executes when no other thread is ready to run.

    The idle thread is initially put on the ready list by thread_start().
    It will be scheduled once initially, at which point it initializes
    idle_thread, "up"s the semaphore passed to it to enable thread_start()
    to continue, and immediately blocks.  After that, the idle thread never
    appears in the ready list.  It is returned by next_thread_to_run() as a
    special case when the ready list is empty. */
static void idle(void *idle_started_ UNUSED) {
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current();
    sema_up(idle_started);

    for (;;) {
        /* Let someone else run. */
        intr_disable();
        thread_block();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the completion of
           the next instruction, so these two instructions are executed
           atomically.  This atomicity is important; otherwise, an interrupt
           could be handled between re-enabling interrupts and waiting for the
           next one to occur, wasting as much as one clock tick worth of time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/*! Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux) {
    ASSERT(function != NULL);

    intr_enable();       /* The scheduler runs with interrupts off. */
    function(aux);       /* Execute the thread function. */
    thread_exit();       /* If function() returns, kill the thread. */
}

/*! Returns the running thread. */
struct thread * running_thread(void) {
    uint32_t *esp;

    /* Copy the CPU's stack pointer into `esp', and then round that
       down to the start of a page.  Because `struct thread' is
       always at the beginning of a page and the stack pointer is
       somewhere in the middle, this locates the curent thread. */
    asm ("mov %%esp, %0" : "=g" (esp));
    return pg_round_down(esp);
}

/*! Returns true if T appears to point to a valid thread. */
static bool is_thread(struct thread *t) {
    return t != NULL && t->magic == THREAD_MAGIC;
}

/*! Does basic initialization of T as a blocked thread named NAME. */
static void init_thread(struct thread *t, const char *name, int priority,
		uint8_t flag_child, block_sector_t pcwd,
		struct list *parents_child_list UNUSED) {

    enum intr_level old_level;

    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->priority = priority;
    t->magic = THREAD_MAGIC;
    t->cwd_sect = pcwd;
    list_init(&(t->files));
    old_level = intr_disable();
    t->voluntarily_exited = 0;
    list_push_back(&all_list, &t->allelem);

    /* Initialize process_wait() system call structures. */
    list_init(&t->child_list);
    sema_init(&t->i_am_done, 0); // Locked by child, implicitly.
    sema_init(&t->load_child, 0); 
    t->am_child = flag_child;  
    t->loaded = false; 
    sema_init(&t->tfile.multfile_sema, 0);

    /* Store the filename in a newly allocated page. */
    if(strcmp("main", name) != 0 && strcmp("init", name) != 0
    		&& strcmp("idle", name) != 0) {
		t->tfile.filename = palloc_get_page(0);
		if(t->tfile.filename == NULL) {
			t->tfile.fd = -1;
			PANIC("Couldn't get page for thread's filename :-(.\n");
		}
		else
			t->tfile.fd = max_fd++;
    }
    else {
    	t->tfile.fd = -1;
    	t->tfile.filename = NULL;
    }

    /* Blocking child from death on sys_exit */
    if (flag_child > 0)
        sema_init(&t->may_i_die, 0);

    /* If process, sys_exit will not block for a parent's approval. */
    else
        sema_init(&t->may_i_die, 1);
    intr_set_level(old_level);
}

/*! Allocates a SIZE-byte frame at the top of thread T's stack and
    returns a pointer to the frame's base. */
static void * alloc_frame(struct thread *t, size_t size) {
    /* Stack data is always allocated in word-size units. */
    ASSERT(is_thread(t));
    ASSERT(size % sizeof(uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/*! Chooses and returns the next thread to be scheduled.  Should return a
    thread from the run queue, unless the run queue is empty.  (If the running
    thread can continue running, then it will be in the run queue.)  If the
    run queue is empty, return idle_thread. */
static struct thread * next_thread_to_run(void) {
    if (list_empty(&ready_list))
      return idle_thread;
    else
      return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/*! Completes a thread switch by activating the new thread's page tables, and,
    if the previous thread is dying, destroying it.

    At this function's invocation, we just switched from thread PREV, the new
    thread is already running, and interrupts are still disabled.  This
    function is normally invoked by thread_schedule() as its final action
    before returning, but the first time a thread is scheduled it is called by
    switch_entry() (see switch.S).

    It's not safe to call printf() until the thread switch is complete.  In
    practice that means that printf()s should be added at the end of the
    function.

    After this function and its caller returns, the thread switch is
    complete. */
void thread_schedule_tail(struct thread *prev) {
    struct thread *cur = running_thread();
  
    ASSERT(intr_get_level() == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate();
#endif

    /* Crude timer facility. */
    if (timer_initd && total_ticks > 0
    		&& total_ticks % TICKS_UNTIL_WRITEBACK == 0)
		sema_up(&crude_time); // No waiting happens here.

    /* If the thread we switched from is dying, destroy its struct thread.
       This must happen late so that thread_exit() doesn't pull out the rug
       under itself.  (We don't free initial_thread because its memory was
       not obtained via palloc().) */
    if (prev != NULL && prev->status == THREAD_DYING &&
        prev != initial_thread) {
        ASSERT(prev != cur);
        palloc_free_page(prev);
    }
}

/*! Schedules a new process.  At entry, interrupts must be off and the running
    process's state must have been changed from running to some other state.
    This function finds another thread to run and switches to it.

    It's not safe to call printf() until thread_schedule_tail() has
    completed. */
static void schedule(void) {
    struct thread *cur = running_thread();
    struct thread *next = next_thread_to_run();
    struct thread *prev = NULL;

    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(cur->status != THREAD_RUNNING);
    ASSERT(is_thread(next));

    if (cur != next)
        prev = switch_threads(cur, next);
    thread_schedule_tail(prev);
}

/*! Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire(&tid_lock);
    tid = next_tid++;
    lock_release(&tid_lock);

    return tid;
}

/*! Finds the file struct pointer that corresponds to the given file
    descriptor. */
struct fd_element *thread_get_matching_fd_elem(int fd) {
	struct list_elem *l;
	struct fd_element *f;
	for (l = list_begin(&thread_current()->files);
			 l != list_end(&thread_current()->files);
			 l = list_next(l)) {
		f = list_entry(l, struct fd_element, f_elem);
		if (f->fd == fd)
			return f;
	}
	return NULL;
}

/*! Returns true if some process has the given directory open, if it's the
    current working directory of some process, or if it's not empty. */
bool thread_is_dir_deletable(const char *path) {
	char filename[NAME_MAX + 1];
	struct inode *parent_inode;
	struct inode *dir_inode =
			dir_get_inode_from_path(path, &parent_inode, filename);

	// If doesn't exist or isn't directory, return false.
	if (dir_inode == NULL || !dir_inode->is_dir)
		return false;

	/* Iterate over all threads, checking each one to see if the cwd is
	   the given directory or if it has the dir in its open files list. */
	struct list_elem *e;
	for (e = list_begin(&all_list);
			e != list_end(&all_list);
			e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, allelem);

		// Check if CWD is the same as the given dir.
		if (t->cwd_sect != BOGUS_SECTOR) {
			if (t->cwd_sect == dir_inode->sector) {
				inode_close(dir_inode);
				return true;
			}
		}

		/* Else iterate over all open files, checking if this dir is one. */
		struct list_elem *l;
		struct fd_element *f;
		for (l = list_begin(&thread_current()->files);
				 l != list_end(&thread_current()->files);
				 l = list_next(l)) {
			f = list_entry(l, struct fd_element, f_elem);
			if (f->file != NULL && f->file->inode != NULL
					&& f->file->inode->sector == dir_inode->sector) {
				ASSERT(f->file->inode->is_dir);
				inode_close(dir_inode);
				return true;
			}
		}
	}

	/* Make sure it's empty. */
	int i;
	for (i = 0; i < MAX_DIR_ENTRIES; i++) {
		if (dir_inode->dir_contents[i] != BOGUS_SECTOR) {
			inode_close(dir_inode);
			return true;
		}
	}

	inode_close(dir_inode);
	return false;
}

/*! Offset of `stack' member within `struct thread'.
    Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);
