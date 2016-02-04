#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/*! Random value for struct thread's `magic' member.
    Used to detect stack overflow.  See the big comment at the top
    of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/*! List of processes in THREAD_READY state, that is, processes
    that are ready to run but not actually running. */
static struct list ready_list;

/*! List of all processes.  Processes are added to this list
    when they are first scheduled and removed when they exit. */
static struct list all_list;

/*! Idle thread. */
static struct thread *idle_thread;

/*! Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/*! Lock used by allocate_tid(). */
static struct lock tid_lock;

static struct lock ready_list_lock;

/*! Stack frame for kernel_thread(). */
struct kernel_thread_frame {
    void *eip;                  /*!< Return address. */
    thread_func *function;      /*!< Function to call. */
    void *aux;                  /*!< Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /*!< # of timer ticks spent idle. */
static long long kernel_ticks;  /*!< # of timer ticks in kernel threads. */
static long long user_ticks;    /*!< # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /*!< # of timer ticks to give each thread. */
static unsigned thread_ticks;   /*!< # of timer ticks since last yield. */

/*! If false (default), use round-robin scheduler.
    If true, use multi-level feedback queue scheduler.
    Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);

void load_avg_calculate(void);
void recent_cpu_calculate(struct thread *t);
void priority_calculate(struct thread *t);

int load_avg;

/*! Initializes the threading system by transforming the code
    that's currently running into a thread.  This can't work in
    general and it is possible in this case only because loader.S
    was careful to put the bottom of the stack at a page boundary.

    Also initializes the run queue and the tid lock.

    After calling this function, be sure to initialize the page allocator
    before trying to create any threads with thread_create().

    It is not safe to call thread_current() until this function finishes. */
void thread_init(void) {
    ASSERT(intr_get_level() == INTR_OFF);
    
	load_avg = 0;

    lock_init(&tid_lock);    
    lock_init(&ready_list_lock);    
    list_init(&ready_list);    
    list_init(&all_list);    

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread();    
    init_thread(initial_thread, "main", PRI_DEFAULT);    
    initial_thread->status = THREAD_RUNNING;    
    initial_thread->tid = allocate_tid();    
}

/*! Starts preemptive thread scheduling by enabling interrupts.
    Also creates the idle thread. */
void thread_start(void) {
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started);

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

    The code provided sets the new thread's `priority' member to PRIORITY, but
    no actual priority scheduling is implemented.  Priority scheduling is the
    goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority, thread_func *function,
                    void *aux) {

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
    init_thread(t, name, priority);
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

    /* Add to run queue. */
    thread_unblock(t);

	if (thread_mlfqs){
		/*  Thread priority is calculated initially at thread 
			initialization. It is also recalculated once every 
			fourth clock tick, for every thread.*/
		priority_calculate(t);
	}

    // Since thread t was added to the ready list, check if its priority
    // is higher than the current thread's. If so the current thread should
    // yield.
    if (thread_get_tpriority(t) > thread_get_tpriority(thread_current())) {
    	thread_yield();
    }

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
	// CALL SORT FUNCTION AFTER PUSHING TO READY_LIST!!!!
    t->status = THREAD_READY;
    intr_set_level(old_level);


}

/*! Returns the name of the running thread. */
const char * thread_name(void) {
    return thread_current()->name;
}

/*! Returns the running thread.
    This is running_thread() plus a couple of sanity checks.
    See the big comment at the top of thread.h for details. */
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

#ifdef USERPROG
    process_exit();
#endif

    /* Remove thread from all threads list, set our status to dying,
       and schedule another process.  That process will destroy us
       when it calls thread_schedule_tail(). */
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
    if (cur != idle_thread) {
        list_push_back(&ready_list, &cur->elem);
		// SORT AFTER PUSHING ELEM ONTO READY_LIST!!!
	}
    cur->status = THREAD_READY;
    schedule();
    intr_set_level(old_level);
}

/*! Invoke function 'func' on all threads, passing along 'aux'.
    This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux) {
    struct list_elem *e;

    ASSERT(intr_get_level() == INTR_OFF);

    for (e = list_begin(&all_list); e != list_end(&all_list);
         e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, allelem);
        func(t, aux);
    }
}

/*! Sets the current thread's priority to NEW_PRIORITY.

    If the current thread no longer has the highest priority, yields. */
void thread_set_priority(int new_priority) {
	struct list_elem *e;
	struct thread *max, *t;
	enum intr_level old_level;

	// Change this thread's priority.
	thread_current()->priority = new_priority;

	// Find the highest priority thread in the ready list. If its
	// priority is higher than the new priority of this thread then yield.
	old_level = intr_disable();
	max = list_entry(list_begin (&ready_list), struct thread, elem);
    for (e = list_begin (&ready_list); e != list_end (&ready_list);
    		e = list_next (e)) {
        t = list_entry (e, struct thread, elem);
        if (thread_get_tpriority(max) < thread_get_tpriority(t)) {
        	max = t;
        }
    }
    intr_set_level(old_level);
    if (thread_get_tpriority(max) > thread_get_tpriority(thread_current())) {
    	thread_yield();
    }
}

/*! Get the maximum of the donated priorities or the thread's own priority. */
int thread_get_tpriority(struct thread *t) {
	int i;
	int8_t max = t->priority;
	for (i = 0; i < MAX_DONATIONS; i++) {
		if (t->donations_received[i].priority > max)
			max = t->donations_received[i].priority;
	}
	return (int) max;
}

int thread_get_priority(void) {
	if(mlfqs){
		return thread_current()->priority;
	}
	return thread_get_tpriority (thread_current());
}

/*! Sets the current thread's nice value to NICE. */
void thread_set_nice(int NICE) {
	ASSERT(NICE <= 20 && NICE >= -20);
	thread_current()->nice = NICE;

	/* Recalculate thread priority based on new nice value. */
	/* New priority depends on newest recent_cpu values, so 
	   recalculate those first. */
	recent_cpu_calculate(thread_current());
	priority_calculate(thread_current());
}

/*! Returns the current thread's nice value. */
int thread_get_nice(void) {
    return thread_current()->nice;
}

/*! Returns 100 times the system load average. */
int thread_get_load_avg(void) {
	int f = 1 << 14;

	// Convert load_avg to the nearest int.
	if (load_avg >= 0){
		load_avg = (load_avg + f/2)/f;
	}
	else {
		load_avg = (load_avg - f/2)/f;
	}

	// Return value.
    return 100*load_avg;
}
/*! Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
	int f = 1<<14;
	struct thread *t;

	t = current_thread();

	if (t->recent_cpu >= 0){
		t->recent_cpu = (t->recent_cpu + f/2)/f;
	}
	else {
		t->recent_cpu = (t->recent_cpu - f/2)/f;
	}

    return 100*t->recent_cpu;
}

/* Calculates the new load_avg value. */
void load_avg_calculate(void){
	int ready_threads = list_size(&ready_list);

	if(thread_current() != idle_thread){
		ready_threads += 1;
	}

	int f = 1 << 14;
	load_avg = ((int64_t)((59*f)/60))*(load_avg)/f + 
		(1*f/60)*ready_threads;

}

/* Calculates the new recent_cpu value for a thread t.*/
void recent_cpu_calculate(struct thread *t){
	ASSERT(is_thread(t));
	
	int f = 1 << 14;

	if (t == idle_thread){
		// does an idle thread have this initialized to 0?
		t->recent_cpu = t->recent_cpu;
	}
	
	int factor = ((((int64_t)(2*load_avg))*f)/(2*load_avg + 1*f));
	t->recent_cpu = (((int64_t)(factor))*t->recent_cpu)/f + f*t->nice;
}

/* Calculates the new priority value for a thread t.*/
void priority_calculate(struct thread *t){
	ASSERT(is_thread(t));
	
	int f = 1 << 14;
	int irecent_cpu;

	if (t == idle_thread){		
		return;
	}

	/* Round real number recent_cpu to the nearest integer. */
	if (t->recent_cpu >= 0){
		irecent_cpu = (t->recent_cpu + f/2)/f;
	}
	else {
		irecent_cpu = (t->recent_cpu - f/2)/f;
	}

	t->priority = PRI_MAX - (irecent_cpu/4) - 2*t->nice;

	/* Limit priorities between PRI_MIN and PRI_MAX. */
	if (t->priority > PRI_MAX){
		t->priority = PRI_MAX;
	}
	else if (t->priority < PRI_MIN){
		t->priority = PRI_MIN;
	}
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
static void init_thread(struct thread *t, const char *name, int priority) {
    enum intr_level old_level;
    int i;

    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    // Initialize the donations arrays.
    for (i = 0; i < MAX_DONATIONS; i++) {
    	(t->donations_given)[i].priority = PRIORITY_SENTINEL;
    	(t->donations_given)[i].thread = NULL;
    	(t->donations_given)[i].lock = NULL;
    	(t->donations_received)[i].priority = PRIORITY_SENTINEL;
    	(t->donations_received)[i].thread = NULL;
    	(t->donations_received)[i].lock = NULL;
    }

    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->priority = priority;
    t->magic = THREAD_MAGIC;

    old_level = intr_disable();    
    list_push_back(&all_list, &t->allelem);    
    intr_set_level(old_level);

	/* If running advanced scheduler, default nice to 0, 
	 * default recent_cpu to 0 if its the first thread, 
	 * or parent's value in other new threads. */
	if (thread_mlfqs){
		t->nice = 0;        
		if (t == initial_thread){
            t->recent_cpu = 0;
        } else {
            t->recent_cpu = thread_get_recent_cpu();            
        }		
	}
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
	struct list_elem *e, *max_e;
	struct thread *t, *max, *rtn;

    if (list_empty(&ready_list))
    	rtn = idle_thread;
    else {
    	// Find the highest priority thread in the ready list and return it.
    	max_e = list_begin (&ready_list);
    	max = list_entry(max_e, struct thread, elem);
        for (e = list_begin (&ready_list); e != list_end (&ready_list);
        		e = list_next (e)) {
            t = list_entry (e, struct thread, elem);
            if (thread_get_tpriority(max) < thread_get_tpriority(t)) {
            	max = t;
            	max_e = e;
            }
        }
        rtn = max;
        list_remove(max_e);
    }
    return rtn;
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

/*! Inserts the given priority into the given thread's donations_received
    array. If there isn't sufficient space returns false, otherwise returns
    true. Donations can be nested or chained; this function searches the
    recipient's donations_given array to see who received a donation, then
    it forwards the donation to that thread if necessary. */
bool thread_donate_priority(int8_t priority, struct thread *recipient,
		struct thread *donor, struct lock *the_lock, int nesting) {
	int i, ridx = -1, didx = -1;
	enum intr_level old_level;

	old_level = intr_disable();

	// Base case for nesting recursion.
	if (nesting >= 10) {
		return false;
	}

	// Find an opening in the receiver's received donations array unless
	// there's an element with the same lock.
	for (i = 0; i < MAX_DONATIONS; i++) {
		if ((recipient->donations_received)[i].lock == the_lock) {
			ridx = i;
			break;
		}
	}
	if (ridx == -1) {
		for (i = 0; i < MAX_DONATIONS; i++) {
			if ((recipient->donations_received)[i].priority ==
					PRIORITY_SENTINEL) {
				ridx = i;
				break;
			}
		}
	}

	// Find an opening in the giver's donations given array unless there's
	// something with the same lock.
	for (i = 0; i < MAX_DONATIONS; i++) {
		if ((donor->donations_given)[i].lock == the_lock) {
			didx = i;
			break;
		}
	}
	if (didx == -1) {
		for (i = 0; i < MAX_DONATIONS; i++) {
			if ((donor->donations_given)[i].priority == PRIORITY_SENTINEL) {
				didx = i;
				break;
			}
		}
	}
	if (ridx == -1 || didx == -1)
		return false;

	// Log the priority that the donor gave away.
	donor->donations_given[didx].priority = priority;
	donor->donations_given[didx].thread = recipient;
	donor->donations_given[didx].lock = the_lock;

	// Log the priority that the receiver got.
	recipient->donations_received[ridx].priority = priority;
	recipient->donations_received[ridx].thread = donor;
	recipient->donations_received[ridx].lock = the_lock;

	// Donate to a thread that the recipient thread is waiting on (if any).
	for (i = 0; i < MAX_DONATIONS; i++) {
		if ((recipient->donations_given)[i].priority != PRIORITY_SENTINEL &&
				(recipient->donations_given)[i].priority < priority) {

			thread_donate_priority(priority,
					(recipient->donations_given)[i].thread, donor,
					(recipient->donations_given)[i].lock, nesting + 1);
			break;
		}
	}

	intr_set_level(old_level);
	return true;
}

/*! Clear the donation corresponding to this lock then clear all the
    corresponding giver's donations. */
bool thread_giveback_priority(struct thread *recipient,
		struct lock *the_lock) {
	int i, j;
	int8_t priority;//, didx = -1, ridx = -1;
	struct thread *donor;
	enum intr_level old_level;

	old_level = intr_disable();

	// Find every thread that donated a priority under the given lock
	// to this thread, then clear all the donations given by that thread
	// to this one.
	for (i = 0; i < MAX_DONATIONS; i++) {
		if ((recipient->donations_received)[i].lock == the_lock) {
			donor = (recipient->donations_received)[i].thread;
			priority = (recipient->donations_received)[i].priority;

			// Clear all the donations from the giver.
			for (j = 0; j < MAX_DONATIONS; j++) {
				if ((donor->donations_given)[j].thread == recipient &&
						(donor->donations_given)[j].priority == priority &&
						(donor->donations_given)[j].lock == the_lock) {

					// Clear the giver's donation.
					donor->donations_given[j].priority = PRIORITY_SENTINEL;
					donor->donations_given[j].thread = NULL;
					donor->donations_given[j].lock = NULL;
				}
			}

			// Clear the receiver's donation.
			recipient->donations_received[i].priority = PRIORITY_SENTINEL;
			recipient->donations_received[i].thread = NULL;
			recipient->donations_received[i].lock = NULL;
		}
	}

	intr_set_level(old_level);

	return true;
}

/*! Offset of `stack' member within `struct thread'.
    Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);
