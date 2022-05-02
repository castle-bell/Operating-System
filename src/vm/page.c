#include <stdio.h>
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
    uint8_t *kpage;
    kpage = palloc_get_page (flags);
    struct page* p = (struct page*)malloc(sizeof(struct page));
    if(p == NULL)
        return NULL;
    p->fpn = pg_no(kpage);
    p->kpage = kpage;
    list_push_back(&lru_list,&p->elem);
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

    /* Free the kpage(코드 보니까 위에도 여기에 포함 되는 것 같음) */
    palloc_free_page(page_frame->kpage);

    /* Remove from lru_list */
    list_remove(&page_frame->elem);
    free(page_frame);
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
    list_remove(&vm_entry->elem);
    free(vm_entry);
}

/* Get accessed bit from list_elem of lru_list */
bool get_accessed_bit(struct list_elem* e)
{
    struct page* frame = list_entry(e,struct page,elem);
    struct vm_entry* v = frame->vm_entry;
    struct thread* cur = thread_current();
    return pagedir_is_accessed(cur->pagedir, v->page);
}

void set_accessed_bit(struct list_elem* e, bool accessed)
{
    struct page* frame = list_entry(e,struct page,elem);
    struct vm_entry* v = frame->vm_entry;
    struct thread* cur = thread_current();
    return pagedir_set_accessed(cur->pagedir, v->page, accessed);
}

/* Select victim page using clock algorithm from page directory */
struct page* select_victim(void)
{
    struct list_elem *point;
    point = list_begin(&lru_list);
    if(point == NULL)
    {
        printf("impossible case\n");
        return NULL;
    }
    while(!get_accessed_bit(point))
    {
        set_accessed_bit(point,false);
        if(list_next(point) == list_end(&lru_list))
            point = list_begin(&lru_list);
        else
            point = list_next(point);
    }
    
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
    for(v; v!=list_end(&mmap->vm_entry_list); v=list_next(v))
    {
      struct vm_entry *vm_entry = list_entry(v,struct vm_entry,mmap_elem);
      list_remove(v);
      release_vm_entry(vm_entry);
    }
    free(mmap);
}