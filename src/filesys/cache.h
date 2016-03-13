#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include "lib/kernel/list.h"
#include "threads/synch.h"


/* ############# Constants ############### */

/* Sizing in memory of the cache of disk sectors for files */
#define NUM_DISK_SECTORS_CACHED 64
#define NUM_DISK_CACHE_PAGES 8
typedef uint32_t cache_sector_id; 

/*! Sentinel value for old_disk_sector so we know it's ignorable (evictions,
    vs write-ahead, vs. normal operation). Works only because we have
    a tiny disk (8MB) cap. */
#define SILLY_OLD_DISK_SECTOR (block_sector_t) 0xFFFFFFFF

/* ############# Structures ############### */

/*! Cache Meta^2 Data, containing flags and locks useful for concurrent 
    reading/writing access, and eviction. */
struct cache_meta_data {
	// ------------------------- Invariants -----------------------------------
	/* This cache sector's id, from 0 to NUM_DISK_SECTORS_CACHED-1 */
    cache_sector_id cid;
    /*  Kernel virtual address of start of this cached sector. */
    void *head_of_sector_in_memory;

    // ------------------ Critical/mutable fields -----------------------------
    /* We only allow single-sector wide allocation at a time. */
    bool cache_sector_free;
    /* Preferentially do not evict if possible. */
    bool cache_sector_dirty;
    /* For basic clock eviction algorithm. */
    bool cache_sector_accessed;
    /* Flag saying we're currently evicting, or writing ahead,
	   so might want to check old_disk_sector (if >= 0)
	   as well if you are sweeping meta^2 data. also (ideally) don't try
	   to evict this again till I finish! */
    bool cache_sector_evicters_ignore;
    /* The disk sector we're evicting. */
    block_sector_t old_disk_sector;
    /* It is the disk sector we're bringing (after eviction), or have
       already brought, in. */
    block_sector_t current_disk_sector;
    /* Cache sector read/write/eviction lock (multiple access exclusion)
       blocks threads waiting for the current disk sector. */
    struct rwlock read_write_diskio_lock; 
    /* Incoming crabbing threads looking for current_disk_sector when we're
	   in the middle of an io operation should request this lock. It
	   will already be held by the io-initiating thread.
	   They will either be woken (at which point they can restart their
	   crab) or they will GET the lock (in which case the lock was
	   just released by io-initiating thread. In this case they
	   immediately release the lock and try crabbing in again. */
    struct lock pending_io_lock;
};

/* ############# Stubs ############### */

void file_cache_init(void);
cache_sector_id crab_into_cached_sector(block_sector_t t, bool readnotwrite);
void crab_outof_cached_sector(cache_sector_id c, bool readnotwrite);
void cache_read(cache_sector_id src, void *dst, int offset, size_t bytes);
void cache_write(cache_sector_id dst, void *src, int offset, int bytes);
void *get_cache_sector_base_addr(cache_sector_id c);
void flush_cache_to_disk(void);

#endif /* filesys/cache.h */
