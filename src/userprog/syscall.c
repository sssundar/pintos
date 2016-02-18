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

//----------------------------- Global variables ------------------------------

struct lock sys_lock;

//---------------------------- Function prototypes ----------------------------

static void sc_handler(struct intr_frame *);
void sc_init(void);
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);
bool get_user_quadbyte (const uint8_t *uaddr, int *arg);

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

	int *esp = f->esp;
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
	else if (sc_n == SYS_CLOSE){
		close(sc_n1);
	}
	else if (sc_n == SYS_SEEK){
		seek(sc_n1, sc_n2);
	}
	else if (sc_n == SYS_EXIT) {
		exit(sc_n1);
	}
	else if (sc_n == SYS_HALT) {
		halt();
	}
	/*
	else if (sc_n == SYS_FILESIZE) { // TODO this hasn't been tested yet
		f->eax = filesize(sc_n1);
	}*/
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

/*
int filesize(int fd){
  struct file *f;
  struct fd_element *r;
  struct list_elem *l;

  for (l = list_begin(&thread_current()->files);
	   l != list_end(&thread_current()->files);
	   l = list_next(l)){
	  r = list_entry(l, struct fd_element, f_elem);
	  if (r->fd == fd){
		  f = r->file;
		  return file_length(f);
	  }
  }
  return -1;
}
*/

/*! Returns a nonnegative int handle fd, or -1 if the file
    could not be opened.
*/
int open(const char *file){
	struct file *f;
	struct fd_element *fd_elem;

	lock_acquire(&sys_lock);

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
	list_push_back(&thread_current()->files, &fd_elem->f_elem);

	lock_release(&sys_lock);
	return fd_elem->fd;
}

/*! Assumes that the client wants to overwrite. */
int write(int fd, const void *buffer, unsigned size) {
	struct file *f;
	struct fd_element *r;
	struct list_elem *l;

	lock_acquire(&sys_lock);
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

void close(int fd){
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

void seek(int fd, unsigned position){
    struct file *f;
    struct fd_element *r;
    struct list_elem *l;

    for (l = list_begin(&thread_current()->files);
    		l != list_end(&thread_current()->files);
    		l = list_next(l)){
    	r = list_entry(l, struct fd_element, f_elem);
    	if (r->fd == fd){
    		f = r->file;
    		file_seek(f, position);
    	}
    }
}

/*! Attempt to get a byte from a specified user virtual addres.
    Internally converts the address to a kernel virtual address.
    Returns the byte value on success as an integer, guaranteed
    positive or zero, and returns -1 on recognizing an invalid
    pointer, in which case the caller should terminate the process/thread. */
int get_user (const uint8_t *uaddr) {
    int result = -1;
    void *kaddr = pagedir_get_page(thread_current()->pagedir, uaddr);
    if ((kaddr != NULL) && is_user_vaddr(kaddr)) {
        result = (int) (*((uint8_t *) kaddr)  & ((unsigned int) 0xFF));
    }
    return result;
}

/*! Attempt to set a byte given a virtual user address. Internally converts
    the address to kernel space. Returns true on success, and false if
    the user address is invalid (this should result in thread/process
    termination. */
bool put_user (uint8_t *udst, uint8_t byte) {
    bool result = false;
    void *kdst = pagedir_get_page(thread_current()->pagedir, udst);
    if ((kdst != NULL) && is_user_vaddr(kdst)) {
        *((uint8_t *) kdst) = byte;
        result = true;
    }
    return result;
}

/*! Gets 4 consecutive user-space bytes assuming Little Endian ordering
    and returns them as an integer. */
bool get_user_quadbyte (const uint8_t *uaddr, int *arg) {
    int byte0, byte1, byte2, byte3;
    byte0 = get_user(uaddr + 0); // < 0 on failure
    byte1 = get_user(uaddr + 1);
    byte2 = get_user(uaddr + 2);
    byte3 = get_user(uaddr + 3);
    if ( (byte0 | byte1 | byte2 | byte3) >= 0 ) {
        // We're little endian, so byte0 is the LSB
        byte0 &= 0xFF;
        byte1 &= 0xFF; byte1 = byte1 << 8;
        byte2 &= 0xFF; byte2 = byte2 << 16;
        byte3 &= 0xFF; byte3 = byte3 << 24;
        *arg = byte3 | byte2 | byte1 | byte0;
        return true;
    }
    return false;
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

struct fd_element{
	int fd;
	struct file *file;
	struct list_elem  f_elem;
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

	/*
	if (sc_n == SYS_WRITE){
		f->eax = write(sc_n1, (void *) sc_n2, sc_n3);
	}
	else if (sc_n == SYS_OPEN){
		f->eax = open((const char *) sc_n1);
	}
	*/
	else if (sc_n == SYS_READ){
		f->eax = read(sc_n1, (void *) sc_n2, sc_n3);
	}
	/*
	else if (sc_n == SYS_FILESIZE){
		f->eax = filesize(sc_n1);
	}
	else if (sc_n == SYS_SEEK){
		seek(sc_n1, sc_n2);
	}
	*/
	else if (sc_n == SYS_TELL){
		f->eax = tell(sc_n1);
	}
	/*
	else if (sc_n == SYS_CLOSE){
		close(sc_n1);
	}
	else if (sc_n == SYS_HALT) {
		halt();
	}
	*/
	/*
	else if (sc_n == SYS_EXIT) {
		exit(sc_n1);
	}
	*/
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

#endif // BLURGLE
