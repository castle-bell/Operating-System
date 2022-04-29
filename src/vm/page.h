#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdio.h>
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"
#include "../threads/palloc.h"

/* Page type */
enum page_type{
    EXEC,
    FILE,
    ANONYMOUS
};

/* Implement structure of vm_entry(Supplemental page table) */
struct vm_entry{
    /* VPN */
    uintptr_t vpn;
    void* page;

    /* Permission, 0 means read-only, 1 means write */
    int permission;

    /* Page type */
    enum page_type p_type;

    /* Reference to the file object and offset */
    struct file* file;
    off_t ofs;
    size_t read_bytes;
    size_t zero_bytes;

    /* Amount of data in the page */
    /* Location in the swap area */
    /* In-memory flag, 0 means in disk, 1 means in memory */
    int flag;

    /* Use hash structure */
    struct hash_elem elem;
};

/* Implement structure of page(abstraction of physical page frame) */
struct page{
    /* Physical page frame number */
    uintptr_t fpn;
    struct vm_entry *vm_entry;
    struct thread *caller;
    struct list_elem elem;
};

struct vm_entry* init_vm(void *upage, struct file* file, off_t ofs, 
    int permission, enum page_type ptype, int flag);

struct vm_entry* find_vme(void *vaddr, struct hash* hash_table);

struct page* init_page(struct vm_entry* vm_entry, struct thread* caller, enum palloc_flags flags);


#endif /* vm/page.h */