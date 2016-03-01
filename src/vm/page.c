/*
 * page.c
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#include <stdbool.h>
#include <hash.h>
#include "threads/vaddr.h"
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

/*! Returns true if the given address is in the current stack. */
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr) {
	return addr >= stack_ptr - MAX_STACK_DELTA &&
			addr < PHYS_BASE && addr >= LOWEST_STACK_ADDR;
}
