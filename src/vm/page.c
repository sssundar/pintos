/*
 * page.c
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#include <stdbool.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "vm/page.h"

/*! Used for initializing hash tables in thread.c's init_thread. */
bool pg_hash_less (const struct hash_elem *a,
		const struct hash_elem *b, void *aux UNUSED) {
	struct spgtbl_elem * spgtbl_elem_a =
			hash_entry(a, struct spgtbl_elem, helm);
	struct spgtbl_elem * spgtbl_elem_b =
			hash_entry(b, struct spgtbl_elem, helm);
	return spgtbl_elem_a->vaddr < spgtbl_elem_b->vaddr;
}

/*! Used for initializing hash tables in thread.c's init_thread. */
unsigned pg_hash_func (const struct hash_elem *e, void *aux UNUSED) {
	struct spgtbl_elem * spgtbl_elem_tmp =
				hash_entry(e, struct spgtbl_elem, helm);
	return hash_bytes ((const void *) spgtbl_elem_tmp,
			sizeof(struct spgtbl_elem));
}

/*! Allocate and populate a new supplemental page table entry. Return it.
 */
void *pg_put(int fd, off_t ofs, void *paddr, void *vaddr, struct file *file,
		uint32_t num_trailing_zeroes, bool writable, enum pgtype type) {

	struct spgtbl_elem *s = (struct spgtbl_elem *) malloc(
	        		sizeof(struct spgtbl_elem));
	struct thread *t = thread_current();

	// If we can't even allocate a supplemental page table entry then there's
	// not enough memory, so panic!
	if (s == NULL) {
		PANIC("Can't allocate supplemental page table entry.");
		NOT_REACHED();
	}

	s->magic = PG_MAGIC;
	switch (type) {
	case MMAPD_FILE_PG:
		// TODO nothing for now?
		break;
	case EXECD_FILE_PG:
		ASSERT(num_trailing_zeroes != PGSIZE);
		s->fd = fd;
		s->offset = ofs;
		s->paddr = paddr; // TODO needs to be set in page fault handler
		s->vaddr = vaddr;
		s->src_file = file;
		s->trailing_zeroes = num_trailing_zeroes;
		s->type = EXECD_FILE_PG;
		s->writable = writable;
		hash_insert(t->spgtbl, &s->helm);
		break;
	case ZERO_PG:
		ASSERT(num_trailing_zeroes == PGSIZE);
		s->fd = -1;
		s->offset = -1;
		s->paddr = paddr; // TODO needs to be set in page fault handler
		s->vaddr = vaddr;
		s->src_file = NULL;
		s->trailing_zeroes = PGSIZE;
		s->type = ZERO_PG;
		s->writable = writable;
		hash_insert(t->spgtbl, &s->helm);
		break;
	case OTHER_PG:
		// TODO nothing for now?
		break;
	default:
		PANIC("Impossible page type in pg_put.");
		NOT_REACHED();
	}

	return s;
}

/*! Returns true if the given address is in the current stack. */
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr) {
	return addr >= stack_ptr - MAX_STACK_DELTA &&
			addr < PHYS_BASE && addr >= LOWEST_STACK_ADDR;
}
