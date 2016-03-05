/*
 * swap.h
 *
 *  Created on: Feb 29, 2016
 *      Author(s): Mukelyan, Luo
 */

#ifndef SRC_VM_SWAP_H_
#define SRC_VM_SWAP_H_

#include <stdbool.h>

#define SECTORS_PER_PAGE 	(PGSIZE / BLOCK_SECTOR_SIZE)

void sp_init(void);
bool sp_get(unsigned long long idx, void *buf);
unsigned long long sp_put(void *paddr);

#endif /* SRC_VM_SWAP_H_ */
