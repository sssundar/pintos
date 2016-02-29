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
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"

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

/*! Replaces calls to palloc_get_page by allocating a user pool page AND
    making/initializing a frame table entry. Evicts a page if there isn't
    enough room in physical memory for this allocation request.
 */
void *fr_alloc_page(void *vaddr, enum pgtype type) {

	// TODO

	return NULL;
}
