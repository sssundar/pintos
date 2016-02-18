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
#include "userprog/process.h"

//----------------------------- Global variables ------------------------------

struct lock sys_lock;

//---------------------------- Function prototypes ----------------------------

static void sc_handler(struct intr_frame *);
void sc_init(void);
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);
bool get_user_quadbyte (const uint8_t *uaddr, int *arg);
bool uptr_is_valid (const void *uptr);

//---------------------------- Function definitions ---------------------------

/*! Attempt to get a byte from a specified user virtual addres.
    Internally converts the address to a kernel virtual address.    
    Returns the byte value on success as an integer, guaranteed
    positive or zero, and returns -1 on recognizing an invalid
    pointer, in which case the caller should terminate the process/thread. */
int get_user (const uint8_t *uaddr) {    
    int result = -1;
    void *kaddr = pagedir_get_page(thread_current()->pagedir, uaddr);
    if ((kaddr != NULL) && is_user_vaddr(uaddr)) {
        result = (int) ( *((uint8_t *) kaddr) & ((unsigned int) 0xFF));        
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
    if ((kdst != NULL) && is_user_vaddr(udst)) {
        *((uint8_t *) udst) = byte;
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


void sc_init(void) {
    intr_register_int(0x30, 3, INTR_ON, sc_handler, "syscall");
    lock_init(&sys_lock);
}

static void sc_handler(struct intr_frame *f) {

	// Don't need to run these through uptr_is_valid b/c they're generated
	// in the kernel.
	// int *esp = f->esp;
	int sc_n, sc_n1, sc_n2, sc_n3;
	// sc_n = *esp;
	// sc_n1 = *(esp + 1);
	// sc_n2 = *(esp + 2);
	// sc_n3 = *(esp + 3);
    get_user_quadbyte ((const uint8_t *) f->esp, &sc_n);
    get_user_quadbyte ((const uint8_t *) (f->esp+4), &sc_n1);
    get_user_quadbyte ((const uint8_t *) (f->esp+8), &sc_n2);
    get_user_quadbyte ((const uint8_t *) (f->esp+12), &sc_n3);

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
	else if (sc_n == SYS_EXEC) {
		f->eax = exec((const char *) sc_n1);
	} 
	else if (sc_n == SYS_WAIT) {
		f->eax = wait((pid_t) sc_n1);
	}
}

/*! Terminates Pintos by calling shutdown_power_off() (declared in
    devices/shutdown.h). This should be seldom used, because you lose some
    information about possible deadlock situations, etc.
 */
void halt (void) {
	shutdown_power_off();
}

int wait(pid_t p) {
	return process_wait(p);
}

/*! Terminates the current user program, returning status to the kernel. If
    the process's parent waits for it this is the status that will be
    returned. Conventionally, a status of 0 indicates success and nonzero
    values indicate errors.

    Closes all the open file descriptors (i.e., behaves like the Linux _exit
    function).
 */
void exit(int status) {
    struct thread *t = thread_current();
    lock_acquire(&sys_lock);

#ifdef USERPROG
    printf ("%s: exit(%d)\n", t->name, status);
#endif

    struct list_elem *elem;        
    struct thread *mychild;
    enum intr_level old_level;    

    /*  Am I a process? Yes, and I'm calling this, exiting normally, not being
        terminated. */

    /*  I can be both a child and a parent */
    
    /*  I might be a parent process. Orphan any children. */

    /*  I'm clearly not in my children's sema-block lists, so just sema_up
        their may_i_dies and disabling interrupts and unflagging their
        am_child, and unlinking my child list. This call CAN be interrupted
        by something trying to terminate the parent, hence this is critical
        code. Then re-enable interrupts and proceed normally. */
    
    old_level = intr_disable();        
    elem = list_begin(&t->child_list);
    while (elem != list_end(&t->child_list)) {        
        mychild = list_entry(elem, struct thread, sibling_list);            
        mychild->am_child = 0;
        sema_up(&mychild->may_i_die);
        elem = list_next(elem);           
        list_remove(elem->prev);            
    }                
    intr_set_level(old_level);        

    /*  Do not release files here; sometimes processes could be terminated by
        an external source and that means that source is responsible for
        cleaning up after us. */
    t->voluntarily_exited = 1;
    t->status_on_exit = status;
    lock_release(&sys_lock);

    if (t->am_child > 0) {
        /* Am I a child process? Then don't kill me just yet, I might be
           needed later. */
        sema_up(&t->i_am_done);
        sema_down(&t->may_i_die);        
    } 
    
    /* Proceed as normal */
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
		exit(-1);
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
		exit(-1);
	}

	if (fd == STDOUT_FILENO){
		putbuf(buffer, size);
		lock_release(&sys_lock);
		return size;
	}

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
			seek(fd, 0); // This is what overwrites the file.
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

	if (!uptr_is_valid(buffer) || fd < 0) {
		lock_release(&sys_lock);
		exit(-1);
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

/*! Creates a new file called file initially initial_size bytes in size.
    Returns true if successful, false otherwise. Creating a new file does
    not open it: opening the new file is a separate operation which would
    require a open system call.
 */
bool create (const char *file, unsigned initial_size) {

	bool success = false;
	lock_acquire(&sys_lock);

	if (!uptr_is_valid(file)) {
		lock_release(&sys_lock);
		exit(-1);
	}

	success = filesys_create(file, initial_size);
	lock_release(&sys_lock);
	return success;
}

/*! Deletes the file called file. Returns true if successful, false
    otherwise. A file may be removed regardless of whether it is open or
    closed, and removing an open file does not close it.
 */
bool remove (const char *file) {

	bool success = false;
	lock_acquire(&sys_lock);

	if (!uptr_is_valid(file)) {
		lock_release(&sys_lock);
		exit(-1);
	}

	// This function does exactly the same as filesys_remove, but to be
	// safe I think getting a lock is necessary as in exec.
	success = filesys_remove(file);
	lock_release(&sys_lock);
	return success;
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

	if (!uptr_is_valid(cmd_line)) {
		lock_release(&sys_lock);
		exit(-1);
	}
	lock_release(&sys_lock);
	tid = process_execute(cmd_line);
	return (pid_t) tid;
}

/*! True if the given pointer is less than PHYSBASE, is not null, and is a
    user address one.
 */
bool uptr_is_valid (const void *uptr) {
	return uptr != NULL && is_user_vaddr(uptr) &&
			pagedir_get_page(thread_current()->pagedir, uptr) != NULL;
}
