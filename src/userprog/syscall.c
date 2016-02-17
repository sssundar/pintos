/*
TODO remove
ORIGINAL STUFF

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void handler(struct intr_frame *);

void init(void) {
    intr_register_int(0x30, 3, INTR_ON, handler, "syscall");
}

static void handler(struct intr_frame *f UNUSED) {
    printf("system call!\n");
    thread_exit();
}
*/

#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "lib/stdio.h"
#include "lib/kernel/stdio.h"
#include "lib/syscall-nr.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "devices/shutdown.h"

struct lock sys_lock;

static void sc_handler(struct intr_frame *);
void sc_init(void);

// TODO important: it appears that when files are opened they are added to
// THIS instead of the thread struct's file list.
struct list file_list;

struct fd_element{
	int fd;
	struct file *file;
	struct list_elem  t_elem;
	struct list_elem  l_elem;
};

void sc_init(void){
    intr_register_int(0x30, 3, INTR_ON, sc_handler, "syscall");
}

static void sc_handler(struct intr_frame *f){

	/*
    printf("system call!\n");
    thread_exit();
	*/
	int *esp;

	esp = f->esp;

	// TODO sushant has some code to handle the stack pointer

	int sc_n = *esp;
	int sc_n1 = *(esp + 1);
	int sc_n2 = *(esp + 2);
	int sc_n3 = *(esp + 3);

	if (sc_n == SYS_WRITE){
		f->eax = write(sc_n1, (void *) sc_n2, sc_n3);
	}
	else if (sc_n == SYS_OPEN){
		f->eax = open((const char *) sc_n1);
	}
	else if (sc_n == SYS_READ){
		f->eax = read(sc_n1, (void *) sc_n2, sc_n3);
	}
	else if (sc_n == SYS_FILESIZE){
		f->eax = filesize(sc_n1);
	}
	else if (sc_n == SYS_SEEK){
		seek(sc_n1, sc_n2);
	}
	else if (sc_n == SYS_TELL){
		f->eax = tell(sc_n1);
	}
	else if (sc_n == SYS_CLOSE){
		close(sc_n1);
	}
	else if (sc_n == SYS_HALT) {
		halt();
	}
	else if (sc_n == SYS_EXIT) {
		exit(sc_n1);
	}
	else if (sc_n == SYS_EXEC) {
		f->eax = exec((const char *) sc_n1);
	}
	else if (sc_n == SYS_CREATE) {
		f->eax = create((const char *) sc_n1, (unsigned) sc_n2);
	}
	else if (sc_n == SYS_REMOVE) {
		f->eax = remove((const char *) sc_n1);
	}

}

/*! Terminates Pintos by calling shutdown_power_off() (declared in
    devices/shutdown.h). This should be seldom used, because you lose some
    information about possible deadlock situations, etc.
 */
void halt (void) {
	shutdown_power_off();
}

/*! Terminates the current user program, returning status to the kernel. If
    the process's parent waits for it this is the status that will be
    returned. Conventionally, a status of 0 indicates success and nonzero
    values indicate errors.

    Closes all the open file descriptors (i.e., behaves like the Linux _exit
    function).
 */
void exit(int status) {

	struct fd_element *r;
	struct list_elem *l;
	struct thread *t = thread_current();

	lock_acquire(&sys_lock);

#ifdef USERPROG
	// TODO it's OK to call printf here?
	printf ("%s:exit(%d)\n", t->name, status);
#endif

	// Close the open file descriptors.
	// TODO migrate this to thread_exit
	for (l = list_begin(&t->files);
			 l != list_end(&t->files);
			 l = list_next(l)) {
		r = list_entry(l, struct fd_element, l_elem);
		close(r->fd);
		free(r);
	}

	thread_current()->status_on_exit = status;
	lock_release(&sys_lock);
	thread_exit();
}

/*! Runs the executable whose name is given in cmd_line, passing any given
    arguments, and returns the new process's program id (pid). Must return
    pid -1, which otherwise should not be a valid pid, if the program cannot
    load or run for any reason. Thus, the parent process cannot return from
    the exec until it knows whether the child process successfully loaded its
    executable. Uses appropriate synchronization to ensure this.
 */
pid_t exec (const char *cmd_line) {
	tid_t tid;
	lock_acquire(&sys_lock);
	tid = process_execute(cmd_line);
	lock_release(&sys_lock);
	return (pid_t) tid;
}

/*! Creates a new file called file initially initial_size bytes in size.
    Returns true if successful, false otherwise. Creating a new file does
    not open it: opening the new file is a separate operation which would
    require a open system call.
 */
bool create (const char *file, unsigned initial_size) {

	// This function does exactly the same as filesys_create, but to be
	// safe I think getting a lock is necessary as in exec.
	bool success;
	lock_acquire(&sys_lock);
	success = filesys_create(file, initial_size);
	lock_release(&sys_lock);
	return success;
}

/*! Deletes the file called file. Returns true if successful, false
    otherwise. A file may be removed regardless of whether it is open or
    closed, and removing an open file does not close it.
 */
bool remove (const char *file) {

	// This function does exactly the same as filesys_remove, but to be
	// safe I think getting a lock is necessary as in exec.
	bool success;
	lock_acquire(&sys_lock);
	success = filesys_remove(file);
	lock_release(&sys_lock);
	return success;
}

int write(int fd, const void *buffer, unsigned size){
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire(&sys_lock);
	if (fd == STDOUT_FILENO){
		putbuf(buffer, size);
		lock_release(&sys_lock);
		return size;
	}
	for (l = list_begin(&file_list);
		 l != list_end(&file_list);
		 l = list_next(l)){
		r = list_entry(l, struct fd_element, l_elem);
		if (r->fd == fd){
			f = r->file;
			lock_release(&sys_lock);
			return file_write(f, buffer, size);
		}
	}
	return -1;
}

/*
Returns a nonnegative int handle fd, or -1 if the file
could not be opened.
*/

int open(const char *file){
	struct file *f;
	struct fd_element *fd_elem;

	if(!file){
		return -1;
	}

	f = filesys_open(file);
	if(!f){
		return -1;
	}

	fd_elem = (struct fd_element *)malloc(sizeof(struct fd_element));
	fd_elem->fd = 3;
	list_push_back(&file_list, &fd_elem->l_elem);
	list_push_back(&thread_current()->files, &fd_elem->t_elem);

	return fd_elem->fd;

}

int read(int fd, void *buffer, unsigned size){
	unsigned int i;
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire (&sys_lock);
	if (fd == STDIN_FILENO){
		for (i = 0; i != size; i++){
			*(uint8_t *)(buffer + i) = input_getc();
		}
		lock_release (&sys_lock);
		return size;
	}
	else{
		for (l = list_begin(&file_list);
			 l != list_end(&file_list);
			 l = list_next(l)){
			r = list_entry(l, struct fd_element, l_elem);
			if (r->fd == fd){
				f = r->file;
				lock_release (&sys_lock);
				return file_read(f, buffer, size);
			}
		}
	}
   	lock_release (&sys_lock);
	return -1;
}

int filesize(int fd){
  struct file *f;
  struct fd_element *r;
  struct list_elem *l;

  for (l = list_begin(&file_list);
	   l != list_end(&file_list);
	   l = list_next(l)){
	  r = list_entry(l, struct fd_element, l_elem);
	  if (r->fd == fd){
		  f = r->file;
		  return file_length(f);
	  }
  }
  return -1;

}

void seek(int fd, unsigned position){
  struct file *f;
  struct fd_element *r;
  struct list_elem *l;

  for (l = list_begin(&file_list);
	   l != list_end(&file_list);
	   l = list_next(l)){
	  r = list_entry(l, struct fd_element, l_elem);
	  if (r->fd == fd){
		  f = r->file;
		  file_seek(f, position);
	  }
  }
}

unsigned tell(int fd){
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	for (l = list_begin(&file_list);
		 l != list_end(&file_list);
		 l = list_next(l)){
		r = list_entry(l, struct fd_element, l_elem);
		if (r->fd == fd){
			f = r->file;
			return file_tell(f);
		}
	}
	return -1;
}

void close(int fd){
	struct fd_element *r;
	struct fd_element *f;
	struct list_elem *l;
	struct thread *t;

	t = thread_current();

	for (l = list_begin(&t->files);
		 l != list_end(&t->files);
		 l = list_next(l)){
		r = list_entry(l, struct fd_element, t_elem);
		if (r->fd == fd){
			f = r;

			file_close(f->file);
			list_remove(&f->l_elem);
			list_remove(&f->t_elem);
			free(f);
		}
    }
}
