#include "filesys/free-map.h"
#include <bitmap.h>

#include <debug.h>
#include <stdio.h>
#include <string.h>

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/synch.h"

static struct file *free_map_file;   /*!< Free map file. */
static struct bitmap *free_map;      /*!< Free map, one bit per sector. */
static struct lock free_map_lock;

/*! Initializes the free map. */
void free_map_init(void) {
    lock_init(&free_map_lock);    
    free_map = bitmap_create(block_size(fs_device));
    if (free_map == NULL)
        PANIC("bitmap creation failed--file system device is too large");
    bitmap_mark(free_map, FREE_MAP_SECTOR);
    bitmap_mark(free_map, ROOT_DIR_SECTOR);
}

/*! Allocates CNT consecutive sectors from the free map and stores the first
    into *SECTORP.

    Returns true if successful, false if not enough consecutive sectors were
    available or if the free_map file could not be written. */
bool free_map_allocate(size_t cnt, block_sector_t *sectorp) {
    
    /* Require this for simpler file extension */
    ASSERT(cnt == 1);

    lock_acquire(&free_map_lock);    
    block_sector_t sector = bitmap_scan_and_flip(free_map, 0, cnt, false);

    // printf("SDEBUG: in free_map_allocate, just allocated sector, cnt in memory: %u, %u\n", sector, cnt);
    
    if (sector != BITMAP_ERROR && free_map_file != NULL &&
        !bitmap_write(free_map, free_map_file)) {

        // ==TODO== Move bitmap writes on the free-map to write-behind         
        sector = BITMAP_ERROR;
    }
    if (sector != BITMAP_ERROR) {
        *sectorp = sector;
        // printf("SDEBUG: in free_map_allocate, just allocated sector, cnt on disk: %u, %u\n", sector, cnt);
    }
    lock_release(&free_map_lock);

    return sector != BITMAP_ERROR;
}

/*! Makes CNT sectors starting at SECTOR available for use. */
void free_map_release(block_sector_t sector, size_t cnt) {
    lock_acquire(&free_map_lock);
    ASSERT(bitmap_all(free_map, sector, cnt));
    bitmap_set_multiple(free_map, sector, cnt, false);
    bitmap_write(free_map, free_map_file); //==TODO== Currently assumed to work

    // printf("SDEBUG: in free_map_release, just released sector, cnt : %u, %u\n", sector, cnt);
    
    lock_release(&free_map_lock);    
}

/*! Opens the free map file and reads it from disk. */
void free_map_open(void) {
    lock_acquire(&free_map_lock);
    free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
    if (free_map_file == NULL)
        PANIC("can't open free map");
    if (!bitmap_read(free_map, free_map_file))
        PANIC("can't read free map");
    lock_release(&free_map_lock);
}

/*! Writes the free map to disk and closes the free map file. */
void free_map_close(void) {
    file_close(free_map_file);
}

/*! Creates a new free map file on disk and writes the free map to it. */
void free_map_create(void) {
    /* Create inode. */    

    // printf("SDEBUG: free_map_create byte size: %u\n",bitmap_file_size(free_map));

    if (!inode_create(FREE_MAP_SECTOR, bitmap_file_size(free_map)))
        PANIC("free map creation failed");

    /* Write bitmap to file. */
    free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
    if (free_map_file == NULL)
        PANIC("can't open free map");

    lock_acquire(&free_map_lock);
    if (!bitmap_write(free_map, free_map_file))
        PANIC("can't write free map");
    lock_release(&free_map_lock);    
}

