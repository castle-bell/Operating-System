#include <stdio.h>
#include "../lib/kernel/hash.h"
#include "vm/page.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"

struct vm_entry *init_vm(void *upage, struct file* file, off_t ofs, 
    int permission, enum page_type ptype, int flag)
{
    struct vm_entry* v = (struct vm_entry*)malloc(sizeof(struct vm_entry));
    if(v == NULL)
        return NULL;
    v->vpn = pg_no(upage);
    v->page = upage;
    v->file = file;
    v->ofs = ofs;
    v->permission = permission;
    v->flag = flag;
    v->p_type = ptype;
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


/* Functions for struct page */
struct page* init_page(struct vm_entry* vm_entry, struct thread* caller, enum palloc_flags flags)
{
    uint8_t kpage;
    kpage = palloc_get_page (flags);
    struct page* p = (struct page*)malloc(sizeof(struct page));
    if(p == NULL)
        return NULL;
    p->fpn = pg_no(kpage);
    p->vm_entry = vm_entry;
    p->caller = caller;
    list_insert(&p->elem,&lru_list);
}