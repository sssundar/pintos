/*
 * frame.h
 *
 *  Created on: Feb 29, 2016
 *      Author: Mukelyan
 */

#ifndef SRC_VM_FRAME_H_
#define SRC_VM_FRAME_H_

/*! Bitmask for "in use" bit in flags field in frame table element. */
#define IN_USE_MASK 0x00000001

/*! Bitmask for "is pinned" bit in flags field in frame table element. */
#define PIN_MASK 	0x00000002

/*! Frame table element. The actual physical address that a frame table
    element corresponds to depends on the index of a frame table element
    in an array. For instance, the ftbl_elem at index 2 should correspond
    to the user address 2 * PGSIZE. The largest index will then be
    32mb / 4096m = 16384 - 1, since Pintos is initialized with 64mb of user
    memory (it has 64 mb total).

    Note that, by our convention, kernel pages are not page-able (i.e.,
    evict-able) because they are not present in the frame table. The eviction
    code will never see them. */
struct ftbl_elem {
	/*! Owned by frame.c. */
	/**@{*/

	void *corr_vaddr;			/*!< Corresponding virtual address. */

	/*! Info about the owning thread including its page directory pointer,
	    which is needed to access the dirty and accessed bits in the
	    corresponding page table entry. */
	struct thread *tinfo;

	/*! Contains flags for this frame table element:

	 \verbatim
		- Bit 0: "In use" bit is set when this frame is in use. This is the
		  least significant bit.
		- Bit 1: "Pinned" bit is set when this frame should not be evicted.
	 \endverbatim
	 */
	uint32_t flags;

	int mid;				/*! Memory mapped file ID, -1 if not present. */
	enum pgtype type; 		/*! Type of the page associated with this frame. */
	bool writable;			/*! Whether this frame is writable. */

	//--------------------- File related data below ---------------------------
	int fd;					/*!< File descriptor for src file. -1 if none. */
	struct file *src_file;  /*!< Source file, could be null if none. */
	/*! The number of zeroes that follow the last bit of this page. Will be set
	    to 0 for (most) pages because they're fully used, but the last page
	    for a given file will have a trailing zeroes count between 0 and
	    PGSIZE, inclusive. */
	uint32_t trailing_zeroes;
	off_t offset;			/*!< Offset in the corresponding src file. */
	//--------------------- File related data above ---------------------------

	/**@}*/
};

void fr_use(void *paddr);
void fr_unuse(void *paddr);
void fr_pin(void *paddr);
void fr_unpin(void *paddr);
void ftbl_get_lock(void);
void ftbl_release_lock(void);
void fr_init_tbl(void);
void *fr_alloc_page(void *vaddr, enum pgtype type, bool writable,
		int mid, int num_trailing_zeroes);

extern struct lock pglock;

#endif /* SRC_VM_FRAME_H_ */
