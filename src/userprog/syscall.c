#include "debug.h"
#include "lib/user/syscall.h"
#include "userprog/syscall.h"
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
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "devices/shutdown.h"

struct lock sys_lock;

static void sc_handler(struct intr_frame *f);
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
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);
bool get_user_quadbyte (const uint8_t *uaddr, int *arg);
struct lock* ptr_sys_lock(void);
// mapid_t sys_mmap(int fd, void *addr);
// void sys_munmap(mapid_t mapid);
// bool sys_chdir(const char *dir);
// bool sys_mkdir(const char *dir);
// bool sys_readdir(int fd, char name[READDIR_MAX_LEN + 1]);
// bool sys_isdir(int fd);
// int sys_inumber(int fd);

struct lock* ptr_sys_lock(void) {
    return &sys_lock;
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

// TODO important: it appears that when files are opened they are added to
// THIS instead of the thread struct's file list.
struct list file_list;

void sc_init(void){
    intr_register_int(0x30, 3, INTR_ON, sc_handler, "syscall");
}


/*! Use the interrupt frame to get the calling user program's stack pointer.
    Use a case block to retrieve the proper number of arguments from the stack.    
    We have to get these one byte at a time because we have no idea whether
    we're crossing pages as we do this.

    The GDT is irrelevant, as all segments are zeroed.
    We can use get_user to retrieve the arguments safely.
    
    Once all arguments are stored, another case block calls the appropriate 
    handler and retrieves its return value, casts it as an integer without
    sign extension, sets the interrupt frame's eax to this value if appropriate.

    System calls are responsible for their own error handling.
    */
static void sc_handler(struct intr_frame *f) {    
    /* We are guaranteed to have one argument, the call number. */    
    int number; 
    int arg1, arg2, arg3;
    if (!get_user_quadbyte((const uint8_t *) f->esp, &number)) { sys_exit(-1); }    
    switch (number) {
        /* Three Additional Arguments */
        case SYS_READ:
        case SYS_WRITE:
            if (!get_user_quadbyte((const uint8_t *) (f->esp + 3), &arg3)) { sys_exit(-1); }            

        /* Two Additional Arguments */
        case SYS_CREATE:
        case SYS_SEEK:
        //case SYS_MMAP:
        //case SYS_READDIR:
            if (!get_user_quadbyte((const uint8_t *) (f->esp + 2), &arg2)) { sys_exit(-1); }            
        
        /* One Additional Argument */
        case SYS_EXIT:
        case SYS_EXEC:
        case SYS_WAIT:
        case SYS_REMOVE:
        case SYS_OPEN:
        case SYS_FILESIZE:
        case SYS_TELL:
        case SYS_CLOSE:
        //case SYS_MUNMAP:
        //case SYS_CHDIR:
        //case SYS_MKDIR:
        //case SYS_ISDIR:
        //case SYS_INUMBER:
            if (!get_user_quadbyte((const uint8_t *) (f->esp + 1), &arg1)) { sys_exit(-1); }            
        
        /* Zero Additional Arguments */
        case SYS_HALT:
            break;

        default:
            /* NO DEE-DEE DO NOT PUSH THAT BUTTON... */
            ASSERT(false);
            break;
    }

    switch (number) {        
        case SYS_READ:
            f->eax = (uint32_t) sys_read((int) arg1, (void *) arg2, 
                (unsigned) arg3);
            break;

        case SYS_WRITE:
            f->eax = (uint32_t) sys_write((int) arg1, (const void *) arg2, 
                (unsigned) arg3);
            break;
        
        case SYS_CREATE:
            f->eax = (uint32_t) sys_create((const char *) arg1, (unsigned) arg2);
            break;

        case SYS_SEEK:
            sys_seek((int) arg1, (unsigned) arg2);
            break;

        // case SYS_MMAP:
        //     f->eax = (uint32_t) sys_mmap((int) arg1, (void *) arg2);
        //     break;

        // case SYS_READDIR:
        //     f->eax = (uint32_t) sys_readdir((int) arg1, 
        //             (char (*)[READDIR_MAX_LEN + 1]) arg2);          
        //     break;
        
        /* One Additional Argument */
        case SYS_EXIT:
            sys_exit((int) arg1);
            break;

        case SYS_EXEC:
            f->eax = (uint32_t) sys_exec((const char *) arg1);
            break;

        case SYS_WAIT:
            f->eax = (uint32_t) sys_wait((pid_t) arg1);
            break;
        
        case SYS_REMOVE:
            f->eax = (uint32_t) sys_remove ((const char *) arg1);
            break;

        case SYS_OPEN:
            f->eax = (uint32_t) sys_open ((const char *) arg1);
            break;

        case SYS_FILESIZE:
            f->eax = (uint32_t) sys_filesize((int) arg1);
            break;

        case SYS_TELL:
            f->eax = (uint32_t) sys_tell((int) arg1);
            break;

        case SYS_CLOSE:
            sys_close((int) arg1);
            break;

        // case SYS_MUNMAP:
        //     sys_munmap((mapid_t) arg1);
        //     break;

        // case SYS_CHDIR:
        //     f->eax = (uint32_t) sys_chdir((const char *) arg1);
        //     break;

        // case SYS_MKDIR:
        //     f->eax = (uint32_t) sys_mkdir((const char *) arg1);
        //     break;

        // case SYS_ISDIR:
        //     f->eax = (uint32_t) sys_isdir((int) arg1);
        //     break;

        // case SYS_INUMBER:
        //     f->eax = (uint32_t) sys_inumber((int) arg1);            
        //     break;
                
        case SYS_HALT:
            sys_halt();
            break;

        default:            
            ASSERT(false);
            break;
    }
}

int sys_wait(pid_t pid) {
    return process_wait((tid_t) pid);
}


/*! Terminates Pintos by calling shutdown_power_off() (declared in
    devices/shutdown.h). This should be seldom used, because you lose some
    information about possible deadlock situations, etc.
 */
void sys_halt (void) {
	shutdown_power_off();
}

/*! Terminates the current user program, returning status to the kernel. If
    the process's parent waits for it this is the status that will be
    returned. Conventionally, a status of 0 indicates success and nonzero
    values indicate errors.

    Closes all the open file descriptors (i.e., behaves like the Linux _exit
    function).
 */
void sys_exit(int status) {	
    struct thread *t = thread_current();

	t->status_on_exit = status;

#ifdef USERPROG
    // TODO it's OK to call printf here?
    printf ("%s:exit(%d)\n", t->name, status);
#endif

    struct list_elem *elem;        
    struct thread *mychild;
    enum intr_level old_level;    

    /*  Am I a process? Yes, and I'm calling this, exiting normally, not being terminated. */
    /*  I can be both a child and a parent */
    
    /* I might be a parent process. Orphan any children. */    
    /*  I'm clearly not in my children's sema-block lists, so just sema_up their may_i_dies and
        disabling interrupts and unflagging their am_child, and unlinking my child list. 
        This call CAN be interrupted by something trying to terminate the parent, hence this is 
        critical code. Then re-enable interrupts and proceed normally. */
    
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

    /*  Do not release files here; sometimes processes could be terminated by an external source and
        that means that source is responsible for cleaning up after us. */    
    t->voluntarily_exited = 1;
    if (t->am_child > 0) {
        /* Am I a child process? Then don't kill me just yet, I might be needed later. */
        sema_up(&t->i_am_done);
        sema_down(&t->may_i_die);        
    } 
    
    /* Proceed as normal */
    thread_exit();
}




/*! Runs the executable whose name is given in cmd_line, passing any given
    arguments, and returns the new process's program id (pid). Must return
    pid -1, which otherwise should not be a valid pid, if the program cannot
    load or run for any reason. Thus, the parent process cannot return from
    the exec until it knows whether the child process successfully loaded its
    executable. Uses appropriate synchronization to ensure this.
 */
pid_t sys_exec (const char *cmd_line) {
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
bool sys_create (const char *file, unsigned initial_size) {

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
bool sys_remove (const char *file) {

	// This function does exactly the same as filesys_remove, but to be
	// safe I think getting a lock is necessary as in exec.
	bool success;
	lock_acquire(&sys_lock);
	success = filesys_remove(file);
	lock_release(&sys_lock);
	return success;
}

int sys_write(int fd, const void *buffer, unsigned size){
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

int sys_open(const char *file){
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

int sys_read(int fd, void *buffer, unsigned size){
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

int sys_filesize(int fd){
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

void sys_seek(int fd, unsigned position){
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

unsigned sys_tell(int fd){
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

void sys_close(int fd){
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


// mapid_t sys_mmap(int fd, void *addr) {
//     return -1;
// }

// void sys_munmap(mapid_t mapid) {    
// }

// bool sys_chdir(const char *dir) {
//     return false;
// }

// bool sys_mkdir(const char *dir) {
//     return false;
// }

// bool sys_readdir(int fd, char name[READDIR_MAX_LEN + 1]) {
//     return false;
// }

// bool sys_isdir(int fd) {
//     return false;
// }

// int sys_inumber(int fd) {
//     return -1;
// }