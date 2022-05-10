#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdio.h>
#include "../lib/kernel/hash.h"
#include "../lib/user/syscall.h"
#include "../filesys/off_t.h"
#include "../threads/palloc.h"

#define MB 1024*1024

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

    /* Store the location where we swap out(using bitmap idx) */
    size_t swap_slot;

    /* Use hash structure */
    struct hash_elem elem;
    struct list_elem mmap_elem;
};

/* Implement structure of page(abstraction of physical page frame) */
struct page{
    /* Physical page frame number */
    uintptr_t fpn;
    void *kpage;
    struct vm_entry *vm_entry;
    struct thread *caller;
    struct list_elem elem;
};

struct mmap_file{
    mapid_t id;
    struct file *file;
    struct list_elem elem;
    struct list vm_entry_list;
};

struct vm_entry* init_vm(void *upage, struct file* file, off_t ofs, 
    int permission, enum page_type ptype, int flag);

struct vm_entry* find_vme(void *vaddr, struct hash* hash_table);
bool check_valid_access(struct vm_entry *v, bool write);
void release_vm_entry(struct vm_entry* vm_entry);

struct page* init_page(enum palloc_flags flags);
struct page* find_frame(struct vm_entry* vm_entry);
void set_page(struct page* page_frame, struct vm_entry* vm_entry, struct thread* caller);
void release_page(struct page* page_frame);

void remove_vm_entry(void);

bool get_accessed_bit(struct list_elem* e);
void set_accessed_bit(struct list_elem* e, bool accessed);

struct page* select_victim(void);

bool write_partition(struct page* page, enum page_type type);

struct mmap_file *init_mmap_file(mapid_t id, struct file *file);
void release_mmap_file(struct mmap_file* mmap);

bool verify_stack(void* esp, void* fault_addr);
bool expand_stack(void* fault_addr);

#endif /* vm/page.h */