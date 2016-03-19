#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/user/syscall.h"
#include "threads/thread.h"

struct lock monitor_ra;			   /*!< Used to wake up read-ahead. */
struct condition cond_ra;          /*!< Used to wake up read-ahead. */

struct semaphore crude_time;       /*!< Downed here, upped in thread_tick */
extern bool timer_initd;

/*! List of the sectors N whose next elements N + 1 should be read ahead. */
struct list ra_sectors;

long long total_ticks;             /*!< Crude timer's tick count. */

/*! Partition that contains the file system. */
struct block *fs_device;

static void do_format(void);
void write_behind_func(void *aux);
void read_ahead_func(void *aux);

/*! Initializes the file system module.
    If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
    fs_device = block_get_role(BLOCK_FILESYS);
    if (fs_device == NULL)
        PANIC("No file system device found, can't initialize file system.");

    inode_init();
    file_cache_init(); 
    free_map_init();    

    // Initialize the read-ahead and write-behind helper threads.
    list_init(&ra_sectors);
    lock_init(&monitor_ra);
    cond_init(&cond_ra);
    sema_init(&crude_time, 0);
    total_ticks = 0;

    thread_create("write-behind", PRI_DEFAULT, write_behind_func, NULL, 1,
    		&thread_current()->child_list, thread_current());

    thread_create("read-ahead", PRI_DEFAULT, read_ahead_func, NULL, 1,
        	&thread_current()->child_list, thread_current());

    timer_initd = true;

    if (format)
        do_format();

    free_map_open();
}

/*! Shuts down the file system module, writing any unwritten data to disk. */
void filesys_done(void) {
	flush_cache_to_disk();
    free_map_close();
}

static int get_filename_length(const char *path) {
	char *last_slash = strrchr(path, '/');
	if (last_slash == NULL)
		return strlen(path);
	return strlen(last_slash + 1);
}

/*! Creates a file named NAME with the given INITIAL_SIZE.  Returns true if
    successful, false otherwise.  Fails if a file named NAME already exists,
    or if internal memory allocation fails. */
bool filesys_create(const char *path, off_t initial_size,
		bool is_directory, block_sector_t parent) {

	if (get_filename_length(path) > NAME_MAX)
		return false;

    block_sector_t inode_sector = 0;
    char filename[NAME_MAX + 1];
	struct inode *parent_inode;
	struct inode *inode =
			dir_get_inode_from_path(path, &parent_inode, filename);
	if (parent_inode == NULL) {
		PANIC("Couldn't find prefix directory!");
		NOT_REACHED();
	}
	if (inode != NULL)
		return false;

	//printf("--> IN FILESYS_CREATE thread current name=%s\n", thread_current()->name);
	//printf("--> IN FILESYS_CREATE thread current cwd sector=%u\n", thread_current()->cwd.inode->sector);

	struct dir dir_static;
	dir_static.inode = parent_inode == NULL ?
			thread_current()->cwd.inode : parent_inode;
	dir_static.pos = 0;
    bool success = (dir_static.inode != NULL &&
    				free_map_allocate(1, &inode_sector) &&
                    inode_create(inode_sector,
                    		initial_size,
                    		is_directory,
							filename,
							is_directory ? parent : BOGUS_SECTOR) &&
                    dir_add(&dir_static, filename, inode_sector));
    if (!success && inode_sector != 0)
        free_map_release(inode_sector, 1);

    return success;
}

/*! Opens the file with the given NAME.  Returns the new file if successful
    or a null pointer otherwise.  Fails if no file named NAME exists,
    or if an internal memory allocation fails. */
struct file * filesys_open(const char *path) {

	//printf("--> path is \"%s\"\n", path);
	//printf("--> thread_curr name = %s\n", thread_current()->name);
	//printf("--> thread_curr cwd sector = %u\n", thread_current()->cwd.inode->sector);
	//printf("--> thread_curr cwd name = \"%s\"\n", thread_current()->cwd.inode->filename);

    char filename[NAME_MAX + 1];
	struct inode *parent_inode;
	struct inode *dir_inode =
			dir_get_inode_from_path(path, &parent_inode, filename);
	if (dir_inode == NULL) {

		//printf("--> got a null dir_inode\n");

		return NULL;
	}

	//printf("--> filename is \"%s\"\n", filename);
	//printf("--> inode sector=%u, inode parent is %u, parent inode = %p\n",
	//		dir_inode->sector, dir_inode->parent_dir, parent_inode);

	// Only case where parent is NULL is for root directory. Already got it,
	// so return it
	if (parent_inode == NULL)
		return file_open(dir_inode);

	struct dir dir_static;
	dir_static.inode = parent_inode;
	dir_static.pos = 0;
    struct inode *inode = NULL;
    dir_lookup(&dir_static, filename, &inode);

    //printf("--> inode is %p\n", inode);

    return file_open(inode);
}

/*! Deletes the file named NAME.  Returns true if successful, false on failure.
    Fails if no file named NAME exists, or if an internal memory allocation
    fails.

    Also returns false if the directory contains files, if some process has
    the directory open, or if it's the current working directory of some
    process. */
bool filesys_remove(const char *name) {

	//printf("--> check if some process is using \"%s\"\n", name);

	if (thread_is_some_process_using_dir(name))
		return false;

	//printf("--> no process is using \"%s\"\n", name);

	char filename[NAME_MAX + 1];
	struct inode *parent_inode;
	struct inode *dir_inode =
			dir_get_inode_from_path(name, &parent_inode, filename);
	if (dir_inode == NULL) // Can't delete non-existent file.
		return false;

	//printf("--> IN FILESYS filename is \"%s\"\n", filename);
	//printf("--> IN FILESYS inode sector=%u, inode parent is %u\n",
	//		dir_inode->sector, dir_inode->parent_dir);

	struct dir parent_dir;
	parent_dir.inode = parent_inode;
	parent_dir.pos = 0;
	dir_remove(&parent_dir, filename);

    return true;
}

/*! Formats the file system. */
static void do_format(void) {
    printf("Formatting file system...");
    free_map_create();
    if (!dir_create(ROOT_DIR_SECTOR, 16, "", BOGUS_SECTOR))
        PANIC("root directory creation failed");
    free_map_close();
    printf("done.\n");
}

/*! Iterate over all the cache entries periodically, writing the dirty ones
    back to disk to protect against a system crash. The timing is done with
    a condition variable that is signalling from thread_tick.
 */
void write_behind_func(void *aux UNUSED) {
	do {
		sema_down(&crude_time); // Wait.
		flush_cache_to_disk();
	} while (true);
}

/*! Whenever a sector is read in from disk, the next one to read is queued up
    in ra_sectors and this thread is woken up. This thread is responsible for
    reading in that sector in the background. */
void read_ahead_func(void *aux UNUSED) {

	do {
		lock_acquire(&monitor_ra);
		while(list_size(&ra_sectors) == 0) {
			cond_wait(&cond_ra, &monitor_ra);
		}

		struct list_elem *l = list_pop_front(&ra_sectors);
		struct ra_sect_elem *rasect =
				list_entry(l, struct ra_sect_elem, ra_elem);

		// Read in the next sector from disk.
		lock_release(&monitor_ra);
		crab_outof_cached_sector(
				crab_into_cached_sector(rasect->sect_n, true), true);
		lock_acquire(&monitor_ra);
		// printf("---> I just saw block_sector_t: %u \n", rasect->sect_n);

		list_remove(l);
		free(rasect);

		lock_release(&monitor_ra);
	} while (true);

}

/*! Gets the last slash that isn't the very last char. */
char *find_last_slash(const char *path) {
	int i;
	int len = strlen(path);
	if (len <= 1)
		return NULL;
	for (i = len - 2; i >= 0; i--) {
		if (path[i] == '/')
			return (char *) (path + i);
	}
	return NULL;
}
