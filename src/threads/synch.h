/*! \file synch.h
 *
 * Data structures and function declarations for thread synchronization
 * primitives.
 */

#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/*! A counting semaphore. */
struct semaphore {
    unsigned value;             /*!< Current value. */
    struct list waiters;        /*!< List of waiting threads. */
};

void sema_init(struct semaphore *, unsigned value);
void sema_down(struct semaphore *);
bool sema_try_down(struct semaphore *);
void sema_up(struct semaphore *);
void sema_self_test(void);

/*! Lock. */
struct lock {
    struct thread *holder;      /*!< Thread holding lock (for debugging). */
    struct semaphore semaphore; /*!< Binary semaphore controlling access. */
};




/*==TODO== Included as stubs so cache.* can compile. Hamik, replace at will.*/
struct rwlock {
	int filler;
};
void rw_init(struct rwlock*);
void rw_acquire(struct rwlock*, bool, bool);
void rw_release(struct rwlock*, bool, bool);
/*==TODO== Included as stubs so cache.* can compile. Hamik, replace at will.*/



void lock_init(struct lock *);
void lock_acquire(struct lock *);
bool lock_try_acquire(struct lock *);
void lock_release(struct lock *);
bool lock_held_by_current_thread(const struct lock *);

/*! Condition variable. */
struct condition {
    struct list waiters;        /*!< List of waiting threads. */
};

void cond_init(struct condition *);
void cond_wait(struct condition *, struct lock *);
void cond_signal(struct condition *, struct lock *);
void cond_broadcast(struct condition *, struct lock *);

/*! A read-write lock's mode. */
enum rwmode { UNLOCKED, RLOCKED, WLOCKED, PENDING_EVICTION };

/*! Read/write lock. Intended for one of these to be used with each sector in
    the file cache. */
struct rwlock {
	enum rwmode mode;
	uint32_t  num_waiting_readers;
	uint32_t num_waiting_writers;
	uint32_t num_current_readers;
	struct lock lock;
	struct condition rcond;
	struct condition wcond;
};

void rw_init(struct rwlock *);
void rw_acquire(struct rwlock *, bool, bool);
void rw_release(struct rwlock *, bool, bool);

/*! Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */

