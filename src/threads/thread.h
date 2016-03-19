/*! \file thread.h
 *
 * Declarations for the kernel threading functionality in PintOS.
 */

#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"

// ------------------------------ Definitions ---------------------------------

typedef int tid_t;						/*! Thread ID type. */
#define TID_ERROR ((tid_t) -1)          /*!< Error value for tid_t. */
#define PRI_MIN 0                       /*!< Lowest priority. */
#define PRI_DEFAULT 31                  /*!< Default priority. */
#define PRI_MAX 63                      /*!< Highest priority. */

/*! Number of ticks until the cache is flushed to disk. Chosen to be roughly
    three times the length of a disk write. */
#define TICKS_UNTIL_WRITEBACK 512

// ---------------------------- Global variables ------------------------------

/*! If false (default), use round-robin scheduler. If true, use multi-level
    feedback queue scheduler. Controlled by kernel command-line option
    "-o mlfqs". */
extern bool thread_mlfqs;

// ------------------------------ Structures ----------------------------------

/*! States in a thread's life cycle. */
enum thread_status {
    THREAD_RUNNING,     				/*!< Running thread. */
    THREAD_READY,       				/*!< Not running but ready to run. */
    THREAD_BLOCKED,   				    /*!< Waiting for an event to trigger */
    THREAD_DYING       				    /*!< About to be destroyed. */
};

/* File list struct. */
struct fd_element{
	int fd;
	struct file *file;
	struct dir *directory;
	char *filename;
	struct semaphore multfile_sema;
	struct list_elem  f_elem;
};

/*! A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

\verbatim
        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+
\endverbatim

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion.

   The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list.
*/
struct thread {
    /*! Owned by thread.c. */
    /**@{*/
    tid_t tid;                       	/*!< Thread identifier. */
    enum thread_status status;       	/*!< Thread state. */
    char name[16];                   	/*!< Name (for debugging purposes). */
    uint8_t *stack;                  	/*!< Saved stack pointer. */
    int priority;                    	/*!< Priority. */
    struct list_elem allelem;        	/*!< Is used for all threads list. */
    /**@}*/

    /*! Shared between thread.c and synch.c. */
    /**@{*/
    struct list_elem elem;           	/*!< List element. */
    /**@}*/

#ifdef USERPROG
    /*! Owned by userprog/process.c. */
    /**@{*/
    uint32_t *pagedir;               	/*!< Page directory. */
    int status_on_exit;					/*!< Status passed by exit call. */
    bool loaded;						/*!< True when loaded from disk. */
    struct semaphore i_am_done;			/*!< Block parent until I exit. */
    struct semaphore may_i_die; 		/*!< Allow parent to keep us blocked. */
    struct semaphore load_child; 		/*!< Lock for child loading. */
    struct thread *parent;     			/*!< Parent thread pointer. */
    struct list_elem sibling_elem;		/*!< Need so this can be in lists. */
    struct list sibling_list;			/*!< Children of my parent. */
    struct list_elem chld_elem;  		/*!< Need so this can be in lists. */
    struct list child_list;				/*!< List of children. */
    uint8_t am_child;					/*!< Flag for whether I am a child. */
    uint8_t voluntarily_exited; 		/*!< Flag for voluntary exit. */
    struct list files;					/*!< Files open in this thread. */
    struct fd_element tfile;			/*!< Process loaded from this file. */
    /**@}*/
#endif
    /*! Owned by thread.c. */
    /**@{*/
    block_sector_t cwd_sect;			/*!< Current working directory. */
    unsigned magic;                     /*!< Detects stack overflow. */
    /**@}*/
};

// ------------------------------ Prototypes ----------------------------------

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *function,
                    void *aux, uint8_t flag_child,
					struct list *parents_child_list,
					struct thread *parent);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current (void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/*! Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);

void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void thread_set_initial_thread_cwd(void);

struct fd_element *thread_get_matching_fd_elem(int fd);
bool thread_is_dir_deletable(const char *path);

#endif /* threads/thread.h */

