/*! \file synch.c
 *
 * Implementation of various thread synchronization primitives.
 */

/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/*! Initializes semaphore SEMA to VALUE.  A semaphore is a
    nonnegative integer along with two atomic operators for
    manipulating it:

    - down or "P": wait for the value to become positive, then
      decrement it.

    - up or "V": increment the value (and wake up one waiting
      thread, if any). */
void sema_init(struct semaphore *sema, unsigned value) {
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/*! Down or "P" operation on a semaphore.  Waits for SEMA's value
    to become positive and then atomically decrements it.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but if it sleeps then the next scheduled
    thread will probably turn interrupts back on. 

    ==ASIDE== This PINTOS comment suggests it is possible to return to this
    function after thread_block() with interrupts in a different state than we 
    left with. I do not believe this is possible. */
void sema_down(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0) {
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/*! Down or "P" operation on a semaphore, but only if the
    semaphore is not already 0.  Returns true if the semaphore is
    decremented, false otherwise.

    This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema) {
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0) {
        sema->value--;
        success = true; 
    }
    else {
      success = false;
    }
    intr_set_level(old_level);

    return success;
}

/*! Up or "V" operation on a semaphore.  Increments SEMA's value
    and wakes up one thread of those waiting for SEMA, if any.

    This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (!list_empty(&sema->waiters)) {
        thread_unblock(list_entry(list_pop_front(&sema->waiters),
                                  struct thread, elem));
    }
    sema->value++;
    intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/*! Self-test for semaphores that makes control "ping-pong"
    between a pair of threads.  Insert calls to printf() to see
    what's going on. */
void sema_self_test(void) {
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema, 0,
    		NULL, NULL);
    for (i = 0; i < 10; i++) {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf ("done.\n");
}

/*! Thread function used by sema_self_test(). */
static void sema_test_helper(void *sema_) {
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++) {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
    }
}

/*! Initializes LOCK.  A lock can be held by at most a single
    thread at any given time.  Our locks are not "recursive", that
    is, it is an error for the thread currently holding a lock to
    try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock) {
    ASSERT(lock != NULL);

    lock->holder = NULL;
    sema_init(&lock->semaphore, 1);
}

/*! Acquires LOCK, sleeping until it becomes available if
    necessary.  The lock must not already be held by the current
    thread.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but interrupts will be turned back on if
    we need to sleep. */
void lock_acquire(struct lock *lock) {
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    sema_down(&lock->semaphore);
    lock->holder = thread_current();
}

/*! Tries to acquires LOCK and returns true if successful or false
    on failure.  The lock must not already be held by the current
    thread.

    This function will not sleep, so it may be called within an
    interrupt handler. */
bool lock_try_acquire(struct lock *lock) {
    bool success;

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success)
      lock->holder = thread_current();

    return success;
}

/*! Releases LOCK, which must be owned by the current thread.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to release a lock within an interrupt
    handler. */
void lock_release(struct lock *lock) {
    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    lock->holder = NULL;
    sema_up(&lock->semaphore);
}

/*! Returns true if the current thread holds LOCK, false
    otherwise.  (Note that testing whether some other thread holds
    a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock) {
    ASSERT(lock != NULL);

    return lock->holder == thread_current();
}

/*! One semaphore in a list. */
struct semaphore_elem {
    struct list_elem elem;              /*!< List element. */
    struct semaphore semaphore;         /*!< This semaphore. */
};

/*! Initializes condition variable COND.  A condition variable
    allows one piece of code to signal a condition and cooperating
    code to receive the signal and act upon it. */
void cond_init(struct condition *cond) {
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
}

/*! Atomically releases LOCK and waits for COND to be signaled by
    some other piece of code.  After COND is signaled, LOCK is
    reacquired before returning.  LOCK must be held before calling
    this function.

    The monitor implemented by this function is "Mesa" style, not
    "Hoare" style, that is, sending and receiving a signal are not
    an atomic operation.  Thus, typically the caller must recheck
    the condition after the wait completes and, if necessary, wait
    again.

    A given condition variable is associated with only a single
    lock, but one lock may be associated with any number of
    condition variables.  That is, there is a one-to-many mapping
    from locks to condition variables.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but interrupts will be turned back on if
    we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));
  
    sema_init(&waiter.semaphore, 0);
    list_push_back(&cond->waiters, &waiter.elem);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/*! If any threads are waiting on COND (protected by LOCK), then
    this function signals one of them to wake up from its wait.
    LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context ());
    ASSERT(lock_held_by_current_thread (lock));

    if (!list_empty(&cond->waiters)) 
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
}

/*! Wakes up all threads, if any, waiting on COND (protected by
    LOCK).  LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}

/*! Initializes a file sector read/write lock. */
void rw_init(struct rwlock *rwlock) {
	ASSERT(rwlock != NULL);

	cond_init(&rwlock->rcond);
	cond_init(&rwlock->wcond);
	lock_init(&rwlock->lock);

	rwlock->num_waiting_readers = 0;
	rwlock->num_waiting_writers = 0;
	rwlock->num_current_readers = 0;
	rwlock->mode = UNLOCKED;
}

/*! Acquires the given read/write lock as a reader if READ is true or as a
    writer if READ is false. Waits if necessary. Ensures fairness by e.g.
    making a (new) reader wait if a writer is waiting for some (old) readers
    to finish.

    TODO If EVICT is true then... */
void rw_acquire(struct rwlock *rwlock, bool read, bool evict) {
	ASSERT(rwlock != NULL);

	lock_acquire(&rwlock->lock);
	switch (rwlock->mode) {
	case UNLOCKED:
		ASSERT(rwlock->num_waiting_readers == 0);
		ASSERT(rwlock->num_waiting_writers == 0);
		/* If the mode is UNLOCKED then just acquire the lock. */
		if (read)
			rwlock->mode = RLOCKED;
		else
			rwlock->mode = WLOCKED;
		break;
	case RLOCKED:
		/*
		If the mode is RLOCKED, no writers are blocked, and a new readers tries
		to get the lock then it gets it immediately.

	    If the mode is RLOCKED, no writers are blocked, and a writer tries to
	    get the lock then it blocks until the readers are done. But if a new
	    reader comes along it must wait for the writer to finish. */
		if (read) {
			if (rwlock->num_waiting_writers == 0) {
				// Just join the pool of readers and exit. This means doing
				// nothing here.
			}
			else {
				rwlock->num_waiting_readers++;
				do {
					cond_wait(&rwlock->rcond, &rwlock->lock);
				} while(rwlock->mode != RLOCKED);
				rwlock->num_waiting_readers--;
				ASSERT(rwlock->mode == RLOCKED);
			}
		}
		else {
			rwlock->num_waiting_writers++;
			do {
				cond_wait(&rwlock->wcond, &rwlock->lock);
			} while (rwlock->mode != WLOCKED);
			rwlock->num_waiting_writers--;
			ASSERT(rwlock->mode == WLOCKED);
		}
		break;
	case WLOCKED:
		/*
		If the mode is WLOCKED and a writer tries to get the lock then it
		blocks until the original writer is finished. But if a reader is
		waiting too then the reader gets the lock before the new writer
		does. */
		if (read) {
			rwlock->num_waiting_readers++;
			do {
				cond_wait(&rwlock->rcond, &rwlock->lock);
			} while(rwlock->mode != RLOCKED);
			rwlock->num_waiting_readers--;
			ASSERT(rwlock->mode == RLOCKED);
		}
		else {
			rwlock->num_waiting_writers++;
			do {
				cond_wait(&rwlock->wcond, &rwlock->lock);
			} while (rwlock->mode != WLOCKED);
			rwlock->num_waiting_writers--;
			ASSERT(rwlock->mode == WLOCKED);
		}
		break;
	case IOLOCKED:
		// TODO - and that read num_current_readers++ will break once we 
        // allow this
		break;
	default:
		PANIC("Unexpected rwlock mode while acquiring a read/write lock.");
		NOT_REACHED();
		break;
	}

	if (read) {
		rwlock->num_current_readers++;
	}

	lock_release(&rwlock->lock);
}

/*! Releases the given read/write lock as a reader if READ is true or as a
    writer if READ is false. Ensures fairness by e.g. signaling the appropriate
    reader or writer to wake up and acquire the r/w lock.

 	TODO If EVICT is true then...
 */
void rw_release(struct rwlock *rwlock, bool read, bool evict) {
	ASSERT(rwlock != NULL);

	lock_acquire(&rwlock->lock);
	switch (rwlock->mode) {
	case UNLOCKED:
		// Can't release a lock if the mode is UNLOCKED...
		PANIC("Can't release unlocked lock!");
		NOT_REACHED();
		break;
	case RLOCKED:
		// Must be a reader to release from an RLOCKED state.
		ASSERT(read);
		// A reader is finishing so decrement the number of current ones.
		rwlock->num_current_readers--;
		if (rwlock->num_waiting_writers == 0
				&& rwlock->num_waiting_readers == 0) {
			// Don't need to signal anyone.
			if (rwlock->num_current_readers == 0)
				rwlock->mode = UNLOCKED;
		}
		else if (rwlock->num_waiting_writers == 0
				&& rwlock->num_waiting_readers > 0) {
			// This case might happen when there is are some writers who
			// release the rwlock, wake up a pool of readers, then right
			// before that pool acquires the rwlock's monitor another reader
			// SWOOPS IN and grabs the monitor. It then starts to wait.
			// When the pool of readers finishes it comes here and sees OMG
			// there's a waiting reader. Weird, but whatever I'll signal him.
			cond_broadcast(&rwlock->rcond, &rwlock->lock);
		}
		else { // If there are some waiting writers...
			// ...signal one of them to wake up if we're the last reader.
			if (rwlock->num_current_readers == 0) {
				rwlock->mode = WLOCKED;
				cond_signal(&rwlock->wcond, &rwlock->lock);
			}
		}
		break;
	case WLOCKED:
		// Must be a writer to release from a WLOCKED state.
		ASSERT(!read);
		if (rwlock->num_waiting_writers == 0
				&& rwlock->num_waiting_readers == 0) {
			// Don't need to signal anyone.
			rwlock->mode = UNLOCKED;
		}
		// If there are any waiting readers at all then we transfer control to
		// them after this writer finishes.
		else if (rwlock->num_waiting_readers > 0) {
			rwlock->mode = RLOCKED;
			cond_broadcast(&rwlock->rcond, &rwlock->lock);
		}
		// If there are some waiting writers but NO waiting readers then...
		else {
			// ...signal one waiting writer to wake up.
			cond_signal(&rwlock->wcond, &rwlock->lock);
		}
		break;
	case IOLOCKED:
		// TODO
		break;
	default:
		PANIC("Unexpected rwlock mode while releasing a read/write lock.");
		NOT_REACHED();
		break;
	}
	lock_release(&rwlock->lock);
}
