#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/filesys.h"

struct list_elem* cache_clock;

void cache_read (void *data, void *buffer, size_t size);
struct buffer_head *get_free_bufhead(struct list *list);

static struct list_elem* get_cache_clock(struct list *list);
static struct list_elem* get_next_cache_clock(struct list *list);
struct buffer_head *select_victim_entry(struct list *list);
struct buffer_head *handler_cache_miss(block_sector_t sector_idx);
void write_dirty(struct buffer_head *buf_head);
void write_all_dirty(struct list *list);

#endif /* filesys/cache.h */