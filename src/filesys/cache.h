#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/filesys.h"

void cache_read (void *data, void *buffer, size_t size);
struct buffer_head *get_free_bufhead(struct list *list);

struct buffer_head *handler_cache_miss(block_sector_t sector_idx);


#endif /* filesys/cache.h */