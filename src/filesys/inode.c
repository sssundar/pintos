#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/*! Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/*! Returns the number of sectors to allocate for an inode SIZE
    bytes long. */
static inline size_t bytes_to_sectors(off_t size) {
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/*! Returns the block device sector that contains byte offset POS
    within INODE.
    Returns -1 if INODE does not contain data for a byte at offset
    POS. */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);

    block_sector_t result; 
    
    cache_sector_id src = crab_into_cached_sector(inode->sector, true);    
    
    struct inode_disk *data = 
        (struct inode_disk *) get_cache_sector_base_addr(src);            
    
    if (pos < data->length)
        result = data->start + pos / BLOCK_SECTOR_SIZE;
    else 
        result = -1;
    
    crab_outof_cached_sector(src, true);        

    return result;
}

/*! List of open inodes, so that opening a single inode twice
    returns the same `struct inode'. */
static struct list open_inodes;

/*! Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
}

/*! Initializes an inode with LENGTH bytes of data and
    writes the new inode to sector SECTOR on the file system
    device.

    If the file we're creating is a directory then LENGTH must be
    sizeof(block_sector_t) * ENTRY_CNT, since each directory entry
    is stored as a sector ID.

    Returns true if successful.
    Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length,
		bool is_directory, const char *filename, block_sector_t parent) {
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);
    ASSERT(filename != NULL);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        strlcpy(disk_inode->filename, filename, NAME_MAX + 1);
        if (is_directory) {
        	disk_inode->is_dir = true;
        	int i;
        	for (i = 0; i < MAX_DIR_ENTRIES; i++) {
        		disk_inode->dir_contents[i] = BOGUS_SECTOR;
        	}
        	disk_inode->parent_dir = parent;
        }
        else {
        	disk_inode->is_dir = false;
        	disk_inode->parent_dir = BOGUS_SECTOR;
        }
        if (free_map_allocate(sectors, &disk_inode->start)) {
            block_write(fs_device, sector, disk_inode);
            if (sectors > 0) {
                static char zeros[BLOCK_SECTOR_SIZE];
                size_t i;
              
                for (i = 0; i < sectors; i++) 
                    block_write(fs_device, disk_inode->start + i, zeros);
            }
            success = true; 
        }
        free(disk_inode);
    }
    return success;
}

/*! Reads an inode from SECTOR
    and returns a `struct inode' that contains it.
    Returns a null pointer if memory allocation fails. */
struct inode * inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode; 
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;

    /* Read the inode from disk to see if it's a directory. If it is then
       copy the directory's entries. */
    void *tmp_buf = malloc ((size_t) BLOCK_SECTOR_SIZE);
    if (tmp_buf == NULL) {
    	PANIC("Couldn't malloc enough room for tmp buf.");
    	NOT_REACHED();
    }
    // Fetch the node from the cache (or disk if necessary)
    cache_sector_id src = crab_into_cached_sector(inode->sector, true);
	cache_read(src, tmp_buf, 0, BLOCK_SECTOR_SIZE);
	crab_outof_cached_sector(src, true);
    // block_read(fs_device, inode->sector, tmp_buf);
    struct inode_disk *tmp_inode = (struct inode_disk *) tmp_buf;
    inode->is_dir = tmp_inode->is_dir;
    strlcpy(inode->filename, tmp_inode->filename, NAME_MAX + 1);
    if (inode->is_dir) {
		int i;
		for(i = 0; i < MAX_DIR_ENTRIES; i++) {
			inode->dir_contents[i] = tmp_inode->dir_contents[i];
		}
    }
    free (tmp_buf);

    return inode;
}

/*! Reopens and returns INODE. */
struct inode * inode_reopen(struct inode *inode) {
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/*! Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) {
    return inode->sector;
}

/*! Closes INODE and writes it to disk.
    If this was the last reference to INODE, frees its memory.
    If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode) {
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);
        
        /* Deallocate blocks if removed. */
        if (inode->removed) {

            block_sector_t start;
            off_t length; 

            cache_sector_id src = crab_into_cached_sector(inode->sector, true);        
            
            struct inode_disk *data = 
                (struct inode_disk *) get_cache_sector_base_addr(src);             



            start = data->start;
            length = data->length;

            crab_outof_cached_sector(src, true);        
            
            free_map_release(inode->sector, 1);
            free_map_release(start,
                             bytes_to_sectors(length)); 

        }
        else {
        	cache_sector_id src = crab_into_cached_sector(inode->sector, true);
			struct inode_disk *data =
				(struct inode_disk *) get_cache_sector_base_addr(src);

			// Add in the potentially changed things from the given inode
			// to the disk inode.
			strlcpy(data->filename, inode->filename, NAME_MAX + 1);
			data->is_dir = inode->is_dir;
			data->parent_dir = inode->parent_dir;
			int i;
			for (i = 0; i < MAX_DIR_ENTRIES; i++) {
				data->dir_contents[i] = inode->dir_contents[i];
			}
			cache_write(src, (void *) data, 0, BLOCK_SECTOR_SIZE);
			crab_outof_cached_sector(src, true);
        }

        free(inode); 
    }
}    

/*! Marks INODE to be deleted when it is closed by the last caller who
    has it open. */
void inode_remove(struct inode *inode) {
    ASSERT(inode != NULL);
    inode->removed = true;
}

/*! Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset) {
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    // ==TODO== REMOVE
    // uint8_t *bounce = NULL;

    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
        
        /*        
        // ==TODO== REMOVE
        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            // Read full sector directly into caller's buffer.
            block_read (fs_device, sector_idx, buffer + bytes_read);            
        }
        else {
            // Read sector into bounce buffer, then partially copy
            //    into caller's buffer.
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                    break;
            }
            block_read(fs_device, sector_idx, bounce);
            memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
        */

        cache_sector_id src = crab_into_cached_sector(sector_idx, true);            
        cache_read(src, (void *) (buffer + bytes_read), sector_ofs, chunk_size);
        crab_outof_cached_sector(src, true);
      
        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    // ==TODO== REMOVE
    // free(bounce);

    return bytes_read;
}

/*! Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
    Returns the number of bytes actually written, which may be
    less than SIZE if end of file is reached or an error occurs.
    (Normally a write at end of file would extend the inode, but
    growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset) {
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    // ==TODO== REMOVE
    // uint8_t *bounce = NULL;

    if (inode->deny_write_cnt)
        return 0;

    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
        
        /*
        // ==TODO== REMOVE
        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            // Write full sector directly to disk. 
            block_write(fs_device, sector_idx, buffer + bytes_written);
        }
        else {
            // We need a bounce buffer. 
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                    break;
            }

            // If the sector contains data before or after the chunk
            // we're writing, then we need to read in the sector
            // first.  Otherwise we start with a sector of all zeros. 

            if (sector_ofs > 0 || chunk_size < sector_left) 
                block_read(fs_device, sector_idx, bounce);
            else
                memset (bounce, 0, BLOCK_SECTOR_SIZE);

            memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
            block_write(fs_device, sector_idx, bounce);
        }
        */

        cache_sector_id dst = crab_into_cached_sector(sector_idx, false);          
        cache_write(dst, (void *) (buffer + bytes_written), sector_ofs, chunk_size);
        crab_outof_cached_sector(dst, false);
    
        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    // ==TODO== REMOVE
    // free(bounce);

    return bytes_written;
}

/*! Disables writes to INODE.
    May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode) {
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/*! Re-enables writes to INODE.
    Must be called once by each inode opener who has called
    inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode) {
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/*! Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) {
    cache_sector_id src = crab_into_cached_sector(inode->sector, true);    
    
    struct inode_disk *data = 
        (struct inode_disk *) get_cache_sector_base_addr(src);            
    
    off_t l = data->length;    
    
    crab_outof_cached_sector(src, true);        

    return l;
}

/*! Returns the index of the first open entry in the list of directory
    entries for the given directory DIR. Returns -1 if not found.
 */
int inode_get_first_open_directory_slot(struct inode *dir) {
	ASSERT(dir != NULL);
	ASSERT(dir->is_dir);

	int i;
	for(i = 0; i < MAX_DIR_ENTRIES; i++) {
		if (dir->dir_contents[i] == BOGUS_SECTOR)
			return i;
	}
	return -1;
}

void inode_find_matching_idx_and_sector(struct inode *directory,
		const char *name, block_sector_t *the_sector, int *the_index) {
	ASSERT(directory->is_dir);

	// If the name is just "." or ".." return the appropriate sector numbers.
	if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0) {
		if (thread_current()->cwd.inode == NULL) {
			PANIC("Null cwd inode.");
			NOT_REACHED();
		}
		block_sector_t rtn;
		if (strcmp(name, "..") == 0)
			rtn = thread_current()->cwd.inode->parent_dir;
		else
			rtn = thread_current()->cwd.inode->sector;
		*the_sector = rtn;
		*the_index = -1;
		return;
	}

	int i;
	// Iterate over this directory's directory entries.
	for (i = 0; i < MAX_DIR_ENTRIES; i++) {

		block_sector_t curr_sect_num = directory->dir_contents[i];
		if (curr_sect_num == BOGUS_SECTOR) {
			continue;
		}
		struct inode *curr_inode = inode_open(curr_sect_num);
		if (curr_inode == NULL) {
			PANIC("Memory alloc problem in inode_open");
			NOT_REACHED();
		}
		// Return this sector number if the filename matches.
		if(strcmp(curr_inode->filename, name) == 0) {
			inode_close(curr_inode);
			*the_sector = curr_inode->sector;
			*the_index = i;
			return;
		}
		inode_close(curr_inode);
	}

done:
	*the_sector = BOGUS_SECTOR;
	*the_index = -1;
	return;
}

/*! Gets the directory entry in the given inode with the given NAME. */
block_sector_t inode_find_matching_dir_entry(
		struct inode *directory, const char *name) {
	int idx;
	block_sector_t sect;
	inode_find_matching_idx_and_sector(directory, name, &sect, &idx);
	return sect;
}

// TODO maybe get rid of this and use given function instead...
/*! Get an inode pointer from a sector ID. Must be freed. */
struct inode_disk *inode_get_inode_from_sector(block_sector_t sect) {
	void *tmp_buf = malloc ((size_t) BLOCK_SECTOR_SIZE);
	if (tmp_buf == NULL) {
		PANIC("Couldn't malloc enough room for tmp buf.");
		NOT_REACHED();
	}
	cache_sector_id src = crab_into_cached_sector(sect, true);
	cache_read(src, tmp_buf, 0, BLOCK_SECTOR_SIZE);
	crab_outof_cached_sector(src, true);
	struct inode_disk *tmp_inode = (struct inode_disk *) tmp_buf;
	return tmp_inode;
}

// TODO Might not need this if using given function...
/*! Copies the contents of the given inode_disk into a new inode. It's the
    caller's responsiblity to free both structs eventually. */
struct inode * inode_disk_to_regular(struct inode_disk * dsk_version,
		block_sector_t sector) {
	void *tmp_buf = malloc ((size_t) BLOCK_SECTOR_SIZE);
	if (tmp_buf == NULL) {
		PANIC("Couldn't malloc enough room for tmp buf.");
		NOT_REACHED();
	}
	struct inode *tmp_inode = (struct inode *) tmp_buf;
	tmp_inode->deny_write_cnt = 1;
	strlcpy(tmp_inode->filename, dsk_version->filename, NAME_MAX + 1);
	tmp_inode->open_cnt = -1;
	tmp_inode->parent_dir = dsk_version->parent_dir;
	tmp_inode->removed = false;
	tmp_inode->sector = sector;
	tmp_inode->is_dir = dsk_version->is_dir;
	int i;
	for (i = 0; i < MAX_DIR_ENTRIES; i++) {
		tmp_inode->dir_contents[i] = dsk_version->dir_contents[i];
	}
	return tmp_inode;
}

