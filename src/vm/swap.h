#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdio.h>
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"
#include "../threads/palloc.h"
#include "vm/page.h"

struct bitmap* swap_init(void);
bool write_partition(struct page* page, enum page_type type);
bool swap(void);
void swap_in(struct vm_entry *vm_entry, void *kpage);
bool swap_out(struct page* victim);

#endif /* vm/swap.h */