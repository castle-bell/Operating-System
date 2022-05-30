#include "filesys/cache.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "../devices/block.h"

void cache_read (void *data, void *buffer, size_t size)
{
  ASSERT(data);
  ASSERT(buffer);
  ASSERT(size >= 0);

  for(int i = 0; i< size; i++)
  {
    *((char*)buffer+i) = *((char *)data+i);
  }
}

struct buffer_head *get_free_bufhead(struct list *list)
{
    struct list_elem *e;
    struct buffer_head *buf_head;

    for(e = list_begin(list); e != list_end(list); e = list_next(e))
    {
        buf_head = list_entry(e, struct buffer_head, elem);
        if(buf_head->is_used == false)
            break;
    }

    if(e == list_end(list))
        return NULL;
    
    return buf_head;
}

/* Implement the victim part */
static struct list_elem* get_cache_clock(struct list *list)
{
    if(cache_clock == NULL)
        cache_clock = list_begin(list);
    return cache_clock;
}

static struct list_elem* get_next_cache_clock(struct list *list)
{
    if(list_next(cache_clock) == list_end(list))
        cache_clock = list_begin(list);
    else
        cache_clock = list_next(cache_clock);
    return cache_clock;
}

struct buffer_head *select_victim_entry(struct list *list)
{
    struct list_elem *point;
    point = get_cache_clock(&list_buffer_head);
    
    ASSERT(point);

    struct buffer_head *buf_point = list_entry(point, struct buffer_head, elem);
    while(buf_point->accessed)
    {
        buf_point->accessed = false;
        point = get_next_cache_clock(&list_buffer_head);
        buf_point = list_entry(point, struct buffer_head, elem);
    }
    get_next_cache_clock(&list_buffer_head);

    return buf_point;
}

struct buffer_head *handler_cache_miss(block_sector_t sector_idx)
{
    /* Check If cache is full */
    struct buffer_head *buf_head;
    while((buf_head = get_free_bufhead(&list_buffer_head)) == NULL)
    {
        struct buffer_head *victim = select_victim_entry(&list_buffer_head);
        ASSERT(!victim->accessed);

        if(victim->dirty)
            write_dirty(victim);

        lock_acquire(&victim->buffer_lock);

        memset(victim->data, 0, BLOCK_SECTOR_SIZE);
        victim->is_used = false;
        victim->dirty = false;
        victim->accessed = false;
        victim->on_disk_loc = UINT32_MAX;
        victim->data = NULL;

        lock_release(&victim->buffer_lock);
    }

    /* Set the buf_head element and read the data from block */
    lock_acquire(&buf_head->buffer_lock);

    buf_head->on_disk_loc = sector_idx;
    buf_head->data = buffer_cache + buf_head->idx * BLOCK_SECTOR_SIZE;
    buf_head->is_used = true;
    block_read (fs_device, sector_idx, buf_head->data);

    lock_release(&buf_head->buffer_lock);
    return buf_head;
}

void write_dirty(struct buffer_head *buf_head)
{
    ASSERT(buf_head->dirty);


    lock_acquire(&buf_head->buffer_lock);

    block_write (fs_device, buf_head->on_disk_loc, buf_head->data);


    lock_release(&buf_head->buffer_lock);


    buf_head->dirty = false;
}

void write_all_dirty(struct list *list)
{
    struct list_elem *e;
    struct buffer_head *buf_head;

    for(e = list_begin(list); e != list_end(list); e = list_next(e))
    {
        buf_head = list_entry(e, struct buffer_head, elem);
        if(buf_head->dirty)
            write_dirty(buf_head);
    }
}