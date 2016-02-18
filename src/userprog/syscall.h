#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"

void sc_handler(struct intr_frame *);
void sys_halt(void);
void sys_exit(int status);
pid_t sys_exec(const char *file);
int sys_wait(pid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
// mapid_t sys_mmap(int fd, void *addr);
// void sys_munmap(mapid_t mapid);
// bool sys_chdir(const char *dir);
// bool sys_mkdir(const char *dir);
// bool sys_readdir(int fd, char name[READDIR_MAX_LEN + 1]);
// bool sys_isdir(int fd);
// int sys_inumber(int fd);
void sc_init(void);

struct lock* ptr_sys_lock(void);

struct fd_element{
	int fd;
	struct file *file;
	struct list_elem  t_elem;
	struct list_elem  l_elem;
};

#endif /* userprog/syscall.h */

