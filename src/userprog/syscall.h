#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void sc_init(void);

// TODO probably remove this. Sushant says he used it in his wait code.
struct lock* ptr_sys_lock(void);

#endif /* userprog/syscall.h */

