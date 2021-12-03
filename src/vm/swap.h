#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>

void init_swap_valid_table();
void swap_in(struct spte *page, void *kva);
int swap_out(void *kva)

#endif