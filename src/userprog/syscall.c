#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "../filesys/filesys.h"
#include "../filesys/file.h"
#include "./process.h"
#include "../threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "../threads/palloc.h"
#include "../vm/page.h"

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

/* Check the validity of pointers */
bool check_esp_validity(void *esp)
{
  if(&esp == NULL)
    return false;

  /* Check the pointer in on the user space */
  if(is_user_vaddr(esp+4) == false)
    return false;
  if(esp < (void *) 0x08048000)
    return false;
  
  // Project 2
  // /* Check esp points the mapped region to get int(check 4 bytes)*/
  // for(int i = 0; i<4; i++)
  // {
  //   if(pagedir_get_page(thread_current()->pagedir,esp+i) == NULL)
  //     return false;
  // }
  return true;
}

/* check arg(pointer) validity */
bool check_arg_validity(void *arg, int n UNUSED)
{ 
  /* Check pointer is not null and point mapped region */
  /* Check the pointer is null pointe */
  if(arg == NULL)
    return false;
  
  if(is_user_vaddr(arg) == false)
    return false; 

  if(is_user_vaddr(arg+4) == false)
    return false;
  
  if(arg < (void *) 0x08048000)
    return false;

  /* Check the virtual address of arg */
  struct thread* cur = thread_current();
  if(!find_vme(arg,&cur->vm))
    return false;
  return true;

  // Project 2
  /* Check the n element of arg is mapped to phys_mem */
  // struct thread *cur = thread_current();
  // void * page;
  // char * array = (char *)arg;

  // int i;
  // for(i = 0; i<n; i++)
  // {
  //   if((page = pagedir_get_page(cur->pagedir,arg+i)) == NULL)
  //     return false;
    
  //   if(array[i] == '\0')
  //     break;
  // }
}

bool check_buffer_validity(void *buffer, unsigned size, bool write, void* esp)
{
  struct thread* cur = thread_current();
  int pg_no = (pg_ofs(buffer)+size-1)/PGSIZE;
  for(int i=0; i<pg_no+1; i++)
  {
    /* Check whether the addr is in range of (stack_bottom, stack_top+32) */
      if(verify_stack(esp,buffer+i*PGSIZE))
      {
         if(!expand_stack(buffer+i*PGSIZE))
         {
            printf("Stack expansion failed\n");
            sys_exit(-1);
         }
      }

    struct vm_entry* v = find_vme(buffer+i*PGSIZE, &cur->vm);
    if(v == NULL)
      return false;
    if(write && (v->permission == 0))
      return false;
    if(find_frame(v) == NULL)
      handle_mm_fault(v);
  }
  return true;
}

void sys_halt(void)
{
  shutdown_power_off();
}

void sys_exit(int status)
{
  if(lock_held_by_current_thread(&filesys_lock))
    lock_release(&filesys_lock);
  if(lock_held_by_current_thread(&frame_lock))
    lock_release(&frame_lock);
  if(lock_held_by_current_thread(&swap_lock))
    lock_release(&swap_lock);
  
  struct thread *cur = thread_current();

  /* First all child process stop until parent call wait */
  sema_down(&cur->wait_parent);
  
  /* Close all mmaped vm_entry */
  sys_munmap(CLOSE_ALL);

  printf("%s: exit(%d)\n",cur->name,status);
  if(cur->is_parent_wait == true)
  {
    cur->parent->child_normal_exit = true;
    cur->parent->wait_exit_status = status;
    /* Pop the cur thread from parent */
    sema_up(&(cur->sema));
    list_remove(&cur->s_elem);
  }

  /* Close running file */
  file_close(cur->file_run);
  thread_exit();
}

pid_t sys_exec(const char *cmd_line)
{
  lock_acquire(&filesys_lock);
  tid_t result;
  result = process_execute(cmd_line); 
  lock_release(&filesys_lock);
  return (pid_t)result;
}

int sys_wait(pid_t pid)
{
  return process_wait((tid_t)pid);
}

bool sys_create(const char *file, unsigned initial_size)
{

  bool create;

  lock_acquire(&filesys_lock);
  /* If file_name = [], then exit */
  if(strlen(file) == 0)
  {
    lock_release(&filesys_lock);
    return false;
  }
  create = filesys_create(file,initial_size);
  lock_release(&filesys_lock);
  return create;
}

bool sys_remove(const char *file)
{
  bool remove;
  lock_acquire(&filesys_lock);
  remove = filesys_remove(file);
  lock_release(&filesys_lock);
  return remove;
}

int sys_open(const char *file)
{
  lock_acquire(&filesys_lock);

  struct file* open_file;
  struct thread* cur = thread_current();
  int fd;

  open_file =  filesys_open(file);
  /* File open failed */
  if(open_file == NULL)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  /* PDE 구현 후 해당 파일 찾아서 descriptor 반환 */
  for(fd = 3; fd<128; fd++)
  {
    if(cur->fdt[fd] == NULL)
      break;
  }
  cur->fdt[fd] = open_file;

  /* Deny if executable file */
  // if(!strcmp(thread_current()->name, file))
  //   file_deny_write(file);

  lock_release(&filesys_lock);
  return fd;
}

int sys_filesize(int fd)
{
  lock_acquire(&filesys_lock);
  int length;
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127 || (file = cur->fdt[fd]) == NULL)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  length = file_length(file);
  lock_release(&filesys_lock);
  return length;
}

int sys_read(int fd, void *buffer, unsigned size)
{
  lock_acquire(&filesys_lock);
  /* Check range of fd */
  if(fd < 0 || fd > 127)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  struct file* file;
  struct thread* cur = thread_current();
  int length;

  /* Read from stdin */
  if(fd == 0)
  {
    lock_release(&filesys_lock);
    return input_getc();
  }

  if(fd == 1 || fd == 2)
  {
    lock_release(&filesys_lock);;
    return -1;
  }

  else
  {
    if((file = cur->fdt[fd]) == NULL)
    {
      lock_release(&filesys_lock);
      return -1;
    }
    length = file_read(file,buffer,size);

  }
  lock_release(&filesys_lock);
  return length;
}

int sys_write(int fd, const void *buffer, unsigned size)
{
  lock_acquire(&filesys_lock);

  /* Check range of fd */
  if(fd < 0 || fd > 127)
  {
    lock_release(&filesys_lock);
    return -1;
  }
  char buf = ((char*)buffer)[0];
  /* Check the buffer's element is not on the kernel space */

  struct file* file;
  struct thread* cur = thread_current();
  int length;

  /* Write buffer to stdout */
  if(fd == 1)
  {
    putbuf(buffer,size);
    lock_release(&filesys_lock);
    return size;
  }

  if(fd == 0 || fd == 2)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  else
  {
    file = cur->fdt[fd];
    if(file == NULL)
    {
      lock_release(&filesys_lock);
      return -1;
    }
    length = file_write(file,buffer,size);
  }
  lock_release(&filesys_lock);
  return length;
}

void sys_seek(int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127)
  {
    lock_release(&filesys_lock);
    return;
  }

  if((file = cur->fdt[fd]) != NULL)
    file_seek(file,position);
  lock_release(&filesys_lock);
}

unsigned sys_tell(int fd)
{
  lock_acquire(&filesys_lock);

  unsigned tell;
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127 || (file = cur->fdt[fd]) == NULL)
  {
    lock_release(&filesys_lock);
    return -1;
  }

  tell = file_tell(file);
  lock_release(&filesys_lock);
  return tell;
}

void sys_close(int fd)
{
  lock_acquire(&filesys_lock);
  struct file* file;
  struct thread* cur = thread_current();

  /* check fd */
  if(fd == 0 || fd == 1 || fd ==2 || fd < 0 || fd > 127)
  {
    lock_release(&filesys_lock);
    return;
  }

  file = cur->fdt[fd];
  if(file != NULL)
  {
    /* Only close the file if fd is valid */
    file_close(file);
    cur->fdt[fd] = NULL;
  }
  lock_release(&filesys_lock);
}

void sys_sigaction(int signum, void (*handler) (void))
{
  /* Check the range of signum */
  if(signum < 1 || signum > 3)
    return;

  struct thread *cur = thread_current();

  if(signum == 1)
    cur->eip1 = handler;

  else if(signum == 2)
    cur->eip2 = handler;

  else /*signum == 3 */
    cur->eip3 = handler;
}

void sys_sendsig(pid_t pid, int signum)
{
  /* Check the range of signum */
  if(signum < 1 || signum > 3)
    return;
  
  /* Check valid pid */
  struct thread* t = find_thread((tid_t)pid);
  
  if(t == NULL)
    return;
  
  for(int i = 0; i<10; i++)
  {
    if(t->sig[i] == 0)
    {
      t->sig[i] = signum;
      break;
    }
  }
}

mapid_t sys_mmap (int fd, void *addr)
{
  /* Check the validity of parameter */
  if(fd == 0 || fd == 1)
    return -1;
  if(addr == NULL)
    return -1;
  if(((int)addr)%PGSIZE != 0)
    return -1;

  struct thread *cur = thread_current();
  if(cur->fdt[fd] == NULL)
    return -1;

  struct file *file = file_reopen(cur->fdt[fd]);
  if(file == NULL)
    return -1;
  
  off_t file_size = file_length(file);
  size_t page_read_bytes = file_size;
  int page_num = file_size/PGSIZE;
  /* Check size of file */
  if(file_size == 0)
    return -1;
  /* Check the address is already in use */
  for(int i = 0; i<page_num+1; i++)
  {
    if(find_vme(addr+i*(PGSIZE),&cur->vm) != NULL)
      return -1;
  }

  struct mmap_file *mmap;
  mmap = init_mmap_file(pg_no(addr),file);
  for(int i = 0; i<page_num+1; i++)
  {
    struct vm_entry* vm_entry = init_vm(addr+i*(PGSIZE),file,0,1,FILE,1);
    hash_insert(&cur->vm,&vm_entry->elem);
    if(page_read_bytes > PGSIZE)
    {
      vm_entry->read_bytes = PGSIZE;
      vm_entry->zero_bytes = 0;
      page_read_bytes -= PGSIZE;
    }
    else
    {
      vm_entry->read_bytes = page_read_bytes;
      vm_entry->zero_bytes = PGSIZE-page_read_bytes;
    }
    list_push_back(&mmap->vm_entry_list,&vm_entry->mmap_elem);
    /* Allocate */
    handle_mm_fault(vm_entry);
  }
  return (mapid_t)mmap->id;
}

void sys_munmap (mapid_t mapid)
{
  /* Reflect the change */
  struct mmap_file *mmap;
  struct thread *cur = thread_current();
  struct list_elem *e = list_begin(&cur->mmap_list);
  while(e!=list_end(&cur->mmap_list))
  {
    mmap = list_entry(e,struct mmap_file,elem);
    if((mmap->id == mapid) || (mapid == CLOSE_ALL))
    {
      if(e != list_end(&cur->mmap_list))
      {
        struct list_elem *v;
        v = list_begin(&mmap->vm_entry_list);
        for(v; v!=list_end(&mmap->vm_entry_list); v=list_next(v))
        {
          struct vm_entry *vm_entry = list_entry(v,struct vm_entry,mmap_elem);
          lock_acquire(&frame_lock);
          swap_out(find_frame(vm_entry));
          lock_release(&frame_lock);
        }
        sys_close(mmap->file);
        e = list_next(e);
        release_mmap_file(mmap);
      }
      if(mapid != CLOSE_ALL)
        break;
    }
  }
}

/* Check the stack is enougth to store in user address, and
   get argument using esp and store */
void get_argument(void *esp, int* argument, int count)
{
  struct thread *cur = thread_current();

  /* Check */
  int i;
  for(i=0; i<count; i++)
  {
    if(!check_esp_validity(esp))
      sys_exit(-1);
    argument[i] = *(int*)esp;
    esp = esp + 4;
  }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* Check the validity of esp, + 4 for the when esp set to PHYS_BASE - 4 */
  void *esp = f->esp;
  if(!check_esp_validity(esp))
    sys_exit(-1);
  /* Get the number of arguments */
  int argument_number;
  int argument[64];
  argument_number = *(int*)esp;
  esp = esp + 4;
  
  /* Divide the case */
  switch(argument_number)
  {
    /* Project 2 */
    case SYS_HALT:
      get_argument(esp,argument,0);
      sys_halt();
      break;

    case SYS_EXIT:
      get_argument(esp,argument,1);
      sys_exit(argument[0]);
      break;

    case SYS_EXEC:
      get_argument(esp,argument,1);
      if(!check_buffer_validity((void *)argument[0],0,false,f->esp))
        sys_exit(-1);
      f->eax = sys_exec(argument[0]);
      break;

    case SYS_WAIT:
      get_argument(esp,argument,1);
      f->eax = sys_wait(argument[0]);
      break;

    case SYS_CREATE:
      get_argument(esp,argument,2);
      if(!check_buffer_validity((void *)argument[0],0,false,f->esp))
        sys_exit(-1);
      f->eax = sys_create(argument[0], argument[1]);
      break;

    case SYS_REMOVE:
      get_argument(esp,argument,1);
      if(!check_buffer_validity((void *)argument[0],0,false,f->esp))
        sys_exit(-1);
      f->eax = sys_remove(argument[0]);
      break;

    case SYS_OPEN:
      get_argument(esp,argument,1);
      if(!check_buffer_validity((void *)argument[0],0,false,f->esp))
        sys_exit(-1);
      f->eax = sys_open(argument[0]);
      break;

    case SYS_FILESIZE:
      get_argument(esp,argument,1);
      f->eax = sys_filesize(argument[0]);
      break;

    case SYS_READ:
      get_argument(esp,argument,3);
      if(!check_buffer_validity((void *)argument[1],argument[2],true,f->esp))
        sys_exit(-1);
      f->eax = sys_read(argument[0], argument[1], argument[2]);
      break;

    case SYS_WRITE:
      get_argument(esp,argument,3);
      if(!check_buffer_validity((void *)argument[1],argument[2],false,f->esp))
        sys_exit(-1);
      f->eax = sys_write(argument[0],argument[1],argument[2]);
      break;

    case SYS_SEEK:
      get_argument(esp,argument,2);
      sys_seek(argument[0], argument[1]);
      break;

    case SYS_TELL:
      get_argument(esp,argument,1);
      f->eax = sys_tell(argument[0]);
      break;

    case SYS_CLOSE:
      get_argument(esp,argument,1);
      sys_close(argument[0]);
      break;

    case SYS_SIGACTION:
      get_argument(esp,argument,2);
      sys_sigaction(argument[0],argument[1]);
      break;

    case SYS_SENDSIG:
      get_argument(esp,argument,2);
      sys_sendsig(argument[0],argument[1]);
      break;

    case SYS_YIELD:
      get_argument(esp,argument,0);
      thread_yield();
      break;
    
    case SYS_MMAP:
      get_argument(esp,argument,2);
      f->eax = sys_mmap(argument[0],argument[1]);
      break;

    case SYS_MUNMAP:
      get_argument(esp,argument,1);
      sys_munmap(argument[0]);
      break;

    default:
      sys_exit(-1);
  }
  
  if(argument_number == SYS_SENDSIG || argument_number == SYS_SIGACTION)
    return;
  /* 나중에 race condition도 생각하기 */
  /* 핸들러는 만들었는데 나중에 생성되는 경우 시그널이 무시될 수 있음 */
  /* Check if there is any signal comes to process */
  struct thread* cur = thread_current();
  for(int i = 0; i<10; i++)
  {
    int *sig = &(cur->sig[i]);
    /* Signal exists */
    if(*sig != 0)
    {
      /* Signum 1 */
      if(*sig == 1)
      {
        if(cur->eip1 != NULL)
        {
          printf("Signum: 1, Action: %p\n",cur->eip1);
          cur->eip1 = NULL;
          *sig = 0;

        }
      }
      /* Signum 2*/
      else if(*sig == 2)
      {
        if(cur->eip2 != NULL)
        {
          printf("Signum: 2, Action: %p\n",cur->eip2);
          cur->eip2 = NULL;
          *sig = 0;
        }
      }
      /* Signum 3*/
      else
      {
        if(cur->eip3 != NULL)
        {
          printf("Signum: 3, Action: %p\n",cur->eip3);
          cur->eip3 = NULL;
          *sig = 0;
        }
      }
    }
    else
      break;
  }
}
