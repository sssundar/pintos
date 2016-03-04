/*
 * page.h
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#ifndef SRC_VM_PAGE_H_
#define SRC_VM_PAGE_H_

/*! Bitmask for "writable" bit in protection field in supplemental page
    table element. */
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
 	the PUSHA instruction can push 32 bytes onto stack, thus this number. */
#define MAX_STACK_DELTA		32

/*! Used in assertions to make sure supplemental page entry made it to
    page fault handler. */
#define PG_MAGIC 			0xe8f291ad

/*! All the possible page sources including files that are memory-mapped,
    files that are loaded as executables, anonymous files (for pages that will
    be zeroed), and "other" sources for pages that are used to e.g. extend
    the stack. */
enum pgtype { MMAPD_FILE_PG, EXECD_FILE_PG, ZERO_PG, OTHER_PG };

/*! Supplemental page table element. There is one of these per allocated page
    for each user process. Instead of putting these in a hash table we
    put them somewhere in kernel space then pack a pointer to each in its
    corresponding page table entry. */
struct spgtbl_elem {
	/*! Owned by page.c. */
	/**@{*/

	void *paddr;			/*!< Physical address of this page. */
	void *vaddr; 			/*!< Virtual address of this page. "Key". */
	bool writable; 			/*!< If true is read/write, else is read only. */

	enum pgtype type;		/*!< The sort of page this is. */

	/*! Index of page in swap. BITMAP_ERROR if not there. */
	unsigned long long swap_idx;

	//--------------------- File related data below ---------------------------
	int mid;				/*!< Mmap'd file id. */
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

	uint32_t magic;
	/**@}*/
};

void pg_init(void);
void pg_lock_pd(void);
void pg_release_pd(void);
struct spgtbl_elem *pg_put(int mid, int fd, off_t ofs, void *paddr,
		void *vaddr, struct file *file, uint32_t num_trailing_zeroes,
		bool writable, enum pgtype type, unsigned long long swap_idx);
bool pg_is_valid_stack_addr(void *addr, void *stack_ptr);

#endif /* SRC_VM_PAGE_H_ */
