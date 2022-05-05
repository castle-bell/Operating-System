#include <stdio.h>
#include <stdlib.h>
#include "../lib/kernel/hash.h"
#include "vm/page.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../userprog/pagedir.h"
#include "../devices/block.h"

struct vm_entry *init_vm(void *upage, struct file* file, off_t ofs, 
    int permission, enum page_type ptype, int flag)
{
    struct vm_entry* v = (struct vm_entry*)malloc(sizeof(struct vm_entry));
    if(v == NULL)
        return NULL;
    v->vpn = pg_no(upage);
    v->page = upage;
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
    lock_acquire(&frame_lock);
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
    {
        lock_release(&frame_lock);
        return NULL;
    }
    lock_release(&frame_lock);
    return page;
}

void set_page(struct page* page_frame, struct vm_entry* vm_entry, struct thread* caller)
{
    lock_acquire(&frame_lock);
    page_frame->vm_entry = vm_entry;
    page_frame->caller = caller;
    lock_release(&frame_lock);
}

void release_page(struct page* page_frame)
{
    /* Remove from PTE */
    lock_acquire(&frame_lock);
    pagedir_clear_page(page_frame->caller->pagedir, page_frame->vm_entry->page);

    list_remove(&page_frame->elem);

    /* Free the kpage(코드 보니까 위에도 여기에 포함 되는 것 같음) */
    palloc_free_page(page_frame->kpage);

    /* Remove from lru_list */
    free(page_frame);
    lock_release(&frame_lock);
}

void release_vm_entry(struct vm_entry* vm_entry)
{
    struct page* page = find_frame(vm_entry);
    if(page != NULL)
    {
        release_page(page);
    }
    vm_entry->page = NULL;
    vm_entry->file = NULL;
    hash_delete(&(thread_current()->vm),&vm_entry->elem);
    free(vm_entry);
}

void remove_lru_elem(void)
{
    struct list_elem* e;
    e = list_begin(&lru_list);
    while(e != list_end(&lru_list))
    {
        struct list_elem* n = list_next(e);
        struct page* p = list_entry(e,struct page,elem);
        if((p->caller == thread_current()))
        {
            release_page(p);
        }
        e = n;
    }
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
}

/* Select victim page using clock algorithm from page directory */
struct page* select_victim(void)
{
    lock_acquire(&frame_lock);
    struct list_elem *point;
    point = get_clock();
    if(point == NULL)
    {
        printf("impossible case\n");
        lock_release(&frame_lock);
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
    lock_release(&frame_lock);
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
    if(fault_addr+32 < esp)
        return false;
    return true;
}

struct vm_entry* expand_stack(void* fault_addr)
{
    struct vm_entry* stack_vm;
    void* next_page_addr;
    struct thread* cur = thread_current();

    next_page_addr = find_vme((uint8_t*)fault_addr-PGSIZE, &cur->vm)->page + PGSIZE;
    struct vm_entry* v = init_vm(next_page_addr,NULL,0,1,ANONYMOUS,1);
    if(v == NULL)
        return NULL;
    hash_insert(&cur->vm, &v->elem);
    return v;
}