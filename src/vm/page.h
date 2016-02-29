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

/*! Supplemental page table element. These are loaded up into an array...

	TODO use a single global mutex for evictions (i.e. do one eviction at a
	time by acquiring the lock before performing eviction)

	TODO Donnie suggested that we use these elements:
	  - physical addr (use this to get to frame to do concurrency control)
	  - user virtual addr
	  - readonly or writable?
	  - where is data from?
	  - # bytes to read
	  - starting offset in file
	  - file that this page is from
	  - hash table element

	TODO note that we have kernel malloc

	TODO note that we have ONE OF THESE FOR EACH PAGE

 */
struct spgtbl_elem {
	/*! Owned by frame.c. */
	/**@{*/

	void *start;			/*!< Start of this memory segment. */
	void *end; 				/*!< End of this memory segment. */
	uint32_t prot;        	/*!< Readable/writable flags. */
	enum pgtype type;		/*!< The sort of page this is. */

	//--------------------- File related data below ---------------------------
	int fd;					/*!< File descriptor for src file. -1 if none. */
	struct file *src_file;  /*!< Source file, could be null if none. */
	/*! The number of zeroes that follow the last bit of this page. Will be set
	    to 0 for (most) pages because they're fully used, but the last page
	    for a given file will have a trailing zeroes count between 0 and
	    PGSIZE, inclusive.
	 */
	uint32_t trailing_zeroes;
	off_t offset;			/*!< Offset in the corresponding src file. */
	//--------------------- File related data above ---------------------------

	/**@}*/
};

bool pg_is_readable(struct spgtbl_elem *spgtbl);
bool pg_is_writable(struct spgtbl_elem *spgtbl);
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr);

#endif /* SRC_VM_PAGE_H_ */
