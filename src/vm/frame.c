/*
 * frame.c
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <hash.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#include "vm/page.h"
#include "vm/frame.h"

/*! Starting address of the user pages in physical memory. */
extern void *start_of_user_pages_phys;

/*! Number of user pages in physical memory. */
extern size_t num_user_pages;

/*! The frame table array. Each page frame has one entry in this array. */
struct ftbl_elem *ftbl;

/*! This lock should be used whenever the frame table is used. */
struct lock ftbl_lock;

bool fr_is_used(struct ftbl_elem *ftbl) {
	return ftbl->flags & IN_USE_MASK;
}

bool fr_is_pinned(struct ftbl_elem *ftbl) {
	return ftbl->flags * PIN_MASK;
}

/*! Sets the pinned flag for the given frame. If it's set that means this frame
    can't be evicted. If it's not set then it can be evicted. The pinned flag
    can be set even if the page is empty; if it's empty and pinned then the
    page can't be allocated.
 */
void fr_set_pin(struct ftbl_elem *ftbl, bool pinned) {
	if (pinned) {
		ftbl->flags |= PIN_MASK;
	}
	else {
		ftbl->flags &= ~PIN_MASK;
	}
}

/*! Sets the used flag for the given frame. If it's set that means this frame
    is currently being used and can't be allocated unless it's evicted. If
    it's not set then it's not in use and can be allocated.
 */
void fr_set_used(struct ftbl_elem *ftbl, bool used) {
	if (used) {
		ftbl->flags |= IN_USE_MASK;
	}
	else {
		ftbl->flags &= ~IN_USE_MASK;
	}
}

/*! Gets the physical address corresponding to the given index in the frame
    table.
 */
void *fr_get_corr_phys_addr(int idx) {
	return start_of_user_pages_phys + idx * PGSIZE;
}

/*! Gets the index in the frame table corresponding to the given physical
    address.
 */
uint32_t fr_get_corr_idx(void *paddr) {
	return (uint32_t)(paddr - start_of_user_pages_phys) / PGSIZE;
}

void fr_init_tbl(void) {
	// Allocate space for the frame table array.
	ftbl = (struct ftbl_elem *) calloc (num_user_pages,
			sizeof (struct ftbl_elem));
}

/*! Replaces calls to palloc_get_page by allocating a user pool page AND
    making/initializing a frame table entry. Evicts a page if there isn't
    enough room in physical memory for this allocation request.
 */
void *fr_alloc_page(void *vaddr, enum pgtype type) {

	int i;

	// Find an open frame table entry.
	// TODO for now, we're going to assume that there is DEFINITELY an open one
	lock_acquire(&ftbl_lock);
	//for (i = 0; i < )
	lock_release(&ftbl_lock);


	// TODO handle eviction case here.

	return NULL;
}
