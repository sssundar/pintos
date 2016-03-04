/*
 * swap.c
 *
 *  Created on: Feb 29, 2016
 *  Updated: 3/3/16
 *      Author(s): Hamik, Dave
 */

#include "vm/swap.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/block.h"

/*! Number of user pages in physical memory. */
extern size_t num_user_pages;

struct lock swap_lock;

/*! The bitmap used to find free swap slots. Bits are true if available, false
    otherwise. */
struct bitmap *bmap;

/*! Represents the swap file as a block device. */
struct block *swap_file;

static unsigned long long get_swap_index(void);

/* Initialize swap slot and lock. */
void sp_init(void) {
    lock_init(&swap_lock);

    swap_file = block_get_role(BLOCK_SWAP);
    if(swap_file == NULL){
        bmap = NULL;
    }
    else{
        bmap = bitmap_create(num_user_pages);
    }
}

/* If the given spot isn't empty, copies the page there to the given buffer.
   Returns true on success, false otherwise. */
bool sp_get(unsigned long long idx, void *buf) {
	lock_acquire(&swap_lock);
	if (bitmap_test(bmap, idx)) {
		lock_release(&swap_lock);
		return false;
	}
    int i;
    for(i = 0; i < SECTORS_PER_PAGE; i++) {
    	block_read(swap_file, i,
    			(void *) ((uint32_t) buf + i * BLOCK_SECTOR_SIZE));
    }
	lock_release(&swap_lock);
	return true;
}

/* Store in the swap partition, returning the index of the slot. */
unsigned long long sp_put(void *vaddr) {
	unsigned long long idx = get_swap_index();
	if (idx == BITMAP_ERROR) {
		return idx;
	}
    lock_acquire(&swap_lock);

    // We have found the index, now store it there.
    int i;
    for(i = 0; i < SECTORS_PER_PAGE; i++) {
    	block_write(swap_file, i,
    			(void *) ((uint32_t) vaddr + i * BLOCK_SECTOR_SIZE));
    }

    lock_release(&swap_lock);
    return idx;
}

/* Gets the next free swap index. */
static unsigned long long get_swap_index(void) {
    lock_acquire(&swap_lock);
    size_t r = bitmap_scan_and_flip(bmap, 0, BLOCK_SECTOR_SIZE, false);
    lock_release(&swap_lock);
    return (unsigned long long) r;
}
