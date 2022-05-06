#include <stdio.h>
#include "../lib/kernel/hash.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../userprog/pagedir.h"
#include "../devices/block.h"
#include "../lib/kernel/bitmap.h"
#include "../threads/thread.h"
#include "vm/swap.h"

#define SECTORS_IN_PAGE 8

/* A block device. */
struct block
  {
    struct list_elem list_elem;         /* Element in all_blocks. */

    char name[16];                      /* Block device name. */
    enum block_type type;                /* Type of block device. */
    block_sector_t size;                 /* Size in sectors. */

    const struct block_operations *ops;  /* Driver operations. */
    void *aux;                          /* Extra data owned by driver. */

    unsigned long long read_cnt;        /* Number of sectors read. */
    unsigned long long write_cnt;       /* Number of sectors written. */
  };

struct bitmap* swap_init(void)
{
    struct block *swap_partition = block_get_role(BLOCK_SWAP);
    if(swap_partition == NULL)
        sys_exit(-1);
    size_t num_bits = (swap_partition->size)/(SECTORS_IN_PAGE);

    struct bitmap* bitmap = bitmap_create(num_bits);
    if(bitmap == NULL)
        sys_exit(-1);
    lock_init(&swap_lock);
    return bitmap;
}

bool write_partition(struct page* page, enum page_type type)
{
    if(type == EXEC || type == ANONYMOUS)
    {
        struct block *swap_p = block_get_role(BLOCK_SWAP);
        
        /* Set lock */
        lock_acquire(&swap_lock);
        size_t free_idx = bitmap_scan_and_flip(swap_bitmap,0,1,false);
        lock_release(&swap_lock);
        
        if(free_idx == BITMAP_ERROR)
        {
            printf("Full swap partition\n");
            return false;
        }
        block_sector_t st_start = SECTORS_IN_PAGE*free_idx;
        for(int i = 0;i<SECTORS_IN_PAGE;i++)
            block_write(swap_p,st_start+i,page->kpage+i*BLOCK_SECTOR_SIZE);
        page->vm_entry->flag = 0;
        page->vm_entry->swap_slot = free_idx;
    }
    /* Type == FILE */
    else
    {
        struct vm_entry *vm_entry = page->vm_entry;
        off_t pos = file_tell(vm_entry->file);
        file_write(vm_entry->file,vm_entry->page,PGSIZE);
        /* Set the origin position */
        file_seek(vm_entry->file,pos);
    }

    /* Set page is not dirty */
    pagedir_set_dirty(page->caller->pagedir, page->vm_entry->page, false);
    return true;
}


bool swap(void)
{
    if(!lock_held_by_current_thread(&frame_lock))
        lock_acquire(&frame_lock);
    if(swap_bitmap == NULL)
        swap_bitmap = swap_init();
    struct page* victim = select_victim();
    // printf("victim: %p, frame: %p\n",victim->vm_entry->page,victim->kpage);
    if(victim == NULL)
    {
        printf("victim is null\n");
        if(lock_held_by_current_thread(&frame_lock))
            lock_release(&frame_lock);
        sys_exit(-1);
    }

    bool status = swap_out(victim);
    if(lock_held_by_current_thread(&frame_lock))
        lock_release(&frame_lock);
    return status;
}

bool swap_out(struct page* victim)
{
    if(victim == NULL)
        return false;

    /* Check the victim's dirty bit */
    struct vm_entry* v = victim->vm_entry;
    struct thread* use = victim->caller;
    bool dirty = pagedir_is_dirty(use->pagedir, v->page);

    switch(victim->vm_entry->p_type)
    {
        case EXEC:
            if(dirty == true)
            {
                write_partition(victim,EXEC);
                victim->vm_entry->p_type = ANONYMOUS;
                release_page(victim);
            }
            else
                release_page(victim);
            break;
        case FILE:
            if(dirty == true)
            {
                write_partition(victim,FILE);
                release_page(victim);
            }
            else
                release_page(victim);
            break;
        case ANONYMOUS:
            write_partition(victim,ANONYMOUS);
            release_page(victim);
            break;
        default:
            printf("Impossible case\n");
            break;
    }
    return true;
}

void swap_in(struct vm_entry *vm_entry, void *kpage)
{
    block_sector_t st = SECTORS_IN_PAGE*(vm_entry->swap_slot);
    struct block *swap_block = block_get_role(BLOCK_SWAP);
    for(int i = 0;i<SECTORS_IN_PAGE;i++)
        block_read(swap_block,st+i,kpage+i*BLOCK_SECTOR_SIZE);
    vm_entry->flag = 1;
    bitmap_set(swap_bitmap,vm_entry->swap_slot,false);
    vm_entry->swap_slot = -1;

    /* Set the swaped block to all 0 */
    char zero[BLOCK_SECTOR_SIZE];
    memset (zero, 0, BLOCK_SECTOR_SIZE);
    for(int j = 0;j<SECTORS_IN_PAGE;j++)
        block_write(swap_block,st+j,zero);
}