#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "../threads/synch.h"
#include "../devices/block.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

/* Define data structure for buffer cache */
struct buffer_head
{
    bool dirty; /* Dirty flag */
    bool accessed; /* Accessed flag */
    struct lock buffer_lock; /* Lock for accessing buffer_head */
    block_sector_t on_disk_loc; /* On-disk location */
    void *data; /* Virtual address of buffer cache entry */
    struct list_elem elem;
};

char *buffer_cache;
struct list list_buffer_head;

void buffer_head_init (void);
struct buffer_head *find_buffer_head(block_sector_t idx);

#endif /* filesys/filesys.h */
