/*
 * swap.c
 *
 *  Created on: Feb 29, 2016
 *  Updated: 3/3/16
 *      Author(s): Hamik, Dave
 */

#include <stdio.h>
#include "vm/swap.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/block.h"

struct lock swap_lock;

/*! The bitmap used to find free swap slots. Bits are false if available, true
    if not. */
struct bitmap *bmap;

/*! Represents the swap file as a block device. */
struct block *swap_file;

static unsigned long long get_slot_index(void);

/* Initialize swap slot and lock. */
void sp_init(void) {
    lock_init(&swap_lock);

    swap_file = block_get_role(BLOCK_SWAP);
    if(swap_file == NULL) {
    	PANIC("Swap partition couldn't spin up!");
        NOT_REACHED();
    }
    else {
    	bmap = bitmap_create(swap_file->size / SECTORS_PER_PAGE);
    }
}

/* If the given spot isn't empty, copies the page there to the given buffer.
   Returns true on success, false otherwise. */
bool sp_get(unsigned long long idx, void *buf) {
	lock_acquire(&swap_lock);
	if (!bitmap_test(bmap, idx)) {
		lock_release(&swap_lock);
		return false;
	}
    int i;
    for(i = 0; i < SECTORS_PER_PAGE; i++) {
    	block_read(swap_file, (block_sector_t) (SECTORS_PER_PAGE * idx + i),
    			(void *) ((uint32_t) buf + i * BLOCK_SECTOR_SIZE));
    }
    bitmap_set(bmap, (size_t) idx, false);
	lock_release(&swap_lock);
	return true;
}

/* Store in the swap partition, returning the index of the slot. */
unsigned long long sp_put(void *vaddr) {
	unsigned long long idx = get_slot_index();
	if (idx == BITMAP_ERROR) {
		return idx;
	}
    lock_acquire(&swap_lock);

    // We have found the index, now store it there.
    int i;
    for(i = 0; i < SECTORS_PER_PAGE; i++) {

    	//printf("  --> writing block %d\n ", i);

    	block_write(
    			swap_file,
    			(block_sector_t) (SECTORS_PER_PAGE * idx + i),
    			(void *) ((uint32_t) vaddr + i * BLOCK_SECTOR_SIZE));
    }

    lock_release(&swap_lock);

    //printf("  --> sp returning: %llu\n", idx);

    return idx;
}

/* Gets the next free swap index. */
static unsigned long long get_slot_index(void) {
    lock_acquire(&swap_lock);
    size_t r = bitmap_scan_and_flip(bmap, 0, 1, false);
    lock_release(&swap_lock);
    return (unsigned long long) r;
}
