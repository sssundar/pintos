#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "list.h"

void sc_init(void);
void sys_close(int fd);

struct lock* ptr_sys_lock(void);

struct fd_element{
	int fd;
	struct file *file;
	struct list_elem  t_elem;
	struct list_elem  l_elem;
};

#endif /* userprog/syscall.h */

