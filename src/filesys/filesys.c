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

/*! Creates a file named NAME with the given INITIAL_SIZE.  Returns true if
    successful, false otherwise.  Fails if a file named NAME already exists,
    or if internal memory allocation fails. */
bool filesys_create(const char *path, off_t initial_size,
		bool is_directory, block_sector_t parent) {
    block_sector_t inode_sector = 0;

    // TODO JUST ADDED
    // Get the filename at the end of the path and make sure it's not a
	// reserved name like "." or ".."
	char name_at_end[NAME_MAX + 1];
	char *prefix_dir = (char *) malloc (sizeof(char) * strlen(path));
	bool no_prefix_dir = false;
	char *last_slash = find_last_slash(path);
	if (last_slash == NULL) {
		no_prefix_dir = true;
		strlcpy(name_at_end, path, NAME_MAX);
	}
	else {
		strlcpy(name_at_end, last_slash + 1,
				(path + strlen(path)) - last_slash);
		strlcpy(prefix_dir, path, last_slash - path + 1);
	}

	// If there IS a prefix directory...
	struct inode *dir_inode = thread_current()->cwd.inode;
	if (!no_prefix_dir) {
		dir_inode = dir_get_inode_from_path(prefix_dir);
		if (dir_inode == NULL) {
			PANIC("Couldn't find predfix directory!");
			NOT_REACHED();
		}
	}
	free (prefix_dir);
	// TODO END JUST ADDED

	//printf("--> IN FILESYS_CREATE thread current name=%s\n", thread_current()->name);
	//printf("--> IN FILESYS_CREATE thread current cwd sector=%u\n", thread_current()->cwd.inode->sector);


    struct dir *dir = dir_open(dir_inode);
    bool success = (dir != NULL &&
                    free_map_allocate(1, &inode_sector) &&
                    inode_create(inode_sector,
                    		initial_size,
                    		is_directory,
							name_at_end,
							is_directory ? parent : BOGUS_SECTOR) &&
                    dir_add(dir, name_at_end, inode_sector));
    if (!success && inode_sector != 0) 
        free_map_release(inode_sector, 1);

    // Only close the directory if WE opened one. That is, don't close an
    // inode that belongs to the current thread's CWD!
    if (!no_prefix_dir)
    	dir_close(dir);

    return success;
}

/*! Opens the file with the given NAME.  Returns the new file if successful
    or a null pointer otherwise.  Fails if no file named NAME exists,
    or if an internal memory allocation fails. */
struct file * filesys_open(const char *path) {

	//printf("--> path is \"%s\"\n", path);
	//printf("--> thread current name=%s\n", thread_current()->name);
	//printf("--> thread current cwd sector=%u\n", thread_current()->cwd.inode->sector);


	// TODO JUST ADDED
    // Get the filename at the end of the path and make sure it's not a
	// reserved name like "." or ".."
	char name_at_end[NAME_MAX + 1];
	char *prefix_dir = (char *) calloc (1, sizeof(char) * strlen(path));
	bool no_prefix_dir = false;
	char *last_slash = find_last_slash(path);
	if (last_slash == NULL) {
		no_prefix_dir = true;
		strlcpy(name_at_end, path, NAME_MAX);
	}
	else {
		strlcpy(name_at_end, last_slash + 1,
				(path + strlen(path)) - last_slash);
		strlcpy(prefix_dir, path, last_slash - path + 1);
	}

	// If there IS a prefix directory...
	struct inode *dir_inode = thread_current()->cwd.inode;
	if (!no_prefix_dir) {
		dir_inode = dir_get_inode_from_path(prefix_dir);
		if (dir_inode == NULL) {
			PANIC("Couldn't find predfix directory!");
			NOT_REACHED();
		}
	}

	//printf("--> name_at_end is \"%s\"\n", name_at_end);
	//printf("--> prefix is \"%s\"\n", prefix_dir);
	//printf("--> inode sector=%u, inode parent is %u\n", dir_inode->sector, dir_inode->parent_dir);

	free (prefix_dir);
	// TODO END JUST ADDED



    struct dir *dir = dir_open(dir_inode);
    struct inode *inode = NULL;

    if (dir != NULL) {        
        dir_lookup(dir, name_at_end, &inode);
    }

    // Only close the directory if WE opened one. That is, don't close an
    // inode that belongs to the current thread's CWD!
    if (!no_prefix_dir)
    	dir_close(dir);

    return file_open(inode);
}

/*! Deletes the file named NAME.  Returns true if successful, false on failure.
    Fails if no file named NAME exists, or if an internal memory allocation
    fails. */
bool filesys_remove(const char *name) {
    struct dir *dir = dir_open_root();
    bool success = dir != NULL && dir_remove(dir, name);
    dir_close(dir);

    return success;
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
