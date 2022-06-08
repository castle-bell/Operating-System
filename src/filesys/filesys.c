#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "../threads/malloc.h"
#include "../threads/thread.h"
#include "../lib/kernel/hash.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
static unsigned dentry_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool dentry_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

/* Initializes the buffer_head */
void buffer_head_init (void)
{
  cache_clock = NULL;
  list_init(&list_buffer_head);
  struct buffer_head *head;

  for(int i = 0; i<64; i++)
  {
    
    if((head = (struct buffer_head *)malloc(sizeof(struct buffer_head))) == NULL)
    {
      printf("Buffer head Malloc failed\n");
      return;
    }

    /* Initialize the element of buffer_head */
    head->idx = i;
    head->is_used = false;
    head->dirty = false;
    head->accessed = false;
    lock_init(&(head->buffer_lock));
    head->on_disk_loc = UINT32_MAX;
    head->data = NULL;
    list_push_back(&list_buffer_head, &(head->elem));
  }
}

struct buffer_head *find_buffer_head(block_sector_t idx)
{
  ASSERT(&list_buffer_head);
  /* Scan the list and find the buf_head which has on_disk_loc == idx */
  struct list_elem *e;
  struct buffer_head *head;

  for(e = list_begin(&list_buffer_head); e != list_end(&list_buffer_head); e = list_next(e))
  {
    head = list_entry(e, struct buffer_head, elem);
    if((head->on_disk_loc) == idx)
      break;
  }

  if(e == list_end(&list_buffer_head))
    return NULL;

  return head;
}

void path_parsing(char *argument[], int* count, char* path)
{
  int i = 0;
  char *save_ptr;
  
  char *ptr = strtok_r(path,"/",&save_ptr);
  while(1)
  {
    if((ptr == NULL) || i >= 250)
      break;
    argument[i] = ptr;
    i++;
    ptr = strtok_r(NULL,"/",&save_ptr);
  }
  argument[i] = NULL;
  *count = i;
}

/* Check the path validity and store the parent directory into dir 
   (Path should be absolute path) */
char *check_path_validity(char *path, struct dir **dir)
{

  /* Handle corner case */
  if(strcmp(path, "") == 0)
    return NULL;
  
  if(strcmp(path,"/") == 0)
  {
    *dir = dir_open_root();
    return ".";
  }

  char *argument[250];
  int count = 0;
  bool absolute;

  char copy[strlen(path) + 1];
  strlcpy(copy,path,strlen(path)+1);
  copy[strlen(path)] = '\0';

  /* Check whether absolute path or relative path */
  if(copy[0] == '/')
    absolute = true;
  else
    absolute = false;


  path_parsing(argument, &count, copy);

  // /* Check dentry cache */
  // bool find = false;
  // bool copy_success = false;
  // struct dentry_cache *cache;
  // char copy_dentry[strlen(path) + 1];
  // strlcpy(copy_dentry,path,strlen(path)+1);
  // copy_dentry[strlen(path)] = '\0';
  // for(int i = strlen(path) - 2; i > 0; i--)
  // {
  //   if(copy_dentry[i] == '/')
  //   {
  //     copy_dentry[i] = '\0';
  //     copy_success = true;
  //     break;
  //   }
  // }

  // if(copy_success == true)
  // {
  //   if((cache = find_cache(&dentry_cache, copy_dentry)) != NULL)
  //     find = true;
  // }

  // if(find == false)
  // {
    struct dir *search;
    struct inode *inode;
    if(absolute == true)
      search = dir_open_root();
    else
      search = dir_open(inode_open(thread_current()->cd));

    /* Check the validity of path */
    /* 0~i-1 element should be already existed */
    for(int i = 0; i < count - 1; i++)
    {
      if(!dir_lookup(search, argument[i], &inode))
        return NULL;
    
      dir_close(search);
      search = dir_open(inode);
    }

    *dir = search;
  // }
  // else
  // {
  //   *dir = dir_open(inode_open(cache->sector));
  // }

  return path + (argument[count - 1] - copy);
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  /* Initialize dentry cache */
  hash_init(&dentry_cache, dentry_hash_func, dentry_less_func, NULL);

  /* Initialize the buffer cache and buffer head */
  buffer_cache = calloc(64,512);
  buffer_head_init();

  if (format) 
    do_format ();
  
  free_map_open ();

}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  /* Write all buffer cache */
  write_all_dirty(&list_buffer_head);
  struct list_elem *e;
  struct buffer_head *buf_head;

  for(e = list_begin(&list_buffer_head); e != list_end(&list_buffer_head); e)
  {
      buf_head = list_entry(e, struct buffer_head, elem);
      if(lock_held_by_current_thread(&buf_head->buffer_lock))
        lock_release(&buf_head->buffer_lock);
      e = list_next(e);
      free(buf_head);
  }
  free(buffer_cache);

  // struct dentry_cache *v;

  // struct hash_iterator i;
  // hash_first (&i, &dentry_cache);
  // while (hash_next (&i))
  // {
  //     v = hash_entry (hash_cur (&i), struct dentry_cache, elem);
  //     hash_delete(&dentry_cache, &v->elem);
  //     free(v->name);
  //     free(v);
  // }
  // hash_destroy(&dentry_cache, NULL);

  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  // if(find_cache(&dentry_cache, name) != NULL)
  //   return false;

  block_sector_t inode_sector = 0;
  struct dir *dir;
  char *path_name = check_path_validity(name, &dir);
  if(path_name == NULL)
    return false;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, path_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  // if(success)
  // {
  //   if(name[0] == '/')
  //   {
  //     /* Add dentry cache */
  //     struct dentry_cache *cache = make_cache(inode_sector, name);
  //     hash_insert(&dentry_cache, &cache->elem);
  //   }
  // }

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // struct dentry_cache *cache;
  // cache = find_cache(&dentry_cache, name);
  struct inode *inode = NULL;

  // if(cache == NULL)
  // {
    struct dir *dir;
    char *path_name = check_path_validity(name, &dir);
    if(path_name == NULL)
      return NULL;

    if (dir != NULL)
      if(!dir_lookup (dir, path_name, &inode))
      {
        dir_close(dir);
        return NULL;
      }
    dir_close (dir);
  // }
  // else
  // {
  //   inode = inode_open(cache->sector);
  // }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success;
  struct dir *dir;
  char *path_name = check_path_validity(name, &dir);
  
  // struct dentry_cache *cache;
  // if((cache = find_cache(&dentry_cache, name)) != NULL)
  // {
  //   hash_delete(&dentry_cache, &cache->elem);
  //   free(cache->name);
  //   free(cache);
  // }

  if(path_name == NULL)
    return false;

  /* Check whether regular file or directory */
  struct inode *inode;
  bool check = dir_lookup(dir, path_name, &inode);
  if(check == false)
  {
    dir_close(dir);
    return false;
  }

  if(inode->data.is_directory)
  {
    struct dir *remove = dir_open(inode);
    
    /* Should not remove ".", ".." entries */
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    { 
      dir_close(remove);
      dir_close(dir);
      return false;
    }

    /* Check there is no threads which current directory is dir */
    if(dir_is_used(remove) || remove->inode->open_cnt != 1)
    {
      dir_close(remove);
      dir_close(dir);
      return false;
    }

    /* Check that the dir is empty */
    if(!dir_isempty(remove))
    {
      dir_close(remove);
      dir_close(dir);
      return false;
    }
    else
    {
      dir_remove(dir, path_name);
      dir_close(remove);
      dir_close(dir);
      return true;
    }

  }
  else
    success = dir != NULL && dir_remove (dir, path_name);
  // struct dir *dir = dir_open_root ();
  // bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Calculate where to put the vm_entry into the hash table */
static unsigned dentry_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  struct dentry_cache *cache = hash_entry(e, struct dentry_cache, elem);
  return strlen(cache->name);
}

/* Compare address values of two entered hash_elem */
static bool dentry_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct dentry_cache *dentry_a = hash_entry(a,struct dentry_cache,elem);
  struct dentry_cache *dentry_b = hash_entry(b,struct dentry_cache,elem);

  return (strcmp(dentry_a->name, dentry_b->name) != 0) ? true : false;
}