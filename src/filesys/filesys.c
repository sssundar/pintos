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
bool filesys_create(const char *name, off_t initial_size) {
    block_sector_t inode_sector = 0;
    
    // printf ("SDEBUG: filesys_create, filename: %s\n", name);

    struct dir *dir = dir_open_root();        

    bool success = (dir != NULL);
    if (success) {
        success = (success && free_map_allocate(1, &inode_sector));
        // printf ("SDEBUG: filesys_create, got past free_map_allocate\n");
        if (success) {
            success = (success && inode_create(inode_sector, initial_size));
            // printf ("SDEBUG: filesys_create, got past inode_create\n");
            if (success) {
                // printf("SDEBUG: in filesys_create, just before adding file to the directory.\n");
                success = (success && dir_add(dir, name, inode_sector));                
                // printf("SDEBUG: in filesys_create, just after adding file to the directory.\n");
                if (success) {
                    // Let's do something with this file! 
                } else {
                /*  ==TODO== Unless directory additions are synchronized
                    so they cannot fail, and we do them first, we would
                    need to clean up after an inode that's been created,
                    but now needs to be removed (but can't be opened as a file!)
                    as it isn't in the directory */
                    inode_tree_destroy(inode_sector);                    
                }
            } else {
                // Nothing to clean up but the inode sector itself! 
                // Inode takes care of its reference tree when it fails on
                // creation.
                inode_tree_destroy(inode_sector);    
            }
        }                                    
    }
    
    dir_close(dir);

    // if (success) 
    //     printf ("SDEBUG: filesys_create, created filename: %s at sector %u\n", name, inode_sector);    
    // else 
    //     printf ("SDEBUG: filesys_create, didn't create filename: %s\n at sector %u\n", name, inode_sector);

    return success;
}

/*! Opens the file with the given NAME.  Returns the new file if successful
    or a null pointer otherwise.  Fails if no file named NAME exists,
    or if an internal memory allocation fails. */
struct file * filesys_open(const char *name) {
    struct dir *dir = dir_open_root();
    struct inode *inode = NULL;

    if (dir != NULL) {
        dir_lookup(dir, name, &inode);
    }
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
    if (!dir_create(ROOT_DIR_SECTOR, 16))
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
				crab_into_cached_sector(rasect->sect_n, true, false), 
                    true);
		lock_acquire(&monitor_ra);
		// printf("---> I just saw block_sector_t: %u \n", rasect->sect_n);

		list_remove(l);
		free(rasect);

		lock_release(&monitor_ra);
	} while (true);

}

