/*
 * page.c
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#include <stdbool.h>
#include <stdio.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "vm/page.h"

/*! Use this whenever tinkering with page directory. */
struct lock pglock;

void pg_init(void) { lock_init(&pglock); }
void pg_lock_pd(void) { lock_acquire(&pglock); }
void pg_release_pd(void) { lock_release(&pglock); }

/*! Allocate and populate a new supplemental page table element (SPGTE). Put
    it in kernel space instead of into a hash table. The caller should pack
    a pointer to this SPGTE into the user space page table element. Return a
    pointer to this SPGTE. */
struct spgtbl_elem *pg_put(int mid, int fd, off_t ofs, void *paddr,
		void *vaddr, struct file *file, uint32_t num_trailing_zeroes,
		bool writable, enum pgtype type) {

	pg_lock_pd();
	struct spgtbl_elem *s = (struct spgtbl_elem *) malloc(
	        		sizeof(struct spgtbl_elem));

	// If we can't even allocate a supplemental page table entry then there's
	// not enough memory, so panic!
	if (s == NULL) {
		PANIC("Can't allocate supplemental page table entry.");
		NOT_REACHED();
	}

	switch (type) {
	case MMAPD_FILE_PG:
		// TODO nothing for now?
		break;
	case EXECD_FILE_PG:
		ASSERT(num_trailing_zeroes != PGSIZE);
		break;
	case ZERO_PG:
		ASSERT(num_trailing_zeroes == PGSIZE);
		ASSERT(fd == -1);
		ASSERT(ofs == -1);
		ASSERT(paddr == NULL);
		ASSERT(file == NULL);
		break;
	case OTHER_PG:
		// TODO nothing for now?
		break;
	default:
		PANIC("Impossible page type in pg_put.");
		NOT_REACHED();
	}
	s->mid = mid;
	s->magic = PG_MAGIC;
	s->fd = fd;
	s->offset = ofs;
	s->paddr = paddr;
	s->vaddr = vaddr;
	s->src_file = file;
	s->trailing_zeroes = num_trailing_zeroes;
	s->type = type;
	s->writable = writable;
	pg_release_pd();

	return s;
}

/*! Heuristic for stack accesses. Returns true if the given address is in
    the current stack. */
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr) {
	return addr >= stack_ptr - MAX_STACK_DELTA &&
			addr < PHYS_BASE && addr >= LOWEST_STACK_ADDR;
}
