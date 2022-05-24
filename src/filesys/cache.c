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
        if((!buf_head->data) && (buf_head->on_disk_loc == 0))
            break;
    }

    if(e == list_end(list))
        return NULL;
    
    return buf_head;
}

struct buffer_head *handler_cache_miss(block_sector_t sector_idx)
{
    /* Check If cache is full */
    struct buffer_head *buf_head;
    if((buf_head = get_free_bufhead(&list_buffer_head)) == NULL)
    {
        printf("full buffer_head, should victim\n");// victim() /* Implement later */
        return NULL;
    }

    /* Set the buf_head element and read the data from block */
    lock_acquire(&buf_head->buffer_lock);
    buf_head->on_disk_loc = sector_idx;
    buf_head->data = buffer_cache + sector_idx*BLOCK_SECTOR_SIZE;
    block_read (fs_device, sector_idx, buf_head->data);
    lock_release(&buf_head->buffer_lock);

    return buf_head;
}