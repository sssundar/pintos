#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/*! Used to indicate an fd match for the "thread_foreach" callback function. */
#define FUNC_SENTINEL 0xFFFFFFFF

void sc_init(void);

// TODO probably remove this. Sushant says he used it in his wait code.
struct lock* ptr_sys_lock(void);

#endif /* userprog/syscall.h */

