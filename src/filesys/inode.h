#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "../threads/synch.h"

#define DIRECT_BLOCK_ENTRIES 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    /* Data structure for Indexed and Extensible files */
    unsigned is_directory;              /* Flag */
    block_sector_t pos;                 /* Current # of sectors */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES - 6];
    block_sector_t indirect_block_sec;
    block_sector_t double_indirect_block_sec;
  };

struct index_disk
{
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
};

/* In-memory inode. */
struct inode 
  {
    off_t dir_pos;                      /* Not use when the file is regular file */
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };



struct bitmap;
struct lock extension_lock;

void inode_init (void);
bool inode_create (block_sector_t, off_t, unsigned);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

block_sector_t get_pos(block_sector_t pos, block_sector_t *level, block_sector_t *two_lev);
block_sector_t idx_to_sector (const struct inode_disk *inode, block_sector_t idx);

#endif /* filesys/inode.h */
