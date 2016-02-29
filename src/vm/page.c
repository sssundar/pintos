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

/*! Returns true if the given address is in the current stack. */
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr) {
	return addr >= stack_ptr - MAX_STACK_DELTA &&
			addr < PHYS_BASE && addr >= LOWEST_STACK_ADDR;
}
