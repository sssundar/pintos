/*! \file timer.c
 *
 * See [8254] for hardware details of the 8254 timer chip.
 */

#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
  
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/*! Number of timer ticks since OS booted. */
static int64_t ticks;

/*!< List of threads waiting for ticks. Initialized by timer_init(). */
static struct list timed_nappers;    

/*! Number of loops per timer tick.  Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

extern struct list all_list;
extern struct list ready_list;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);
static void real_time_delay(int64_t num, int32_t denom);
static bool less_ticks (const struct list_elem *a, 
                        const struct list_elem* b, 
                        void *aux UNUSED);
bool less_sort (const struct list_elem *a, 
				const struct list_elem* b, 
				void *aux UNUSED);


/*! Sets up the timer to interrupt TIMER_FREQ times per second,
    and registers the corresponding interrupt. Initializes the
    list of threads blocked on timer_sleep calls. */
void timer_init(void) {
    pit_configure_channel(0, 2, TIMER_FREQ);
    intr_register_ext(0x20, timer_interrupt, "8254 Timer");
    list_init(&timed_nappers);
}

/*! Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void) {
    unsigned high_bit, test_bit;

    ASSERT(intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    /* Approximate loops_per_tick as the largest power-of-two
       still less than one timer tick. */
    loops_per_tick = 1u << 10;
    while (!too_many_loops (loops_per_tick << 1)) {
        loops_per_tick <<= 1;
        ASSERT(loops_per_tick != 0);
    }

    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1) {
        if (!too_many_loops(high_bit | test_bit))
            loops_per_tick |= test_bit;
    }

    printf("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/*! Returns the number of timer ticks since the OS booted. */
int64_t timer_ticks(void) {
    enum intr_level old_level = intr_disable();
    int64_t t = ticks;
    intr_set_level(old_level);
    return t;
}

/*! Returns the number of timer ticks elapsed since THEN, which
    should be a value once returned by timer_ticks(). */
int64_t timer_elapsed(int64_t then) {
    return timer_ticks() - then;
}

/*  Ignores aux, and performs an arithmetic less than on the ticks remaining
    in the thread structures containing list_elem's a, b. Returns true if 
    a is less than b, or false if a is greater than or equal to b. */
bool less_ticks (   const struct list_elem *a, const struct list_elem* b, 
                    void *aux UNUSED) {
    struct thread *t;
    int64_t a_ticks_remaining, b_ticks_remaining;

    t = list_entry (a, struct thread, elem);
    a_ticks_remaining = t->ticks_remaining;
    t = list_entry (b, struct thread, elem);
    b_ticks_remaining = t->ticks_remaining;

    if (a_ticks_remaining < b_ticks_remaining) {
        return true;
    } 
    return false;    
}

/*  Ignores aux, and performs an arithmetic less than on the priorities 
	remaining in the thread structures containing list_elem's a, b. 
	Returns true if a's priority is greater than b's priority, false 
	otherwise. */
bool less_sort (const struct list_elem *a, const struct list_elem* b, 
                    void *aux UNUSED) {
    struct thread *t;
	struct thread *s;

    t = list_entry (a, struct thread, elem);
    s = list_entry (b, struct thread, elem);

    if (t->priority > s->priority) {
        return true;
    } 
    return false;    
}

/*! Sleeps for approximately TICKS timer ticks.  Interrupts must
    be turned on. Negative ticks will wake the thread at the next tick. */
void timer_sleep(int64_t ticks) {
    /* Makes sure interrupts are enabled. */
    ASSERT(intr_get_level() == INTR_ON);           

    struct thread * t;

    /*  Assigns current thread's ticks_remaining to ticks.
        It falls on the caller to avoid a race condition. */
    t = thread_current();
    t->ticks_remaining = ticks;

    /*  Disables interrupts, adds current thread (running) to the nappers list, 
        maintaining min-sorted order, in linear time. */
    intr_disable();    

    if ( list_empty(&timed_nappers) ) {
        list_push_back (&timed_nappers, &t->elem);
    } else {
        list_insert_ordered (   &timed_nappers, &t->elem, 
                                (list_less_func *) less_ticks, NULL);
    }    

    /* Blocks thread with interrupts disabled, as required. */
    thread_block();

    /*  Re-enables interrupts, as they were enabled when we entered but are 
        disabled on re-entry from thread_block. */
    intr_enable();
}

/*! Sleeps for approximately MS milliseconds.  Interrupts must be turned on. */
void timer_msleep(int64_t ms) {
    real_time_sleep(ms, 1000);
}

/*! Sleeps for approximately US microseconds.  Interrupts must be turned on. */
void timer_usleep(int64_t us) {
    real_time_sleep(us, 1000 * 1000);
}

/*! Sleeps for approximately NS nanoseconds.  Interrupts must be turned on. */
void timer_nsleep(int64_t ns) {
    real_time_sleep(ns, 1000 * 1000 * 1000);
}

/*! Busy-waits for approximately MS milliseconds.  Interrupts need not be
    turned on.

    Busy waiting wastes CPU cycles, and busy waiting with interrupts off for
    the interval between timer ticks or longer will cause timer ticks to be
    lost.  Thus, use timer_msleep() instead if interrupts are enabled. */
void timer_mdelay(int64_t ms) {
    real_time_delay(ms, 1000);
}

/*! Sleeps for approximately US microseconds.  Interrupts need not be turned on.

    Busy waiting wastes CPU cycles, and busy waiting with interrupts off for
    the interval between timer ticks or longer will cause timer ticks to be
    lost.  Thus, use timer_usleep() instead if interrupts are enabled. */
void timer_udelay(int64_t us) {
    real_time_delay(us, 1000 * 1000);
}

/*! Sleeps execution for approximately NS nanoseconds.  Interrupts need not be
    turned on.

    Busy waiting wastes CPU cycles, and busy waiting with interrupts off for
    the interval between timer ticks or longer will cause timer ticks to be
    lost.  Thus, use timer_nsleep() instead if interrupts are enabled. */
void timer_ndelay(int64_t ns) {
    real_time_delay(ns, 1000 * 1000 * 1000);
}

/*! Prints timer statistics. */
void timer_print_stats(void) {
    printf("Timer: %"PRId64" ticks\n", timer_ticks());
}

/*! Timer interrupt handler. Interrupts are disabled before entry. */
static void timer_interrupt(struct intr_frame *args UNUSED) {
    struct thread *thread_walker;
    struct list_elem *list_walker;

    ticks++;
    thread_tick();    

	if(thread_mlfqs){
		/* recent_cpu updated each timer tick. */
		thread_current()->recent_cpu=thread_current()->recent_cpu + int_to_fp(1);

		if(ticks % TIMER_FREQ == 0){
			/* Calculate the load_avg and every thread's recent_cpu*/
			load_avg_calculate();
			
			list_walker = list_begin(&all_list);
			while(list_walker != list_end(&all_list)){
				thread_walker = list_entry(list_walker, struct thread, 
										   allelem);
				recent_cpu_calculate(thread_walker);
				list_walker = list_walker->next;
			}
		}

		if (ticks % 4 == 0){
			
			list_walker = list_begin(&all_list);
			while(list_walker != list_end(&all_list)){
				thread_walker = list_entry(list_walker, struct thread, 
										   allelem);
				priority_calculate(thread_walker);
				list_walker = list_walker->next;
			}
			/* Don't forget to sort the ready list. */
			list_sort(&ready_list, less_sort, NULL);
		}

	}
        
    /*  Decrement all nappers' ticks_remaining (maintains min-sorting).
        Walk from the minimum (head->next) to the maximum (tail->prev).        
        For any that go <= 0 (ignore overflow, practically speaking),
        remove them from the nappers list and unblock them. These 
        are always guaranteed to be at the head of the list throughout
        the iteration. */
    if (!list_empty(&timed_nappers)) {
        list_walker = list_front(&timed_nappers);            
        do {            
            thread_walker = list_entry(list_walker, struct thread, elem);
            thread_walker->ticks_remaining--;
            if (thread_walker->ticks_remaining <= 0
            		&& thread_walker->status == THREAD_BLOCKED) {
                /*  ==TODO== Could extend thread_unblock() to handle multiple
                    threads at once (e.g. if we spliced out a segment rather
                    than removing one at a time), but let's wait on Hamik's
                    priority scheduling implementation. */                
                list_remove(list_walker); 
                thread_unblock(thread_walker);
            }
            list_walker = list_walker->next;
        } while (list_walker->next != NULL);
    }    
}

/*! Returns true if LOOPS iterations waits for more than one timer tick,
    otherwise false. */
static bool too_many_loops(unsigned loops) {
    /* Wait for a timer tick. */
    int64_t start = ticks;
    while (ticks == start)
        barrier();

    /* Run LOOPS loops. */
    start = ticks;
    busy_wait(loops);

    /* If the tick count changed, we iterated too long. */
    barrier();
    return start != ticks;
}

/*! Iterates through a simple loop LOOPS times, for implementing brief delays.

    Marked NO_INLINE because code alignment can significantly affect timings,
    so that if this function was inlined differently in different places the
    results would be difficult to predict. */
static void NO_INLINE busy_wait(int64_t loops) {
    while (loops-- > 0)
        barrier();
}

/*! Sleep for approximately NUM/DENOM seconds. */
static void real_time_sleep(int64_t num, int32_t denom) {
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
          (NUM / DENOM) s          
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
       1 s / TIMER_FREQ ticks
    */
    int64_t ticks = num * TIMER_FREQ / denom;

    ASSERT(intr_get_level() == INTR_ON);
    if (ticks > 0) {
        /* We're waiting for at least one full timer tick.  Use timer_sleep()
           because it will yield the CPU to other processes. */                
        timer_sleep(ticks); 
    }
    else {
        /* Otherwise, use a busy-wait loop for more accurate sub-tick timing. */
        real_time_delay(num, denom); 
    }
}

/*! Busy-wait for approximately NUM/DENOM seconds. */
static void real_time_delay(int64_t num, int32_t denom) {
    /* Scale the numerator and denominator down by 1000 to avoid
       the possibility of overflow. */
    ASSERT(denom % 1000 == 0);
    busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}

