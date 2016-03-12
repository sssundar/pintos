#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/*! Cache circular queue head index for clock eviction */
cache_sector_id cache_head;

/*! Maintain a global allow_cache_sweeps lock permanently
    This allows threads to search the cache meta^2 data for their 
    sector of interest without having to worry about whether sector meta^2 data
    changes partway through. They cannot spend a long time holding this lock.
    See discussion of crabbing in Lecture 25! */
struct lock allow_cache_sweeps; 

/* Pointer to the head of an array of cache_meta_data structs for our cache */
struct cache_meta_data *supplemental_filesystem_cache_table;

/* Pointer to the pages associated with the file system cache itself */
void *file_system_cache; 

/*! Increment head index, wrapping at NUM_DISK_SECTORS_CACHED to 0 
    Must only be called with the lock allow_cache_sweeps locked so access
    is synchronous. */
static cache_sector_id update_head(void) {
    cache_head = (cache_head + 1) & ((uint32_t) 0x0000003F);
    return cache_head;
}

/*! Initialize the disk cache and cache meta^2 data (different than inode
    meta data). Must be called after kernel pages have been allocated. 

    Use palloc to allocate 64 contiguous sectors, or 8 pages for the cache.
    Use calloc to allocate kernel memory for all the meta^2 data.

    This function will either succeed or panic the kernel. */
void file_cache_init(void) {
    /*  We'll start off our eviction policy's circular queue at the first sector
        in our cache */
    cache_head = 0;                     

    /*  Initialize the lock that ensures threads can sweep the cache suppl.
        table and expect it to stay static while they sweep. */
    lock_init(&allow_cache_sweeps);

    /*  Allocate pages for our NUM_DISK_SECTORS_CACHED sector cache in the
        kernel pool */
    file_system_cache = palloc_get_multiple(PAL_ASSERT | PAL_ZERO, 
                                                  NUM_DISK_CACHE_PAGES);        

    /* Allocate and initialize cache metadata in kernel space */
    supplemental_filesystem_cache_table = 
        (struct cache_meta_data *) calloc(  NUM_DISK_SECTORS_CACHED, 
                                            sizeof(struct cache_meta_data));
    if (supplemental_filesystem_cache_table == NULL) 
        PANIC("Couldn't allocate cache metadata table.");

    struct cache_meta_data *meta_walker = supplemental_filesystem_cache_table;
    void *fs_cache = file_system_cache;
    for (int k = 0; k < NUM_DISK_SECTORS_CACHED; k++) {        
        meta_walker->head_of_sector_in_memory = fs_cache;
        meta_walker->cache_sector_free = true;
        meta_walker->cache_sector_dirty = false;
        meta_walker->cache_sector_accessed = false;
        meta_walker->cache_sector_evicters_ignore = false;
        meta_walker->old_disk_sector = SILLY_OLD_DISK_SECTOR;
        meta_walker->current_disk_sector = SILLY_OLD_DISK_SECTOR;
        rw_init(&meta_walker->read_write_diskio_lock);

        fs_cache += BLOCK_SECTOR_SIZE;
        meta_walker += 1;
    }
}

void pull_sector_from_disk_to_cache(block_sector_t t, cache_sector_id c);
void push_sector_from_cache_to_disk(block_sector_t t, cache_sector_id c);
bool try_allocating_free_cache_sector(cache_sector_id* c);
cache_sector_id select_cache_sector_for_eviction(void);
void evict_cached_sector (cache_sector_id* c);

/*! The file system read/write calls see this as their interface to the cache.
    It synchronously checks whether (t) is in our cache. If it is, it returns
    its ID. If it isn't, it tries to allocate a free cache sector with 
    that disk sector. If it fails, it attempts an eviction. The upshot is
    this call will either succeed or panic the kernel. 

    After this call the caller may acquire the read/write/evict lock,
    though no guarantees are made as to whether the intended sector is in
    the cache at the returned location, as we're Golden-Rule crabbing, 
    releasing locks quickly. */
cache_sector_id crab_into_cached_sector(block_sector_t t) {
    //==TODO== IMPLEMENT
    
    return 0;
}

/*! Bring in a sector (t) from the disk to our cache at index (c).
    Sets up cache meta data appropriately. 
    Assumes cache sector is not free, and cache_sector_evicters_ignore set, 
        prior to entry. 
    Will wake waiters using rwlock prior to exit.

    This function will either succeed of panic the kernel. */
void pull_sector_from_disk_to_cache(block_sector_t t, cache_sector_id c) {
    //==TODO== IMPLEMENT
}

/*! Write a sector (c) from the cache to the disk at sector (t).
    Occurs either during eviction or write_ahead.   
    
    Assumes cache sector is not free, and cache_sector_evicters_ignore set, 
        prior to entry. 
    Does not touch cache meta-data.     

    This function will either succeed of panic the kernel. */
void push_sector_from_cache_to_disk(block_sector_t t, cache_sector_id c) {
    //==TODO== IMPLEMENT    
}

/*! Tries to allocate a free sector, if one exists in our cache. 

    Assumes prior to entry, cache has been locked from sweeps/modification of 
    meta data. 

    Simply sets the cache_sector_free to false and cache_sector_evicters_ignore
    to true. Expects pull_sector_from_disk_to_cache to be called afterward, or
    evict_cached_sector, to complete setup. 
    
    The cache_sector_id pointer c is set to the index in 
    0..NUM_DISK_SECTORS_CACHED-1 that was found free, on success.

    Returns false on failure, true on success. */
bool try_allocating_free_cache_sector(cache_sector_id* c) {
    //==TODO== IMPLEMENT
    return false;
}

/*! Implements clock eviction policy. 

    Assumes prior to entry, cache has been locked from sweeps/modification of 
    meta data. Assumes try_allocating_free_cache_sector() was already attempted.
    
    Sets the cache sector chosen's cache_sector_evicters_ignore.

    Look only at sectors that don't have cache_sector_evicters_ignore set. 
    Panics, for now, if none are found.
    
    ==TODO==    
    Look for unpinned sectors that are not dirty or metadata, first.
        If some are found, but all are accessed, toss the first.
        Otherwise toss the first non-accessed one. 
    If none are found, look for unpinned sectors that are dirty.
        If some are found, but all are accessed, toss the first.
        Otherwise toss the first non-accessed one. 
    If none are found, all sectors are inode-style metadata.
        If some are found, but all are accessed, toss the first.
        Otherwise toss the first non-accessed one. 

    Currently, tosses the first non-accessed sector it finds, moving the 
    global clock hand appropriately. Guaranteed to be the only thread
    accessing this clock hand by a cache sweep lock locked prior to entry.

    If no non-accessed sectors are found, returns the first.
*/
cache_sector_id select_cache_sector_for_eviction(void) {
    //==TODO== IMPLEMENT
    return 0;
}

/*! 
We will lock the cache from sweeps temporarily

We acquire the allow_cache_sweeps lock
Now the outside world sees our eviction choices as atomic.
We set the evictors_ignore flag for the sector being evicted so the kernel 
doesn't try to evict it again, and so writeahead ignores us. This also helps 
incoming threads know to look at the old sector (if it's meaningful).
We release the allow_cache_sweeps lock

We request an eviction lock (r/w/e)->e.
We set old_disk_sector to current_disk_sector
We set current_disk_sector to the thing we're going to bring in
We do the eviction (not yet the replacement) if cache_sector_dirty is set

We acquire the allow_cache_sweeps lock
We clear the cache_sector_accessed bit
We do not touch cache_sector_free - we'll replace this sector ourselves.
We remove old_disk_sector 
We wake up everyone in the eviction wait list, even those waiting on the soon
to-be- replacement. Let them duke it out, or block on us again.
We release the allow_cache_sweeps lock

We proceed with replacement of the evicted cache.
New inquiries into the replacement will block on our read/write lock
cache_sector_accessed is set by anyone who blocks on us.
    by the rwlock accessors, and cleared by write-behind.

We release the r/w/e eviction lock. We're done!

Updates the pointer to the cache_sector_id (c) that was evicted. 
*/
void evict_cached_sector (cache_sector_id* c) {
    //==TODO== IMPLEMENT
}

