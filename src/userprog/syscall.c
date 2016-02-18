#include "lib/user/syscall.h"
#include "userprog/syscall.h"
#include "lib/stdio.h"
#include "lib/syscall-nr.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/stdio.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "threads/malloc.h"
#include "devices/input.h"

//----------------------------- Global variables ------------------------------

struct lock sys_lock;

//---------------------------- Function prototypes ----------------------------

static void sc_handler(struct intr_frame *);
void sc_init(void);
//int get_user (const uint8_t *uaddr);
//bool put_user (uint8_t *udst, uint8_t byte);
//bool get_user_quadbyte (const uint8_t *uaddr, int *arg);
bool uptr_is_valid (const void *uptr);

//---------------------------- Function definitions ---------------------------

void sc_init(void) {
    intr_register_int(0x30, 3, INTR_ON, sc_handler, "syscall");
    lock_init(&sys_lock);
}

static void sc_handler(struct intr_frame *f UNUSED) {

	/*
	TODO remove
    printf("system call!\n");
    thread_exit();
    */

	// Don't need to run these through uptr_is_valid b/c they're generated
	// in the kernel.
	int *esp = f->esp;
	int sc_n = *esp;
	int sc_n1 = *(esp + 1);
	int sc_n2 = *(esp + 2);
	int sc_n3 = *(esp + 3);

	if (sc_n == SYS_WRITE) {
		f->eax = write(sc_n1, (void *) sc_n2, sc_n3);
	}
	else if (sc_n == SYS_OPEN) {
		f->eax = open((const char *) sc_n1);
	}
	else if (sc_n == SYS_CLOSE) {
		close(sc_n1);
	}
	else if (sc_n == SYS_SEEK) {
		seek(sc_n1, sc_n2);
	}
	else if (sc_n == SYS_EXIT) {
		exit(sc_n1);
	}
	else if (sc_n == SYS_HALT) {
		halt();
	}
	else if (sc_n == SYS_READ) {
		f->eax = read(sc_n1, (void *) sc_n2, sc_n3);
	}
	else if (sc_n == SYS_FILESIZE) {
		f->eax = filesize(sc_n1);
	}
	else if (sc_n == SYS_TELL){
		f->eax = tell(sc_n1);
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
 */
void exit(int status) {
	struct thread *t = thread_current();

	lock_acquire(&sys_lock);

#ifdef USERPROG
	// TODO it's OK to call printf here?
	printf ("%s: exit(%d)\n", t->name, status);
#endif

	thread_current()->status_on_exit = status;
	lock_release(&sys_lock);

	// Note that thread_exit closes all the open file decriptors. TODO not yet
	thread_exit();
}

/*! Returns the size, in bytes, of the file open as fd. */
int filesize(int fd){
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire(&sys_lock);

	for (l = list_begin(&thread_current()->files);
			l != list_end(&thread_current()->files);
			l = list_next(l)) {
		r = list_entry(l, struct fd_element, f_elem);
		if (r->fd == fd) {
			f = r->file;
			lock_release(&sys_lock);
			return file_length(f);
		}
	}
	lock_release(&sys_lock);
	return -1;
}

/*! Opens the file called file. Returns a nonnegative integer handle called a
    "file descriptor" (fd), or -1 if the file could not be opened. File
    descriptors numbered 0 and 1 are reserved for the console: fd 0
    (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output.
    The open system call will never return either of these file descriptors,
    which are valid as system call arguments only as explicitly described
    below.

	Each process has an independent set of file descriptors. File descriptors
	are not inherited by child processes. When a single file is opened more
    than once, whether by a single process or different processes, each open
	returns a new file descriptor. Different file descriptors for a single
	file are closed independently in separate calls to close and they do not
    share a file position.
*/
int open(const char *file){
	struct file *f;
	struct fd_element *fd_elem;

	lock_acquire(&sys_lock);
	if (!uptr_is_valid(file)) {
		lock_release(&sys_lock);
		return -1;
	}

	if (!file) {
		lock_release(&sys_lock);
		return -1;
	}

	f = filesys_open(file);
	if (!f) {
		lock_release(&sys_lock);
		return -1;
	}

	fd_elem = (struct fd_element *) malloc(sizeof(struct fd_element));
	if (fd_elem == NULL) {
		lock_release(&sys_lock);
		return -1;
	}

	fd_elem->fd = thread_current()->max_fd;
	thread_current()->max_fd++;
	fd_elem->file = f;
	list_push_back(&thread_current()->files, &fd_elem->f_elem);

	lock_release(&sys_lock);
	return fd_elem->fd;
}

/*! Writes size bytes from buffer to the open file fd. Returns the number of
    bytes actually written, which may be less than size if some bytes could
    not be written.

    Writing past end-of-file would normally extend the file, but file growth
    is not implemented by the basic file system. The expected behavior is to
    write as many bytes as possible up to end-of-file and return the actual
    number written, or 0 if no bytes could be written at all. Fd 1 writes to
    the console.
 */
int write(int fd, const void *buffer, unsigned size) {
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire(&sys_lock);

	if (!uptr_is_valid(buffer)) {
		lock_release(&sys_lock);
		return -1;
	}

	if (fd == STDOUT_FILENO){
		putbuf(buffer, size);
		lock_release(&sys_lock);
		return size;
	}

	// TODO we only tested writing to the console.
	for (l = list_begin(&(thread_current()->files));
		 l != list_end(&(thread_current()->files));
		 l = list_next(l)){
		r = list_entry(l, struct fd_element, f_elem);
		if (r->fd == fd){
			f = r->file;
			if (!f){
				lock_release(&sys_lock);
				return -1;
			}
			lock_release(&sys_lock);
			seek(fd, 0); // This is what overwrites the file. TODO NOT YET.
			return file_write(f, buffer, size);
		}
	}
	lock_release(&sys_lock);
	return -1;
}

/*! Reads size bytes from the file open as fd into buffer. Returns the number
    of bytes actually read (0 at end of file), or -1 if the file could not be
    read (due to a condition other than end of file). Fd 0 reads from the
    keyboard using input_getc().
*/
int read(int fd, void *buffer, unsigned size){
	unsigned int i;
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire (&sys_lock);

	if (!uptr_is_valid(buffer)) {
		lock_release(&sys_lock);
		return -1;
	}

	// Reading from stdin.
	if (fd == STDIN_FILENO) {
		for (i = 0; i != size; i++){
			*(uint8_t *)(buffer + i) = input_getc();
		}
		lock_release (&sys_lock);
		return size;
	}
	// Reading from other kinds of files.
	else {
		for (l = list_begin(&thread_current()->files);
				l != list_end(&thread_current()->files);
				l = list_next(l)) {
			r = list_entry(l, struct fd_element, f_elem);
			if (r->fd == fd) {
				f = r->file;
				lock_release (&sys_lock);
				seek(r->fd, 0); // I.e., start reading from beginning of file.
				return file_read(f, buffer, size);
			}
		}
	}
   	lock_release (&sys_lock);
	return -1;
}

void close(int fd) {
	struct fd_element *r;
	struct list_elem *l;
	struct thread *t;

	lock_acquire(&sys_lock);
	t = thread_current();

	for (l = list_begin(&t->files);
			l != list_end(&t->files);
			l = list_next(l)) {
		r = list_entry(l, struct fd_element, f_elem);
		if (r->fd == fd) {
			file_close(r->file);
			list_remove(&r->f_elem);
			free(r);
			break;
		}
    }

	lock_release(&sys_lock);
}

void seek(int fd, unsigned position) {
    struct file *f;
    struct fd_element *r;
    struct list_elem *l;

    lock_acquire(&sys_lock);
    for (l = list_begin(&thread_current()->files);
    		l != list_end(&thread_current()->files);
    		l = list_next(l)) {
    	r = list_entry(l, struct fd_element, f_elem);
    	if (r->fd == fd){
    		f = r->file;
    		file_seek(f, position);
    	}
    }
    lock_release(&sys_lock);
}

/*! Returns the position of the next byte to be read or written in open file
    fd, expressed in bytes from the beginning of the file.
 */
unsigned tell(int fd){
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire(&sys_lock);
	for (l = list_begin(&thread_current()->files);
			l != list_end(&thread_current()->files);
			l = list_next(l)){
		r = list_entry(l, struct fd_element, f_elem);
		if (r->fd == fd){
			f = r->file;
			lock_release(&sys_lock);
			return file_tell(f);
		}
	}
	lock_release(&sys_lock);
	return -1;
}



/*! True if the given pointer is less than PHYSBASE, is not null, and is a
    user address one.
 */
bool uptr_is_valid (const void *uptr) {
	return uptr != NULL && is_user_vaddr(uptr) &&
			pagedir_get_page(thread_current()->pagedir, uptr) != NULL;
}

#ifdef BLURGLE

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

//----------------------------- Global variables ------------------------------

struct lock sys_lock;

// TODO important: it appears that when files are opened they are added to
// THIS instead of the thread struct's file list.
struct list file_list;

//---------------------------- Function prototypes ----------------------------

static void sc_handler(struct intr_frame *);
void sc_init(void);

//---------------------------- Function definitions ---------------------------

struct fd_element {
	int fd;
	struct file *file;
	struct list_elem  f_elem;
	struct list_elem  l_elem;
};

void sc_init(void) {
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

	/*
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
	*/
	else if (sc_n == SYS_EXEC) {
		f->eax = exec((const char *) sc_n1);
	}
	/*
	else if (sc_n == SYS_CREATE) {
		f->eax = create((const char *) sc_n1, (unsigned) sc_n2);
	}
	else if (sc_n == SYS_REMOVE) {
		f->eax = remove((const char *) sc_n1);
	}
	*/

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

#endif // BLURGLE
