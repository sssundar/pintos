#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include "lib/kernel/list.h"

/* Sizing in memory of the cache of disk sectors for files */
#define NUM_DISK_SECTORS_CACHED 64
#define NUM_DISK_CACHE_PAGES 8
typedef uint32_t cache_sector_id; 

/*! Filler value for old_disk_sector so we know it's ignorable (evictions,
    vs write-ahead, vs. normal operation). Works only because we have
    a tiny disk (8MB) cap. */
#define SILLY_OLD_DISK_SECTOR (block_sector_t) 0xFFFFFFFF

/*! Cache Meta^2 Data, containing flags and locks useful for concurrent 
    reading/writing access, and eviction. */
struct cache_meta_data {
    void *head_of_sector_in_memory; 
        /*  Kernel virtual address (never paged out) of start of this cache 
            sector */
    bool cache_sector_free;
        /* we only allow single-sector wide allocation at a time */
    bool cache_sector_dirty;
        /* preferentially do not evict if possible */
    bool cache_sector_accessed;
        /* for basic clock eviction algorithm */
    bool cache_sector_evicters_ignore;
        /*  flag saying we're currently evicting, or writing ahead, 
            so might want to check old_disk_sector (if >= 0) 
            as well if you are sweeping meta^2 data. also (ideally) don't try
            to evict this again till I finish! */
    block_sector_t old_disk_sector;
        /* the disk sector we're evicting */
    block_sector_t current_disk_sector;
        /*  it is the disk sector we're bringing (after eviction), 
            or have already brought, in */
    struct rwlock read_write_diskio_lock; 
        /*  cache sector read/write/eviction lock (multiple access exclusion) 
            blocks threads waiting for the current disk sector */
};

/*! Initialize the disk cache and cache meta^2 data (different than inode
    meta data). Must be called after kernel pages have been allocated. 

    This function will either succeed or panic the kernel. */
void file_cache_init(void);

/*! The file system read/write calls see this as their interface to the cache.
    It synchronously checks whether (t) is in our cache. If it is, it returns
    its ID. If it isn't, it tries to allocate a free cache sector with 
    that disk sector. If it fails, it attempts an eviction. The upshot is
    this call will either succeed or panic the kernel. 

    After this call the caller may acquire the read/write/evict lock,
    though no guarantees are made as to whether the intended sector is in
    the cache at the returned location, as we're Golden-Rule crabbing, 
    releasing locks quickly. */
cache_sector_id crab_into_cached_sector(block_sector_t t);



#endif /* filesys/cache.h */