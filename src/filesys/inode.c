#include "filesys/inode.h"
#include <list.h>

#include <debug.h>
#include <stdio.h>
#include <string.h>

#include <round.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/*! Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static void get_indirection_indices(uint32_t *base_first_index, 
                                    uint32_t *base_second_index, 
                                    uint32_t *final_first_index, 
                                    uint32_t *final_second_index, 
                                    off_t current_length, 
                                    off_t final_length);

static bool inode_extend(   bool create_double_indirection, 
                            block_sector_t* doubly_indirect_, 
                            off_t current_length, 
                            off_t future_length);

/*! On-disk inode.
    Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {    
    off_t length;                       /*!< File size in bytes. */
    unsigned magic;                     /*!< Magic number. */

    /* Branching out to data, or INDIRECTION_BRANCH references */
    block_sector_t ignore[125];         /* Unused */
    block_sector_t doubly_indirect;     /* 8 Mb reference */
};

/* An indirection sector referencing data or further indirection sectors */
struct indirection_block {
    block_sector_t sector[128];        
}

/*! In-memory inode. */
struct inode {
    struct list_elem elem;              /*!< Element in inode list. */
    block_sector_t sector;              /*!< Sector number of disk location. */
    int open_cnt;                       /*!< Number of openers. */
    bool removed;                       /*!< True if deleted, false otherwise.*/
    int deny_write_cnt;                 /*!< 0: writes ok, >0: deny writes. */    
    struct lock ismd_lock;               /*! Inode Struct Metadata Lock */
    struct lock extension_lock;         /*! Extension lock */
};

/*! Returns the block device sector that contains byte offset POS
    within INODE.
    Returns -1 if INODE does not contain data for a byte at offset
    POS. */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);

    block_sector_t result, doubly_indirect; 

    int first, second;     
    get_indirection_indices(&first, &second, &first, &second, pos, pos);
    
    cache_sector_id src = crab_into_cached_sector(inode->sector, true, false);    
    
    struct inode_disk *data = 
        (struct inode_disk *) get_cache_sector_base_addr(src);            
    
    if (pos >= data->length)             
        result = -1;
    else
        result = 0;

    doubly_indirect = data->doubly_indirect;

    crab_outof_cached_sector(src, true);        

    return result;
}

/*! List of open inodes, so that opening a single inode twice
    returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/*! Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
    lock_init(&open_inodes_lock);
}

/*! Takes a current length (0) and a final length (length) and 

    Returns (base_first_index >= 0, base_second_index >= 0)
    Returns (final_first_index >= 0, final_second_index >= 0)

    As the base and final double/single indirection references we must start
    allocating at, and end up at, allocated and referenced to new data sectors, 
    to meet the length requirement. (0,0,1,2) would mean start at the beginning, and 
    fill the entire 0th double indirection referenced sector with 
    single indirection references to data sectors, and fill the 0th,1st,2nd 
    data sector references out the second double indirection referenced
    sector. */
static void 
get_indirection_indices(    int* base_first_index, int* base_second_index, 
                            int *final_first_index, int* final_second_index, 
                            off_t current_length, off_t final_length) {
    /* We have a single double indirection block */
    /*  Each double indirection block entry points to a single indirection
        block. Each single indirection block points to 128 sectors, or
        2**16 bytes, or 0..65535. Therefore, int-dividing the current length
        by 65536, we see that the quotient is the index into the double
        indirection array, at the current length */
    *base_first_index = current_length >> 16;
    /*  The remainder of this division, itself divided by 512, is the index
        into the single indirection block. */
    *base_second_index = (current_length - (*base_first_index << 16)) >> 9;
    
    /* We can easily get the final indices the same way */
    *final_first_index = final_length >> 16;
    *final_second_index = (final_length - (*final_first_index << 16)) >> 9;
}


/*! 
    Extends an inode on disk at SECTOR with CURRENT_LENGTH bytes of data
    to handle FUTURE_LENGTH bytes of data. Does not modify inode metadata
    on disk or in memory other than adding index references. e.g. update
    the length yourself!

    You must enter this function with a file extension lock held, but
    you may release on exit at your leisure. 

    Returns false and de-allocates the sectors handled, 
    if memory or disk allocation fails. Does not de-allocate
    SECTOR.

    If create_double_indirection is true, will allocate a doubly indirect 
    index sector and return its sector on disk in DOUBLY_INDIRECT. 
    Otherwise, will expect that pointer to be the double indirection reference
    for your inode in question. 

    */
static bool inode_extend(   bool create_double_indirection, 
                            block_sector_t* doubly_indirect_, 
                            off_t current_length, 
                            off_t future_length) {

    struct indirection_block *emptiness = NULL;    

    uint32_t base_first_index, base_second_index, first_index, second_index;         
    cache_sector_id data, singly, doubly;
    uint32_t first_sweep, second_sweep;
    bool double_indirection_flag, first_sweep_flag, second_sweep_flag;
    bool new_single_indirection_block_flag;
    uint32_t second_sweep_start, second_sweep_limit;                        
    block_sector_t single_indirection_sector, data_sector;            
    struct indirection_block *cached_single_indirection_sector;
    struct indirection_block *cached_double_indirection_sector;

    if (future_length == 0)
        return true;

    emptiness = calloc(1, sizeof(*emptiness));

    if (emptiness != NULL) {
        /*  Set all entries in emptiness to SILLY_OLD_DISK_SECTOR,
            as our sentinels */
        memset( (void *) emptiness, 
                (int) ((unsigned char) 0xFF),
                BLOCK_SECTOR_SIZE);
    } else {
        return false;
    }

    /* Find the indirection blocks necessary for length bytes */                      
    get_indirection_indices(&base_first_index, &base_second_index, 
                            &first_index, &second_index, 
                            current_length, future_length);
        
    /*  In the case of creation, we don't have a doubly indirect sector
    to start with. Let's get one. */
    if (create_double_indirection) {
        double_indirection_flag = 
            free_map_allocate(1, doubly_indirect_);                    
        if (!double_indirection_flag) {
            free(emptiness);
            return false;
        }
    } else {
        ASSERT(*doubly_indirect_ != SILLY_OLD_DISK_SECTOR);
    }

    block_sector_t doubly_indirect = *doubly_indirect_;

    /*  Bring this doubly indirect map in, cache, pin, and clear it if 
        necessary */                    
    doubly = crab_into_cached_sector(doubly_indirect, 
                                        false, 
                                        create_double_indirection);                        

    cached_double_indirection_sector = 
        (struct indirection_block *) get_cache_sector_base_addr(doubly);

    if (create_double_indirection) {
        memcpy( (void *) cached_double_indirection_sector, 
                (void *) emptiness, 
                (size_t) BLOCK_SECTOR_SIZE );             
    }

    /*  Want to avoid pinning several cache sectors in sequence,
        has the potential for deadlock if we over-constrain the 
        cache */
    crab_outof_cached_sector( doubly, false );                

    /*  Now, sweep through and allocate all the first level indirection
        sectors. */                        
    for (   first_sweep = base_first_index; 
            first_sweep < first_index+1; 
            first_sweep++) {                

        /* Allocate and remember the next single indirection sector */
        doubly = crab_into_cached_sector(doubly_indirect, 
                                            false, 
                                            false);
        cached_double_indirection_sector = 
            (struct indirection_block *) 
                get_cache_sector_base_addr(doubly);
        
        single_indirection_sector = 
            cached_double_indirection_sector->sector[first_sweep];

        first_sweep_flag = true;
        new_single_indirection_block_flag = false;
        if (single_indirection_sector == SILLY_OLD_DISK_SECTOR) {
            new_single_indirection_block_flag = true;
            first_sweep_flag = free_map_allocate(1, 
                                    &single_indirection_sector);            
            if (first_sweep_flag)                
                cached_double_indirection_sector->sector[first_sweep] = 
                    single_indirection_sector;            
        } 

        /*  Want to avoid pinning several cache sectors in sequence,
            has the potential for deadlock if we over-constrain the 
            cache */
        crab_outof_cached_sector( doubly, false );
        
        if (!first_sweep_flag) {          
            /* Release disk allocations immediately outside this loop */
            break;
        } 

        /*  Now the double indirection sector contains a reference
        to a single indirection sectors that we need to cache, clear,
        then flesh out with cleared data sector references */
        
        /* Cache the single indirection sector */                
        singly = crab_into_cached_sector(single_indirection_sector, 
                                            false, 
                                            true);

        cached_single_indirection_sector = 
            (struct indirection_block *) 
                get_cache_sector_base_addr(singly);

        if (new_single_indirection_block_flag) {
            memcpy( (void *) cached_single_indirection_sector, 
                    (void *) emptiness, 
                    (size_t) BLOCK_SECTOR_SIZE );   
        }  

        crab_outof_cached_sector(singly, false);                                                    

        /* Set up to get and clear second_sweep_limit data sectors */
        if (first_sweep == first_index && first_index != base_first_index) {
            /*  Extension crosses single indirection block boundaries,
                and we're not on the first block */
            second_sweep_start = 0;
            second_sweep_limit = second_index+1;
        } else if (first_sweep == first_index) {
            /*  Extension occurs in one single indirection block, */
            second_sweep_start = base_second_index;
            second_sweep_limit = second_index+1;
        } else if (first_sweep == base_first_index) {
            /*  Extension occurs over multiple single indirection blocks, 
                and we're at the first. */
            second_sweep_start = base_second_index;
            second_sweep_limit = INDIRECTION_REFERENCES; 
        }

        for (   second_sweep = second_sweep_start; 
                second_sweep < second_sweep_limit; 
                second_sweep++) {
            singly = crab_into_cached_sector(single_indirection_sector, 
                                            false, 
                                            false);
            cached_single_indirection_sector = 
                (struct indirection_block *) 
                    get_cache_sector_base_addr(singly);

            /* Allocate the next data sector */
            second_sweep_flag = 
                free_map_allocate(
                    1, 
                    &data_sector );
                                    
            if (second_sweep_flag)
                cached_single_indirection_sector->
                        sector[second_sweep] = data_sector;
            else 
                data_sector = SILLY_OLD_DISK_SECTOR;
            
            crab_outof_cached_sector(singly, false); 

            if (!second_sweep_flag) {
                /*  Release disk allocations immediately 
                    outside this loop */
                break;
            }

            /* Now, cache and clear the data sector */
            crab_into_cached_sector(data_sector, false, true);
            crab_outof_cached_sector(data_sector, false);
        }
    }                        

    /* Clean up if things went wrong */
    if (!first_sweep_flag || !second_sweep_flag) {            

        cleanup_failed_extension(   base_first_index, 
                                    base_second_index, 
                                    doubly_indirect);
        *doubly_indirect_ = SILLY_OLD_DISK_SECTOR;
        free(emptiness);
        return false;
    }

    free(emptiness);
    return true;
}

/*! 
    Initializes an inode with LENGTH bytes of data and
    writes the new inode to sector SECTOR on the file system
    device.

    You must enter this function with a file extension lock held,
    so that the inode referencing this inode_disk created can be
    atomically and rapidly placed into the inode_list. Then other
    file-opens will not try to create new inodes for the same file.
    Returns true if successful.
    
    Returns false and de-allocates the sectors handled, 
    if memory or disk allocation fails. Does not de-allocate
    the sector the inode_disk was supposed to be placed in. 
    */
bool inode_create(block_sector_t sector, off_t length) {
    struct inode_disk *disk_inode = NULL;    
    bool success = false;
    
    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    
    if (disk_inode != NULL) {
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;        
                        
        if (inode_extend(true, &disk_inode->doubly_indirect, 0, length)) {
            /* Write the disk_inode to disk, too! */
            
            cache_sector_id di = crab_into_cached_sector(sector, false, true);
            memcpy( get_cache_sector_base_addr(di),                        
                    (void *) disk_inode, 
                    (size_t) BLOCK_SECTOR_SIZE );     

            crab_outof_cached_sector(di, false);

            if (length > 0) {
                ASSERT(disk_inode->doubly_indirect != SILLY_OLD_DISK_SECTOR);
            }

            success = true;         
        }
                    
        free(disk_inode);        
    }

    return success;
}

/*! Cleans up after a failed file extension, either during creation or afterward
    A file extension lock must be held on the inode prior to entry.

    Starts at base_first_index and base_second index, and goes until
    it sees silly references in the index referenced by doubly_indirect.
    It frees this index sector as well if the base indices are both zero,
    as this indicates the file had length zero before. 

    It is up to the caller to free their inode_disk and inode (if necessary) 
    depending on the circumstances under which this function was called. */
void cleanup_failed_extension(  uint32_t base_first_index, 
                                uint32_t base_second_index, 
                                block_sector_t doubly_indirect) {
    cache_sector_id singly, doubly;
    uint32_t first_sweep, second_sweep, second_sweep_start;    
    block_sector_t single_indirection_sector, data_sector;            
    struct indirection_block *cached_single_indirection_sector;
    struct indirection_block *cached_double_indirection_sector;

    for (   first_sweep = base_first_index; 
            first_sweep < INDIRECTION_REFERENCES; 
            first_sweep++) {
        /* Get single indirection referenced if it's not silly */
        
        doubly = crab_into_cached_sector(doubly_indirect, 
                                            false, 
                                            false);
        cached_double_indirection_sector = 
            (struct indirection_block *) 
                get_cache_sector_base_addr(doubly);
        
        single_indirection_sector = 
            cached_double_indirection_sector->sector[first_sweep];

        if (single_indirection_sector == SILLY_OLD_DISK_SECTOR ) {
            /* No more! */
            break;
        } else {                    
        /*  Replace single indirection reference with a silly reference. 
            Not strictly necessary for creation cleanup, but 
            it is necessary if we're just cleaning up a failed
            extension.

            Note that this isn't preserving the linear contiguous 
            structure of the disk_inode index, so we'd better have a 
            file lock (on this file) as we do this */

            &cached_double_indirection_sector->sector[first_sweep] = 
                SILLY_OLD_DISK_SECTOR;
        }

        /*  Want to avoid pinning several cache sectors in sequence,
            has the potential for deadlock if we over-constrain the 
            cache */
        crab_outof_cached_sector( doubly, false );                
        if (first_sweep == base_first_index)
            second_sweep_start = base_second_index;
        else 
            second_sweep_start = 0;

        for (   second_sweep = second_sweep_start; 
                second_sweep < INDIRECTION_REFERENCES; 
                second_sweep++) {
            /* Get data sector referenced if it's not silly */
            singly = crab_into_cached_sector(single_indirection_sector
                                                false, 
                                                false);
            cached_single_indirection_sector = 
                (struct indirection_block *) 
                    get_cache_sector_base_addr(singly);
        
            data_sector = 
                cached_single_indirection_sector->sector[second_sweep];
            
            if (data_sector == SILLY_OLD_DISK_SECTOR ) {
                /* No more! */
                break;
            } else {                    
            /*  Replace data reference with a silly reference. 
                Not strictly necessary for creation cleanup, but 
                it is necessary if we're just cleaning up a failed
                extension.

                Note that this isn't preserving the linear contiguous 
                structure of the disk_inode index, so we'd better have a
                file lock (on this file) as we do this */

                &cached_single_indirection_sector->sector[second_sweep]= 
                    SILLY_OLD_DISK_SECTOR;
            }

            /*  Want to avoid pinning several cache sectors in sequence,
                has the potential for deadlock if we over-constrain the 
                cache */
            crab_outof_cached_sector( singly, false );                

            /* Remove data sector from the free-map */
            free_map_release(data_sector, 1);                    
        }

        /* Remove single indirection reference from the free-map */
        free_map_release(single_indirection_sector, 1);
    }

    /*  Free double indirection reference from free-map if original file
        size was zero. */
    if (base_first_index == 0 && base_second_index == 0)
        free_map_release(doubly_indirect, 1);
}

/*! Reads an inode from SECTOR
    and returns a `struct inode' that contains it.
    Returns a null pointer if memory allocation fails. */
struct inode * inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;    

    /* Check whether this inode is already open. */
    lock_acquire(&open_inodes_lock);    

    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {        
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {            
            inode_reopen(inode);            
            lock_release(&open_inodes_lock);
            return inode; 
        }
    }    

    /* Allocate memory. */
    inode = malloc(sizeof *inode);    
    if (inode == NULL) {        
        lock_release(&open_inodes_lock);
        return NULL;
    }

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;     
    lock_init(&inode->extension_lock);    
    lock_init(&inode->ismd_lock);
    lock_release(&open_inodes_lock);
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

    int open_count;
    lock_acquire(&inode->ismd_lock);
    open_count = --inode->open_cnt;
    lock_release(&inode->ismd_lock);
    
    /* Release resources if this was the last opener. */    
    if (open_count == 0) {
        /* Remove from inode list. Remember that if directory
            removals are thread-safe then when filesys_close is called
            no one else can access this file so open_cnt can only decrease.
            Therefore:            
            once we've made open_cnt thread_safe with ismd_lock, no
            outstanding readers or writers for this file at this point.
            Therefore we can just remove it from the inodes list, 
            and free all its sectors on disk, with no issues.
            */    
        lock_acquire(&open_inodes_lock);
        list_remove(&inode->elem);        
        lock_release(&open_inodes_lock);
        
        /* Deallocate blocks if removed. */
        if (inode->removed) {

            block_sector_t doubly_indirect;            

            cache_sector_id src = crab_into_cached_sector(inode->sector, true, 
                    false);        
            
            struct inode_disk *data = 
                (struct inode_disk *) get_cache_sector_base_addr(src);    

            doubly_indirect = data->doubly_indirect;

            crab_outof_cached_sector(src, true);        
            
            /* Re-appropriated cleanup function, ignore the name */  
            cleanup_failed_extension(0, 0, doubly_indirect); 

            free_map_release(inode->sector, 1);

        }

        free(inode); 
    }
}    

/*! Marks INODE to be deleted when it is closed by the last caller who
    has it open. Don't need to synchronize. */
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

        cache_sector_id src = crab_into_cached_sector(sector_idx, true, false);            
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

        cache_sector_id dst = crab_into_cached_sector(sector_idx, false, false);          
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
    lock_acquire(&inode->ismd_lock);
    inode->deny_write_cnt++;    
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    lock_release(&inode->ismd_lock);
}

/*! Re-enables writes to INODE.
    Must be called once by each inode opener who has called
    inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode) {
    lock_acquire(&inode->ismd_lock);
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
    lock_release(&inode->ismd_lock);
}

/*! Returns the length, in bytes, of INODE's data */
off_t inode_length(const struct inode *inode) {
    ASSERT (inode != NULL);

    cache_sector_id src = crab_into_cached_sector(inode->sector, true, false);    
    
    struct inode_disk *data = 
        (struct inode_disk *) get_cache_sector_base_addr(src);            
    
    off_t l = data->length;    
    
    crab_outof_cached_sector(src, true);        

    return l;
}

