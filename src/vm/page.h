/*
 * page.h
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#ifndef SRC_VM_PAGE_H_
#define SRC_VM_PAGE_H_

/*! Bitmask for "writable" bit in protection field in supplemental page
    table element.
*/
#define PROT_WRITE_MASK		0x00000002

/*! Bitmask for "readable" bit in protection field in supplemental page
    table element.
*/
#define PROT_READ_MASK		0x00000001

/*! Maximum stack size. */
#define STACK_SIZE_MB 		8

/*! Lowest possible address for a stack element. */
#define LOWEST_STACK_ADDR	(void *)(PHYS_BASE - STACK_SIZE_MB * (1 << 20))

/*! Maximum distance below bottom of stack that is still "in" stack. NB
 	the PUSHA instruction can push 32 bytes onto stack, thus this number.
*/
#define MAX_STACK_DELTA		32

/*! All the possible page types including memory-mapped files, files loaded as
    executables, pages that are supposed to be zeroed, and any other kinds of
    pages (like those in the stack) which can be sent back and forth from
    swap.
*/
enum pgtype { MMAPD_FILE_PG, EXECD_FILE_PG, ZERO_PG, OTHER_PG };

/*! Supplemental page table element. There is one of these per allocated page
    for each user process. They are loaded up in a hash table and live in
    the kernel pool of pages.

	TODO use a single global mutex for evictions (i.e. do one eviction at a
	time by acquiring the lock before performing eviction)
 */
struct spgtbl_elem {
	/*! Owned by frame.c. */
	/**@{*/

	void *paddr;			/*!< Physical address of this page. */
	void *vaddr; 			/*!< Virtual address of this page. "Key". */
	bool writable; 			/*!< If true is read/write, else is read only. */

	enum pgtype type;		/*!< The sort of page this is. */

	//--------------------- File related data below ---------------------------
	int fd;					/*!< File descriptor for src file. -1 if none. */
	struct file *src_file;  /*!< Source file, could be null if none. */
	/*! The number of zeroes that follow the last bit. Will be set to 0 for
	    most pages of a file because they're fully used, but the last page
	    for a given file will have a trailing zeroes count between 0 and
	    PGSIZE, inclusive. To get the number of bytes that were read for this
	    supplemental page table entry just do PGSIZE - trailing_zeroes.
	 */
	uint32_t trailing_zeroes;
	off_t offset;			/*!< Offset in the corresponding src file. */
	//--------------------- File related data above ---------------------------

	struct hash_elem helm;  /*!< Is a Pintos hash element. */
	/**@}*/
};

bool pg_hash_less (const struct hash_elem *a,
		const struct hash_elem *b, void *aux);

unsigned pg_hash_func (const struct hash_elem *e, void *aux);

bool pg_is_valid_stack_addr(void *addr, void *stack_ptr);

#endif /* SRC_VM_PAGE_H_ */
