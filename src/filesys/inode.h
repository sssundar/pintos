#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "filesys/directory.h"
#include "list.h"
#include "bitmap.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

struct bitmap;

#define INDIRECTION_REFERENCES ( BLOCK_SECTOR_SIZE/sizeof(block_sector_t) )

/*! Temporary restriction on # of files possible in one directory entry
    so we can work on extensible files in parallel w/ subdirectories. TODO*/
#define MAX_DIR_ENTRIES 100

/*! On-disk inode.
    Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    // block_sector_t start;            /*!< First data sector. */
    off_t length;                       /*!< File size in bytes. */
    bool is_dir;						/*!< True if is a directory. */
    char filename[NAME_MAX + 1];

    /*! Sector of parent directory. Only set to not BOGUS_SECTOR for dirs. */
    block_sector_t parent_dir;

    /*! Entries in this directory. Set to BOGUS_SECTOR if they are unused. */
    block_sector_t dir_contents[MAX_DIR_ENTRIES];

    uint32_t unused[20];                /*!< Not used. */
    block_sector_t doubly_indirect;     /*!< 8 Mb reference. */
    unsigned magic;                     /*!< Magic number. */
};

/* An indirection sector referencing data or further indirection sectors */
struct indirection_block {
    block_sector_t sector[128];        
};

/*! In-memory inode. */
struct inode {
    struct list_elem elem;              /*!< Element in inode list. */
    block_sector_t sector;              /*!< Sector number of disk location. */
    int open_cnt;                       /*!< Number of openers. */
    bool removed;                       /*!< T if deleted, F otherwise. */
    int deny_write_cnt;                 /*!< 0: writes ok, >0: deny writes. */
    bool is_dir;						/*!< True if is a directory. */
    char filename[NAME_MAX + 1];		/*!< Filename for this inode. */
    struct lock ismd_lock;              /*!< Inode Struct Metadata Lock */
    struct lock extension_lock;         /*!< Extension lock */
    
    /*! Sector of parent directory. Only set to not BOGUS_SECTOR for dirs. */
    block_sector_t parent_dir;

    /*! Entries in this directory. Nulled if is not a directory. */
    block_sector_t dir_contents[MAX_DIR_ENTRIES];
};

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool is_directory,
		const char *filename, block_sector_t parent);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);
void inode_tree_destroy(block_sector_t inode_sector);

block_sector_t inode_find_matching_dir_entry(
		struct inode *tmp_inode, const char *curr_dir_name);
int inode_get_first_open_directory_slot(struct inode *the_inode);
void inode_find_matching_idx_and_sector(struct inode *directory,
		const char *name, block_sector_t *the_sector, int *the_index);

#endif /* filesys/inode.h */
