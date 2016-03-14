#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "filesys/cache.h"

/*! Sectors of system file inodes. @{ */
#define FREE_MAP_SECTOR 0       /*!< Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /*!< Root directory file inode sector. */
/*! @} */

/*! Each element in a list of sectors to read-ahead. */
struct ra_sect_elem {
	block_sector_t sect_n;
	struct list_elem ra_elem;
};

/*! Block device that contains the file system. */
struct block *fs_device;

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char *name, off_t initial_size);
struct file *filesys_open(const char *name);
bool filesys_remove(const char *name);

#endif /* filesys/filesys.h */

