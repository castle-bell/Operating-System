#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if(pos < inode->data.length)
    // return inode->data.direct_map_table[pos / BLOCK_SECTOR_SIZE];
    return idx_to_sector(&(inode->data), pos / BLOCK_SECTOR_SIZE);
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&extension_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, unsigned is_directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_directory = is_directory;
      if (free_map_alloc (sectors, disk_inode)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
               
              for (i = 0; i < sectors; i++) 
              {
                block_sector_t sec = idx_to_sector(disk_inode, i);
                block_write (fs_device, sec, zeros);
              }
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->dir_pos = 0;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          for(int i = 0; i<inode->data.pos; i++)
          {
            free_map_release(idx_to_sector(&inode->data, i), 1);
          }
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct buffer_head *buf_head = find_buffer_head(sector_idx);

      if(buf_head == NULL)
      {
        buf_head = handler_cache_miss(sector_idx);
      }

      /* Read full sector directly into caller's buffer. */
      /* Implement cache_read */
      
      ASSERT(buf_head);
      ASSERT(buf_head->is_used);

      lock_acquire(&buf_head->buffer_lock);

      // block_read (fs_device, sector_idx, buf_head->data);
      cache_read (buf_head->data + sector_ofs, buffer + bytes_read, chunk_size);

      /* Update the buf_head element */
      buf_head->accessed = true;
      lock_release(&buf_head->buffer_lock);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* Implement extend the inode */

  struct inode_disk *disk_inode = &inode->data;
  int old_length = disk_inode->length;
  int write_end = offset + size - 1;
  int add_len = write_end - (disk_inode->pos * BLOCK_SECTOR_SIZE) + 1; /* # of bytes to write */

  if(write_end > old_length - 1)
  {
    if(add_len > 0)
    {
      int add_sec = (add_len)/BLOCK_SECTOR_SIZE + 1;
      int old_pos = disk_inode->pos;
      if(!free_map_alloc(add_sec, disk_inode))
        return -1;

      /* Fill the allocated blocks with zero */
      for(int num = 0; num < disk_inode->pos - old_pos; num ++)
      {
        block_sector_t alloc = idx_to_sector(disk_inode, old_pos + num);
        char zeros[BLOCK_SECTOR_SIZE];
        memset(zeros, 0, BLOCK_SECTOR_SIZE);
        block_write(fs_device, alloc, zeros);
      }
    }
    disk_inode->length += write_end - (old_length - 1);
    block_write (fs_device, inode->sector, disk_inode);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct buffer_head *buf_head = find_buffer_head(sector_idx);

      if(buf_head == NULL)
      {
        buf_head = handler_cache_miss(sector_idx);
      }

      ASSERT(buf_head);
      ASSERT(buf_head->is_used);

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // /* Write full sector directly to disk. */
          lock_acquire(&buf_head->buffer_lock);

          cache_read (buffer + bytes_written, buf_head->data, chunk_size);
          buf_head->accessed = true;
          buf_head->dirty = true;

          lock_release(&buf_head->buffer_lock);
        }
      else 
        {
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */

          lock_acquire(&buf_head->buffer_lock);

          if (!(sector_ofs > 0 || chunk_size < sector_left))
            memset (buf_head->data, 0, BLOCK_SECTOR_SIZE);

          cache_read (buffer + bytes_written, buf_head->data + sector_ofs, chunk_size);
          buf_head->dirty = true;
          buf_head->accessed = true;

          lock_release(&buf_head->buffer_lock);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

block_sector_t get_pos(block_sector_t pos, block_sector_t *level, block_sector_t *two_lev)
{
  ASSERT(pos <= DIRECT_BLOCK_ENTRIES*DIRECT_BLOCK_ENTRIES + 
                                   2*(DIRECT_BLOCK_ENTRIES)- 6 - 1);

  *two_lev = 0;

  if(pos <= (DIRECT_BLOCK_ENTRIES - 6) - 1)
  {
    *level = 0;
    return pos;
  }
  /* Pos is Bigger than size of direct_map_table */
  else if(pos <= (2*DIRECT_BLOCK_ENTRIES - 6) - 1)
  {
    *level = 1;
    return (pos- (DIRECT_BLOCK_ENTRIES - 6));
  }
  else
  {
    *level = 2;
    pos -= 2*DIRECT_BLOCK_ENTRIES - 6;
    *two_lev = pos/DIRECT_BLOCK_ENTRIES;
    pos = pos % DIRECT_BLOCK_ENTRIES;

    return pos;
  }

  return 0;
}

block_sector_t idx_to_sector (const struct inode_disk *inode, block_sector_t idx)
{
  block_sector_t level;
  block_sector_t two_lev;
  block_sector_t pos;
  struct index_disk index;

  pos = get_pos(idx, &level, &two_lev);
  if(level == 0)
    return inode->direct_map_table[pos];
  else if(level == 1)
  {
    block_read(fs_device, inode->indirect_block_sec, &index);
    return index.direct_map_table[pos];
  }
  else
  {
    block_read(fs_device, inode->double_indirect_block_sec, &index);
    block_read(fs_device, index.direct_map_table[two_lev], &index);
    return index.direct_map_table[pos];
  }
}