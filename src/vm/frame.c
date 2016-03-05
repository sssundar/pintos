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
#include <bitmap.h>
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "lib/user/syscall.h"
#include "filesys/file.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"

#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

/*! Starting address of the user pages in physical memory. */
extern void *start_of_user_pages_phys;

/*! Number of user pages in physical memory. */
extern size_t num_user_pages;

/*! The frame table array. Each page frame has one entry in this array. */
struct ftbl_elem *ftbl;

/*! This lock should be used whenever the frame table is used. */
struct lock ftbl_lock;

static bool fr_is_used(struct ftbl_elem *ftbl);
static bool fr_is_pinned(struct ftbl_elem *ftbl);
static void fr_set_pin(struct ftbl_elem *ftbl, bool pinned);
static void fr_set_used(struct ftbl_elem *ftbl, bool used);
static uint32_t fr_get_corr_idx(void *paddr);
static void fr_use_helper(void *paddr, bool use);
static void fr_pin_helper(void *paddr, bool pin);
static uint32_t evict_rand(void); 

static bool fr_is_used(struct ftbl_elem *felem) {
	return felem->flags & IN_USE_MASK;
}

static bool fr_is_pinned(struct ftbl_elem *felem) {
	return felem->flags & PIN_MASK;
}


void ftbl_get_lock (void) {
	lock_acquire(&ftbl_lock);			
}

void ftbl_release_lock (void) {	
	lock_release(&ftbl_lock);	
}

/*! Sets the pinned flag for the given frame. If it's set that means this frame
    can't be evicted. If it's not set then it can be evicted. The pinned flag
    can be set even if the page is empty; if it's empty and pinned then the
    page can't be allocated. */
static void fr_set_pin(struct ftbl_elem *felem, bool pinned) {
	if (pinned) {
		felem->flags |= PIN_MASK;
	}
	else {
		felem->flags &= ~PIN_MASK;
	}
}

/*! Sets the used flag for the given frame. If it's set that means this frame
    is currently being used and can't be allocated unless it's evicted. If
    it's not set then it's not in use and can be allocated. */
static void fr_set_used(struct ftbl_elem *felem, bool used) {
	if (used) {
		felem->flags |= IN_USE_MASK;
	}
	else {
		felem->flags &= ~IN_USE_MASK;
	}
}

static void fr_use_helper(void *paddr, bool use) {
	ftbl_get_lock();
	ASSERT(paddr != NULL);
	uint32_t idx = fr_get_corr_idx(paddr);
	ASSERT(idx < num_user_pages);
	fr_set_used(&ftbl[idx], use);
	ftbl_release_lock();
}

static void fr_pin_helper(void *paddr, bool pin) {
	ftbl_get_lock();
	ASSERT(paddr != NULL);
	uint32_t idx = fr_get_corr_idx(paddr);
	ASSERT(idx < num_user_pages);

	fr_set_pin(&ftbl[idx], pin);

	ftbl_release_lock();
}

void fr_use(void *paddr) { return fr_use_helper(paddr, true); }
void fr_unuse(void *paddr) { return fr_use_helper(paddr, false); }
void fr_pin(void *paddr) { return fr_pin_helper(paddr, true); }
void fr_unpin(void *paddr) { return fr_pin_helper(paddr, false); }

/*! Gets the index in the frame table corresponding to the given physical
    address. */
static uint32_t fr_get_corr_idx(void *paddr) {
	return (uint32_t)(paddr - start_of_user_pages_phys) / PGSIZE;
}

// The inverse of the fr_get_corr_idx function
static void *fr_get_corr_paddr(uint32_t idx) {
	return (void *) ((uint32_t) start_of_user_pages_phys + idx * PGSIZE);
}

/* Initialize the frame table */
void fr_init_tbl(void) {
	lock_init(&ftbl_lock);
	// Allocate space for the frame table array.
	ftbl = (struct ftbl_elem *) calloc (num_user_pages,
			sizeof (struct ftbl_elem));
}

/* Random Frame Eviction */
static uint32_t evict_rand(void) { 
	uint32_t ans;

	do {
		ans = 1 + ((uint32_t)timer_ticks() * 37) % (num_user_pages - 1);
	} while(fr_is_pinned(&ftbl[ans]));

	return ans;
}

/*! Replaces calls to palloc_get_page by allocating a user pool page AND
    making/initializing a frame table entry. Evicts a page if there isn't
    enough room in physical memory for this allocation request. 

    Frame IS PINNED. User MUST UNPIN it after setting it up.

    Return kernel virtual address (i.e. physical address) of allocated page.
    This function will never return NULL, since it will evict a page if
    one isn't available. */
void *fr_alloc_page(void *vaddr, enum pgtype type, bool writable,
		int mid, int num_trailing_zeroes) {
	void *kpage;

	// Get a page from palloc. Use this address to index into the frame table.
	// Note that if palloc returns a page then it's DEFINITELY free.
	kpage = palloc_get_page(
			PAL_USER | (type != ZERO_PG ? 0x00000000 : PAL_ZERO));
	
	ftbl_get_lock();	

	// If we didn't get a page then evict one.
	uint32_t ev_idx = 0xFFFFFFFF; // Dummy, "sentinel" value.
	if (kpage == NULL) {		

		// Get index in frame table to evict then get the frame table element.
		ev_idx = evict_rand();
		struct ftbl_elem *to_evict = &ftbl[ev_idx];
		ASSERT(is_user_vaddr(to_evict->corr_vaddr));

		// If memory-mapped page then write it to its file, not swap.
		unsigned long long sidx;
		if (to_evict->type == MMAPD_FILE_PG) {

			printf("  --> about to write to DISK b/c mmapped. \n");

			if(write(to_evict->fd, fr_get_corr_paddr(ev_idx), PGSIZE)
					!= PGSIZE) {
				PANIC("Couldn't page mmapped file to disk.");
				NOT_REACHED();
			}
			sidx = BITMAP_ERROR;
		}
		// Write everything else to swap.
		else {

			sidx = sp_put(fr_get_corr_paddr(ev_idx));
			if (sidx == BITMAP_ERROR) {
				PANIC("Not enough room in swap.");
				NOT_REACHED();
			}

		}

		// After writing need to make a supplemental page entry with the
		// swap index we just used. Make the evicted page's PTE pouint
		// at it.
		struct spgtbl_elem *s = (struct spgtbl_elem *) pg_put(
				to_evict->mid,
				to_evict->fd,
				to_evict->type == ZERO_PG ? -1 : 0,
				NULL,
				to_evict->corr_vaddr,
				to_evict->src_file,
				to_evict->trailing_zeroes,
				to_evict->writable,
				to_evict->type,
				sidx);

		pagedir_clear_page(to_evict->tinfo->pagedir, to_evict->corr_vaddr);
		if (!pagedir_set_page(to_evict->tinfo->pagedir,
				to_evict->corr_vaddr, (void *) s,
				to_evict->writable, true)) {
			PANIC("Couldn't install suppl page entry during eviction.");
		}

		kpage = (void *)((uint32_t) start_of_user_pages_phys
				+ PGSIZE * ev_idx);
		
		if (kpage == NULL) {
			PANIC("Couldn't alloc a page even after eviction.");
			NOT_REACHED();
		}
	}

	// Get the frame table element corresponding to the allocated page. It
	// must be the same as the one just evicted.
	uint32_t idx = fr_get_corr_idx(kpage);
	ASSERT(ev_idx == 0xFFFFFFFF || ev_idx == idx);
	ASSERT(idx < num_user_pages);

	// Initialize the frame table element.
	ftbl[idx].type = type;
	ftbl[idx].writable = writable;
	ftbl[idx].corr_vaddr = vaddr;
	fr_set_pin(&ftbl[idx], true);
	fr_set_used(&ftbl[idx], true);
	ftbl[idx].tinfo = thread_current();
	ftbl[idx].fd = -1;
	ftbl[idx].offset = -1;
	ftbl[idx].src_file = NULL;
	ftbl[idx].trailing_zeroes = num_trailing_zeroes;
	ftbl[idx].mid = mid;

	ftbl_release_lock();	
	return kpage;
}
