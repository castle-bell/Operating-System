#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
struct bitmap *free_map;      /* Free map, one bit per sector. */

/* Initializes the free map. */
void
free_map_init (void) 
{
  free_map = bitmap_create (block_size (fs_device));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if not enough consecutive
   sectors were available or if the free_map file could not be
   written. */
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{
  block_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    {
      bitmap_set_multiple (free_map, sector, cnt, false); 
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR)
    *sectorp = sector;
  return sector != BITMAP_ERROR;
}

bool free_map_alloc (size_t sectors, struct inode_disk *inode_disk)
{
  block_sector_t sec = sectors;
  block_sector_t pos = inode_disk->pos;
  block_sector_t level = 0;
  block_sector_t two_lev = 0;
  block_sector_t flag = 0;
  block_sector_t idx = 0;
  block_sector_t start;

  struct index_disk *indirect_index;
  indirect_index = (struct index_disk *)calloc(1, sizeof(struct index_disk));
  if(inode_disk->indirect_block_sec != 0)
    block_read(fs_device, inode_disk->indirect_block_sec, indirect_index);

  struct index_disk *double_index;
  double_index = (struct index_disk *)calloc(1, sizeof(struct index_disk));
  if(inode_disk->double_indirect_block_sec != 0)
    block_read(fs_device, inode_disk->double_indirect_block_sec, double_index);

  struct index_disk *double_entry;
  double_entry = (struct index_disk *)calloc(1, sizeof(struct index_disk));

  ASSERT(sizeof(struct index_disk) == BLOCK_SECTOR_SIZE);

  while(sectors > 0)
  {
    while(!free_map_allocate(sec, &start))
    {
      if(sec == 0)
      {
        free(indirect_index);
        free(double_index);
        free(double_entry);
        return false;
      }
      sec = sec/2;
    }
    if(start == 0)
    {
      free(indirect_index);
      free(double_index);
      free(double_entry);
      return false;
    }
    sectors -= sec;
    /* Update the element of inode_disk */
    // update_block_sector(pos, sec);
    for(block_sector_t i = 0; i < sec; i++)
    {
      idx = get_pos(pos, &level, &two_lev);
      if(level == 0)
        inode_disk->direct_map_table[idx] = start + i;
      else if(level == 1)
        indirect_index->direct_map_table[idx] = start + i;
      /* level == 2 */
      else
      {
        if(double_index-> direct_map_table[two_lev] == 0)
        {
          if(!free_map_allocate(1, &double_index->direct_map_table[two_lev]))
            return false;
          flag = 1;
        }
        else
        {
          if(flag == 0)
          {
            block_read(fs_device, double_index->direct_map_table[two_lev], double_entry);
            flag = 1;
          }
        }

        double_entry->direct_map_table[idx] = start + i;
        if(idx == DIRECT_BLOCK_ENTRIES - 1)
        {
          block_write(fs_device, double_index->direct_map_table[two_lev], double_entry);
          memset(double_entry, 0, sizeof(struct index_disk));
        }
      }
      pos ++;
    }
  }
  inode_disk->pos = pos;

  /* Store to disk */
  if(indirect_index->direct_map_table[0] != 0)
  {
    if(!free_map_allocate(1, &inode_disk->indirect_block_sec))
      return false;
    block_write(fs_device, inode_disk->indirect_block_sec, indirect_index);
  }
  if(double_entry->direct_map_table[two_lev] != 0)
  {
    block_write(fs_device, double_index->direct_map_table[two_lev], double_entry);
  }
  if(double_index->direct_map_table[0] != 0)
  {
    if(!free_map_allocate(1, &inode_disk->double_indirect_block_sec))
      return false;
    block_write(fs_device, inode_disk->double_indirect_block_sec, double_index);
  }

  free(indirect_index);
  free(double_entry);
  free(double_index);

  ASSERT(sectors == 0);

  return true;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (block_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);
}

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map), 0))
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}
