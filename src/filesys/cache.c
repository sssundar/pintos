/************************************************
* Hint: Start reading at the crab-in function! *
***********************************************/

/* ================== Imports ============== */

#include <stdint.h>
#include <stdbool.h>

#include <debug.h>
#include <stdio.h>
#include <string.h>

#include "devices/block.h"
#include "lib/kernel/list.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"

/* =============== Stubs ================== */ 

void pull_sector_from_disk_to_cache(block_sector_t t, cache_sector_id c);
void push_sector_from_cache_to_disk(block_sector_t t, cache_sector_id c);
bool try_allocating_free_cache_sector(cache_sector_id* c, block_sector_t t);
void select_cache_sector_for_eviction(cache_sector_id* c, block_sector_t t);
void evict_cached_sector (cache_sector_id c);
void mark_cache_sector_as_accessed(cache_sector_id c);
void mark_cache_sector_as_dirty(cache_sector_id c);
bool is_disk_sector_in_cache (cache_sector_id c, block_sector_t t);

/* =============== Statically Allocated Variables ================= */ 

/*! Cache circular queue head index for clock eviction */
cache_sector_id cache_head;

/*! Maintain a global allow_cache_sweeps lock permanently
    This allows threads to search the cache meta^2 data for their 
    sector of interest without having to worry about whether sector meta^2 data
    changes partway through. They cannot spend a long time holding this lock.
    See discussion of crabbing in Lecture 25! */
struct lock allow_cache_sweeps; 

/* Pointer to the head of a contiguous array of cache_meta_data structs */
struct cache_meta_data *supplemental_filesystem_cache_table;

/* Pointer to the pages associated with the file system cache itself */
void *file_system_cache;

/* ========================= Functions ================== */

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

    int k;
    for (k = 0; k < NUM_DISK_SECTORS_CACHED; k++) {        
        meta_walker->cid = k;
        meta_walker->head_of_sector_in_memory = fs_cache;
        meta_walker->cache_sector_free = true;
        meta_walker->cache_sector_dirty = false;
        meta_walker->cache_sector_accessed = false;
        meta_walker->cache_sector_evicters_ignore = false;
        meta_walker->old_disk_sector = SILLY_OLD_DISK_SECTOR;
        meta_walker->current_disk_sector = SILLY_OLD_DISK_SECTOR;
        rw_init(&meta_walker->read_write_diskio_lock);
        lock_init(&meta_walker->pending_io_lock);
        fs_cache += BLOCK_SECTOR_SIZE;
        meta_walker += 1;
    }
}

/*! Increment head index, wrapping at NUM_DISK_SECTORS_CACHED to 0 
    Must only be called with the lock allow_cache_sweeps locked so access
    is synchronous. */
static cache_sector_id update_head(void) {
    cache_head = (cache_head + 1) & ((uint32_t) 0x0000003F); // This is % 64
    return cache_head;
}

/*! For external use, after an io lock has been granted and data has been 
    written. Mark the cache sector c as dirty after acquiring a sweep lock. 
    We do this after we've actually written something to the sector */
void mark_cache_sector_as_dirty(cache_sector_id c) {
    ASSERT(c < NUM_DISK_SECTORS_CACHED);
 
    lock_acquire(&allow_cache_sweeps);        
    (supplemental_filesystem_cache_table+c)->cache_sector_dirty = true;
    lock_release(&allow_cache_sweeps);
}

/*! For external use after an io or rw lock has been granted and the
    sector has been confirmed what was requested. 

    Mark the cache sector c as accessed after acquiring a sweep lock. */
void mark_cache_sector_as_accessed(cache_sector_id c) {
    ASSERT(c < NUM_DISK_SECTORS_CACHED);

    lock_acquire(&allow_cache_sweeps);        
    (supplemental_filesystem_cache_table+c)->cache_sector_accessed = true;
    lock_release(&allow_cache_sweeps);
}

/*! Accessor. Assumes appropriate rw_lock held. 
    Reads BYTES bytes from file cache sector SRC into DST, 
    starting at position (OFFSET > 0) in SRC. */
void cache_read(cache_sector_id src, void *dst, int offset, size_t bytes) {

    ASSERT(offset+bytes-1 < BLOCK_SECTOR_SIZE);

    ASSERT(offset > 0);
    ASSERT(bytes > 0);

    memcpy(dst, 
            (void *) (  (uint32_t) (supplemental_filesystem_cache_table+
                                    src)->head_of_sector_in_memory + 
                        (uint32_t) offset), 
            bytes);
}

/*! Accessor. Assumes appropriate rw_lock held. 
    Writes BYTES bytes from BUFFER into file cache sector C 
    starting at position OFFSET in C. */
void cache_write(cache_sector_id dst, void *src, int offset, int bytes) {

    ASSERT(offset+bytes-1 < BLOCK_SECTOR_SIZE);

    ASSERT(offset > 0);
    ASSERT(bytes > 0);

    memcpy((void *) (   (uint32_t) (supplemental_filesystem_cache_table +
                                    dst)->head_of_sector_in_memory + 
                        (uint32_t) offset), 
            src,
            bytes);
}

/* Gets the kernel virtual address of the base of the cache sector specified */
void *get_cache_sector_base_addr(cache_sector_id c) {
    return (supplemental_filesystem_cache_table + c)->head_of_sector_in_memory;
}

/*! Must be called after acquiring a r/w lock. Verifies that the intended
    disk sector is in residence in the cache sector locked. Given that
    the r/w lock is held, there is no question of the cache being in-eviction 
    so we don't bother checking the old disk sector. */
bool is_disk_sector_in_cache (cache_sector_id c, block_sector_t t) {        
    bool result = false;
    lock_acquire(&allow_cache_sweeps);
    if ((supplemental_filesystem_cache_table+c)->current_disk_sector == t) {
        result = true;
    } 
    lock_release(&allow_cache_sweeps);
    return result;
}

/*! The system calls use file_read/write which use inode_read/write, which
    use block_read/write with target sectors.

    This is the wrapper for the cache that inode uses. We deal with block_read
    and block_write, and inode gets to work with a buffer in memory.

    So this function is part of the interface to the cache.
    It implements a cache_lock-check-release-sector_lock-act crabbing mechanism.
    The caller can assume that when a cache sector is returned, it contains
    the correct sector and that they have been granted a lock to act on it
    as specified in readnotwrite. 

    The catch is that the caller must be sure to crab out of the sector when
    they're done with it. 

    The idea is that readahead, writebehind, eviction, swapping, these all
    happen under the hood of this function. The inode functions just worry about
    how to interpret files, how to extend them, etc. but when they want to 
    actually do reads/writes, they come here and can assume the cache has
    what they need.

    The requested sector on disk, t, must already exist on the free-space map 
    of the disk. 

    ==TODO== What about file extension? deletion? write-behind? read-ahead?
    ==TODO== What if the sector doesn't exist in the free-space map? Should
        that be the responsibility of the inode function caller?
    */
cache_sector_id crab_into_cached_sector(block_sector_t t, bool readnotwrite) {
    
    cache_sector_id target;
    struct cache_meta_data *meta_walker;
    bool free_sector_allocated;

    while (true) {        
    
        /*
        Acquire the cache sweep lock.    
        Check to see whether the block sector requested is in our cache
            Might be in middle of eviction, pull-in, read-ahead, write-behind
            or deletion - be careful.
        Release the cache sweep lock
        */
    
        target = NUM_DISK_SECTORS_CACHED; /* Nonsense */
        meta_walker = supplemental_filesystem_cache_table; /* Base */
    
        lock_acquire(&allow_cache_sweeps);
    
        int k;
        for (k = 0; k < NUM_DISK_SECTORS_CACHED; k++) {        
    
            if (!meta_walker->cache_sector_free) {                
    
                if ( (!meta_walker->cache_sector_evicters_ignore &&
                        (meta_walker->current_disk_sector == t) ) ||
                    (meta_walker->cache_sector_evicters_ignore &&
                                (meta_walker->old_disk_sector == t) ) ) {
                    /*  Either this has my sector of interest, or
                        it's about to be evicted and it still contains
                        my sector of interest, and I might be able to 
                        worm my way in, or it is evicting (or doing some
                        other blocking io) and I will block on this, and 
                        be woken when it's safe to bring my sector back in
                        again. */                         
                    target = meta_walker->cid;                    
                    break;
                } 

                if (meta_walker->cache_sector_evicters_ignore && 
                        (meta_walker->current_disk_sector == t) ) {
                    /*  When this sector finishes io, it will contain
                        my sector of interest. However, it does not contain
                        it right now. Therefore, I should block on its
                        pending_io lock till it's done. */
                	lock_release(&allow_cache_sweeps);
                    lock_acquire(&meta_walker->pending_io_lock);
                    lock_acquire(&allow_cache_sweeps);
                    /*  Immediately release the lock and try to crab
                        for my sector again. This will wake
                        up others waiting on this lock. */
                    lock_release(&meta_walker->pending_io_lock);

                    target = meta_walker->cid;
                    break;
                }
            }
    
            meta_walker += 1;
    
        }        
    
        meta_walker = supplemental_filesystem_cache_table; /* Base */

        if (target < NUM_DISK_SECTORS_CACHED) {
            
            lock_release(&allow_cache_sweeps);   

            /*
                The block sector was in our cache
                
                Acquire a cache read/write lock on it

                Break out of loop
            */

            /* Read = True, Write = False */
            /* DiskIO = True, CacheRW = False */
            rw_acquire(&(meta_walker+target)->read_write_diskio_lock,
                        readnotwrite, 
                        false); 

            if (!is_disk_sector_in_cache(target, t)) {
                /* We have the r/w lock but the requested disk sector is not
                    in the sector "target". Try again, and don't set any 
                    accessed/dirty flags. */
                rw_release(&(meta_walker+target)->read_write_diskio_lock, 
                            readnotwrite, 
                            false); 
                continue;
            } else {
                /* We have the r/w lock and the requested disk sector is in the 
                    sector "target". We're done. */
                break;
            }            

        } else {
            /*  Retain the sweep lock, because the block sector was not in our 
                cache. We need to carefully let all other threads know we're 
                bringing it in so no one else tries to at the same time,
                and so no one can access the old sector thinking it's new 
                sector!
                        
                Draft a free cache sector 
                    Set evicters_ignore flag to true                    
                    Set (current, old) disk sectors to (t, SILLY)
                    Acquire the (guaranteed free) pending_io_lock               
                or 
                Prep a used, not-ignored-by-evictors cache sector for eviction
                    Set evicters_ignore flag to true
                    Set (current, old) disk sectors to (t, current)
                    Acquire the (guaranteed free) pending_io_lock

                    Nothing about the cache data has changed, and incoming
                    readers/writers for the old sector can still correctly
                    read/write the cache. Incoming readers/writers for the 
                    new sector will see that there is a cache sector
                    for their target, but that it's in the process 
                    of disk io. However, we haven't acquired a disk io lock
                    yet (or even put ourselves in the queue, with priority)
                    so these incoming must be prevented from accessing the 
                    cache sector, which has another sector's data. That's 
                    the reason for the pending_io_lock shenanigans.

                Release the sweep lock
            */
        
            free_sector_allocated = try_allocating_free_cache_sector(&target, 
                                                                        t);
            
            if (!free_sector_allocated) {
                select_cache_sector_for_eviction(&target, t);
            }
            
            lock_release(&allow_cache_sweeps);

            /*
                Acquire a disk io lock on that sector  
                Release the pending_io_lock

                    This will force incoming and blocked readers/writers to 
                    wait till we're done, but allows current readers/writers 
                    to finish.                                         
            */

            /* IRRELEVANT: Read = True, Write = False */
            /* DiskIO = True, CacheRW = False */
            rw_acquire(&(meta_walker+target)->read_write_diskio_lock,
                        true,
                        true);

            lock_release(&(meta_walker+target)->pending_io_lock);

            /* Evict/Load as necessary */

            if (!free_sector_allocated) {
                /* Proceed with Eviction */
                evict_cached_sector(target);
            } 

            /* Bring in the relevant data from disk */        
            pull_sector_from_disk_to_cache(t, target);

            /*    
                Acquire a sweep lock                                
                    Set the evictors_ignore to false
                    Set accessed and dirty to false
                    Remove the old_disk_sector and set it to SILLY
                    Release the disk io lock 
                Release the sweep lock

                Go to start of loop to try and acquire a 
                    cache rw lock on the updated sector            
            */                    

            lock_acquire(&allow_cache_sweeps);

            (meta_walker+target)->cache_sector_accessed = false;
            (meta_walker+target)->cache_sector_dirty = false;
            (meta_walker+target)->old_disk_sector = SILLY_OLD_DISK_SECTOR;

            /* IRRELEVANT: Read = True, Write = False */
            /* DiskIO = True, CacheRW = False */
            rw_release(&(meta_walker+target)->read_write_diskio_lock,
                        true,
                        true);

            lock_release(&allow_cache_sweeps);
        }

    }

    return target;
}

/*! The read/write/io lock interface in synch.c handles fairness and access
    scheduling. We simply release the lock. This is a catch-all wrapper so
    we can easily highlight entry and exit into sectors while debugging. */
void crab_outof_cached_sector(cache_sector_id c, bool readnotwrite) {
    struct cache_meta_data *meta_walker;
    meta_walker = supplemental_filesystem_cache_table; /* Base */

    /* Assumption: external callers  don't crab in except to read or write */    
    mark_cache_sector_as_accessed(c); /* Since we did */
    if (!readnotwrite)
        mark_cache_sector_as_dirty(c); /* If we wrote */

    /* Read = True, Write = False */
    /* DiskIO = True, CacheRW = False */
    rw_release(&(meta_walker+c)->read_write_diskio_lock, 
                readnotwrite,
                false); 
}

/*! Tries to allocate a free sector, if one exists in our cache. 

    Prior to entry, a sweep lock must be acquired.

    Searches for free cache sectors
    Picks the first it finds.

    Sets cache_sector_free to false    
    Set evicters_ignore flag to true                    
    Set (current, old) disk sectors to (t, SILLY)
    Acquire the (guaranteed free) pending_io_lock               

    The cache_sector_id pointer c is set to the index in 
    0..NUM_DISK_SECTORS_CACHED-1 that was found free, on success.

    Returns false on failure, true on success. */
bool try_allocating_free_cache_sector(cache_sector_id* c, block_sector_t t) {
    
    bool result = false;    
    struct cache_meta_data *meta_walker;
    
    meta_walker = supplemental_filesystem_cache_table; /* Base */
    
    int k;
    for (k = 0; k < NUM_DISK_SECTORS_CACHED; k++) {        
        if (meta_walker->cache_sector_free) {            
            meta_walker->cache_sector_free = false;
            meta_walker->cache_sector_evicters_ignore = true;
            meta_walker->current_disk_sector = t;
            meta_walker->old_disk_sector = SILLY_OLD_DISK_SECTOR;
            lock_acquire(&meta_walker->pending_io_lock);
            *c = meta_walker->cid;
            result = true;
            break;
        }

        meta_walker += 1;
    }
    
    return result;
}

/*! Implements clock eviction policy. 

    Prior to entry, cache has been locked from sweeps and we've tried
    to allocate a free cache sector.

    Therefore, we can safely assume there are no free cache entries while
    we execute this call.
    
    Preps a used, not-ignored-by-evictors cache sector for eviction
    Panics, for now, if none are found.
        Set evicters_ignore flag to true
        Set (current, old) disk sectors to (t, current)
        Acquire the (guaranteed free) pending_io_lock

    Currently, returns the first such non-accessed sector it finds, moving the 
    global clock hand appropriately. 

    If no non-accessed sectors are found, returns the first.
    
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

*/
void select_cache_sector_for_eviction(cache_sector_id *c, block_sector_t t) {    
    
    bool found_an_eviction_candidate = false;  

    bool firstPass = true; /* Is this our first pass through the cache? */

    struct cache_meta_data *meta_walker;
    
    meta_walker = supplemental_filesystem_cache_table; /* Base */

    while (!found_an_eviction_candidate) {
        
        int k;
        for (k = 0; k < NUM_DISK_SECTORS_CACHED; k++) {            
            
            if (    !(meta_walker+cache_head)->cache_sector_evicters_ignore ) {
                
                if (!firstPass || (firstPass && 
                        !(meta_walker+cache_head)->cache_sector_accessed ) ) {
                    
                    (meta_walker+cache_head)->cache_sector_evicters_ignore = 
                        true;
                    
                    (meta_walker+cache_head)->old_disk_sector = 
                        (meta_walker+cache_head)->current_disk_sector;
                    
                    (meta_walker+cache_head)->current_disk_sector = t;
        
                    lock_acquire(&(meta_walker+cache_head)->pending_io_lock);  
                    
                    *c = cache_head;

                    update_head();        

                    found_an_eviction_candidate = true;

                    break;
        
                }                
        
            }
            
            update_head();            
        
        }
        
        if (firstPass)
            firstPass = false;
        else
            break;
    
    }

    ASSERT(found_an_eviction_candidate);        
}

/*! Write a sector (c) from the cache to the disk at sector (t).    
    
    Assumes cache sector is not free.
    Assumes cache_sector_evicters_ignore is set prior to entry. 
    Assumes an io lock has been granted prior to entry. 
        Implies no current readers/writers have locks on this data.
    Does not touch cache meta-data or release any locks.

    This function will either succeed of panic the kernel. */
void push_sector_from_cache_to_disk(block_sector_t t, cache_sector_id c) {    
    block_write(
        fs_device, 
        t, 
        (supplemental_filesystem_cache_table+c)->head_of_sector_in_memory
        );
}

/*! Bring in a sector (t) from the disk to our cache at index (c).
    
    Assumes cache sector is not free.
    Assumes cache_sector_evicters_ignore is set prior to entry. 
    Assumes an io lock has been granted prior to entry. 
        Implies no current readers/writers have locks on this data.
    Does not touch cache meta-data or release any locks.

    This function will either succeed of panic the kernel. */
void pull_sector_from_disk_to_cache(block_sector_t t, cache_sector_id c) {
    block_read(
        fs_device, 
        t, 
        (supplemental_filesystem_cache_table+c)->head_of_sector_in_memory
        );
}

/*! This function is called with a disk io lock held. 
    It does not release any locks or change any metadata.

    We do the eviction if cache_sector_dirty is set.
    Replacement is not within the scope of this call. */
void evict_cached_sector (cache_sector_id c) {
    ASSERT(
        (supplemental_filesystem_cache_table + c)->old_disk_sector != 
        SILLY_OLD_DISK_SECTOR
        );

    if ((supplemental_filesystem_cache_table + c)->cache_sector_dirty) {
        push_sector_from_cache_to_disk(
            (supplemental_filesystem_cache_table + c)->old_disk_sector, c);
    } 
}

void flush_cache_to_disk(void) {

}
