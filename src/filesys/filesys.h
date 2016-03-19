#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "filesys/cache.h"

// ------------------------------ Definitions ---------------------------------

/*! Sectors of system file inodes. @{ */
#define FREE_MAP_SECTOR 0        /*!< Free map file inode sector. */
#define ROOT_DIR_SECTOR 1        /*!< Root directory file inode sector. */
/*! @} */

#define BOGUS_SECTOR 0xFFFFFFFF  /*!< Non-present sector. */

// ---------------------------- Global variables ------------------------------

struct block *fs_device; 		 /*! Block device that contains file system. */

// ------------------------------ Structures ----------------------------------

/*! Each element in a list of sectors to read-ahead. */
struct ra_sect_elem {
	block_sector_t sect_n;
	struct list_elem ra_elem;
};

// ------------------------------ Prototypes ----------------------------------

void filesys_init(bool format);
void filesys_done(void);
struct file *filesys_open(const char *path);
bool filesys_remove(const char *name);
char *find_last_slash(const char *path);
bool filesys_create(const char *path, off_t initial_size,
		bool is_directory, block_sector_t parent);

#endif /* filesys/filesys.h */
