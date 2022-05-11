#include <stdio.h>
#include <stdlib.h>
#include "../lib/kernel/hash.h"
#include "vm/page.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../threads/malloc.h"
#include "../userprog/pagedir.h"
#include "../devices/block.h"
#include "vm/swap.h"

struct vm_entry *init_vm(void *upage, struct file* file, off_t ofs, 
    int permission, enum page_type ptype, int flag)
{
    struct vm_entry* v = (struct vm_entry*)malloc(sizeof(struct vm_entry));
    if(v == NULL)
        return NULL;
    v->vpn = pg_no(upage);
    v->page = pg_round_down(upage);
    v->permission = permission;
    v->p_type = ptype;
    v->file = file;
    v->ofs = ofs;
    v->flag = flag;
    v->swap_slot = -1;
    return v;
}

struct vm_entry* find_vme(void *vaddr, struct hash* h)
{
    bool check = false;
    uintptr_t vpn = pg_no(vaddr);
    struct vm_entry *v;

    struct hash_iterator i;
    hash_first (&i, h);
    while (hash_next (&i))
    {
        v = hash_entry (hash_cur (&i), struct vm_entry, elem);
        if(v->vpn == vpn)
        {
            check = true;
            break;
        }
    }

    if(check == true)
        return v;
    else
        return NULL;
}

bool check_valid_access(struct vm_entry *v, bool write)
{
    if(v == NULL)
        return false;    
    if(write)
        if(v->permission != 1)
            return false;
    return true;
}

/* Functions for struct page */
struct page* init_page(enum palloc_flags flags)
{
    lock_acquire(&frame_lock);
    uint8_t *kpage;
    kpage = palloc_get_page (flags);
    if(kpage == NULL)
    {
        lock_release(&frame_lock);
        return NULL;
    }

    struct page* p = (struct page*)malloc(sizeof(struct page));
    if(p == NULL)
    {
        palloc_free_page(kpage);
        lock_release(&frame_lock);
        return NULL;
    }
    p->fpn = pg_no(kpage);
    p->kpage = kpage;
    p->caller = NULL;
    p->vm_entry = NULL;
    list_push_back(&lru_list,&p->elem);
    lock_release(&frame_lock);
    return p;
}

struct page* find_frame(struct vm_entry* vm_entry)
{
    struct thread* cur = thread_current();
    struct page* page;
    uintptr_t fpn = pg_no(pagedir_get_page(cur->pagedir,vm_entry->page));
    struct list_elem *e = list_begin(&lru_list);
    for(e; e!=list_end(&lru_list); e=list_next(e))
    {
        page = list_entry(e,struct page,elem);
        if(page->fpn == fpn)
            break;
    }
    if(e == list_end(&lru_list))
        return NULL;
    return page;
}

void set_page(struct page* page_frame, struct vm_entry* vm_entry, struct thread* caller)
{
    page_frame->vm_entry = vm_entry;
    page_frame->caller = caller;
}

void release_page(struct page* page_frame)
{
    /* Remove from PTE */
    if(page_frame == NULL)
        return;

    void* kpage = page_frame->kpage;

    /* Check whether install_page succeed */
    pagedir_clear_page(page_frame->caller->pagedir, page_frame->vm_entry->page);

    list_remove(&page_frame->elem);

    page_frame->kpage = NULL;
    palloc_free_page(kpage);
    page_frame->vm_entry = NULL;
    page_frame->caller = NULL;

    /* Remove from lru_list */
    free(page_frame);
}

void release_vm_entry(struct vm_entry* vm_entry)
{
    lock_acquire(&frame_lock);
    struct page* page = find_frame(vm_entry);
    if(page != NULL)
    {
        release_page(page);
    }
    else
    {
        if(vm_entry->swap_slot != -1)
        {
            block_sector_t st = SECTORS_IN_PAGE*(vm_entry->swap_slot);
            struct block *swap_block = block_get_role(BLOCK_SWAP);
            vm_entry->flag = 1;
            lock_acquire(&swap_lock);
            bitmap_set(swap_bitmap,vm_entry->swap_slot,false);
            lock_release(&swap_lock);
            vm_entry->swap_slot = -1;

            /* Set the swaped block to all 0 */
            char zero[BLOCK_SECTOR_SIZE];
            memset (zero, 0, BLOCK_SECTOR_SIZE);
            for(int j = 0;j<SECTORS_IN_PAGE;j++)
                block_write(swap_block,st+j,zero);
        }
    }
    lock_release(&frame_lock);
    vm_entry->page = NULL;
    vm_entry->file = NULL;
    hash_delete(&(thread_current()->vm),&vm_entry->elem);
    free(vm_entry);
}

static void vm_destructor(struct hash_elem* e, void* aux UNUSED)
{
    release_vm_entry(hash_entry(e,struct vm_entry,elem));
}

void remove_vm_entry(void)
{
    struct thread* cur = thread_current();
    struct vm_entry* v;

    // /* Release all vm_entry in hash table */
    // struct hash_iterator i;
    // struct hash_elem* p;
    // hash_first (&i, &cur->vm);
    // p = hash_next(&i);
    // while (p)
    // {
    //     v = hash_entry (hash_cur (&i), struct vm_entry, elem);
    //     p = hash_next(&i);
    //     printf("v->page: %p\n",v->page);
    //     // release_vm_entry(v);
    // }
    hash_destroy(&cur->vm,vm_destructor);
}

/* Get accessed bit from list_elem of lru_list */
bool get_accessed_bit(struct list_elem* e)
{
    struct page* frame = list_entry(e,struct page,elem);
    struct vm_entry* v = frame->vm_entry;
    struct thread* use = frame->caller;
    return pagedir_is_accessed(use->pagedir, v->page);
}

void set_accessed_bit(struct list_elem* e, bool accessed)
{
    struct page* frame = list_entry(e,struct page,elem);
    struct vm_entry* v = frame->vm_entry;
    struct thread* use = frame->caller;
    return pagedir_set_accessed(use->pagedir, v->page, accessed);
}

static struct list_elem* get_clock(void)
{
    if(clock == NULL)
        clock = list_begin(&lru_list);
    return clock;
}

static struct list_elem* get_next_clock(void)
{
    if(list_next(clock) == list_end(&lru_list))
        clock = list_begin(&lru_list);
    else
        clock = list_next(clock);
    return clock;
}

/* Select victim page using clock algorithm from page directory */
struct page* select_victim(void)
{
    struct list_elem *point;
    point = get_clock();
    if(point == NULL)
    {
        printf("impossible case\n");
        return NULL;
    }
    while(!get_accessed_bit(point))
    {
        set_accessed_bit(point,false);
        point = get_next_clock();

    }
    get_next_clock();
    
    /* Selected victim */
    struct page* p = list_entry(point,struct page,elem);
    return p;
}

struct mmap_file *init_mmap_file(mapid_t id, struct file *file)
{
    struct thread *cur = thread_current();
    struct mmap_file *mmap = (struct mmap_file *)malloc(sizeof(struct mmap_file));
    mmap->id = id;
    mmap->file = file;
    list_init(&mmap->vm_entry_list);
    list_push_back(&cur->mmap_list,&mmap->elem);
    return mmap;
}

void release_mmap_file(struct mmap_file* mmap)
{
    struct list_elem *v;
    v = list_begin(&mmap->vm_entry_list);
    while(v != list_end(&mmap->vm_entry_list))
    {
        struct vm_entry *vm_entry = list_entry(v,struct vm_entry,mmap_elem);
        v = list_remove(v);
        release_vm_entry(vm_entry);
    }
    list_remove(&mmap->elem);
    free(mmap);
}

bool verify_stack(void* esp, void* fault_addr)
{
    struct thread* cur = thread_current();
    struct vm_entry *v = find_vme(fault_addr,&cur->vm);
    if(v != NULL)
        return false;
    
    /* Check the validity of fault_addr */
    if(!is_user_vaddr(esp) || !is_user_vaddr(fault_addr))
        return false;
    if(fault_addr < PHYS_BASE-8*MB)
        return false;

    if(fault_addr >= esp-32)
        return true;
    return false;
}

bool expand_stack(void* fault_addr)
{
    struct thread* cur = thread_current();
    void* page_addr = pg_round_down(fault_addr);
    struct vm_entry* v = init_vm(page_addr,NULL,0,1,ANONYMOUS,1);
    if(v == NULL)
        return false;
    hash_insert(&cur->vm, &v->elem);
    return true;
}