#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "../filesys/filesys.h"
#include "../filesys/file.h"
#include "../lib/user/syscall.h"
#include "./process.h"
#include "../threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

/* Check the validity of pointers */
bool checkvalidity(void *pointer)
{
  /* Check the pointer is null pointe */
  if(pointer == NULL)
    return false;
  
  /* Check the pointer in on the user space */
  if(is_user_vaddr(pointer+4) == false)
    return false;

  /* Check the pointer is pointing mapped region */
  struct thread *cur = thread_current();
  if(pagedir_get_page(cur->pagedir,pointer) == NULL)
    return false;
  return true;
}

bool check_string_validity(char *array)
{
  int len = strlen(array);
  if(is_user_vaddr(array+len) == false)
    return false;
}

static void sys_halt(void)
{
  shutdown_power_off();
}

static void sys_exit(int status)
{
  struct thread *cur = thread_current();
  /* Save exit status at process descriptor*/

  printf("%s: exit(%d)\n",cur->name,status);

  if(cur->is_parent_wait == true)
  {
    cur->parent->child_normal_exit = true;
    cur->parent->wait_exit_status = status;
    sema_up(&(cur->sema));
  }
  thread_exit();
}

static pid_t sys_exec(const char *cmd_line)
{
  if(!checkvalidity(cmd_line) || !check_string_validity(cmd_line))
  {
    sys_exit(-1);
  }

  tid_t result;
  result = process_execute(cmd_line);
  return (pid_t)result;
}

static int sys_wait(pid_t pid)
{
  return process_wait((tid_t)pid);
}

static bool sys_create(const char *file, unsigned initial_size)
{
  if(!checkvalidity(file) || !check_string_validity(file))
  {
    sys_exit(-1);
  }

  /* If file_name = [], then exit */
  if(strlen(file) == 0)
    return false;

  return filesys_create(file,initial_size);
}

static bool sys_remove(const char *file)
{
  if(!checkvalidity(file) || !check_string_validity(file))
  {
    sys_exit(-1);
  }

  return filesys_remove(file);
}

static int sys_open(const char *file)
{
  if(!checkvalidity(file) || !check_string_validity(file))
  {
    sys_exit(-1);
  }

  struct file* open_file;
  struct thread* cur = thread_current();
  int fd;

  open_file =  filesys_open(file);
  /* File open failed */
  if(open_file == NULL)
    return -1;

  /* PDE 구현 후 해당 파일 찾아서 descriptor 반환 */
  for(fd = 3; fd<128; fd++)
  {
    if(cur->fdt[fd] == NULL)
      break;
  }
  cur->fdt[fd] = open_file;
  return fd;
}

static int sys_filesize(int fd)
{
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127)
    return -1;

  if((file = cur->fdt[fd]) == NULL)
    return -1;
  
  return file_length(file);
}

static int sys_read(int fd, void *buffer, unsigned size)
{
  if(!checkvalidity(buffer) || !check_string_validity((char *)buffer))
  {
    sys_exit(-1);
  }

  /* Check range of fd */
  if(fd < 0 || fd > 127)
    return -1;

  struct file* file;
  struct thread* cur = thread_current();
  int length;

  /* Read from stdin */
  if(fd == 0)
  {
    return input_getc();
  }

  if(fd == 1 || fd == 2)
    return -1;

  else
  {
    if((file = cur->fdt[fd]) == NULL)
      sys_exit(-1);
    length = file_read(file,buffer,size);

  }
  return length;
}

static int sys_write(int fd, const void *buffer, unsigned size)
{
  if(!checkvalidity(buffer) || !check_string_validity((char *)buffer))
  {
    sys_exit(-1);
  }

  /* Check range of fd */
  if(fd < 0 || fd > 127)
    return -1;

  /* Check the buffer's element is not on the kernel space */

  struct file* file;
  struct thread* cur = thread_current();
  int length;

  /* Write buffer to stdout */
  if(fd == 1)
  {
    putbuf(buffer,size);
    return size;
  }

  if(fd == 0 || fd == 2)
    return -1;

  else
  {
    file = cur->fdt[fd];
    if(file == NULL)
      sys_exit(-1);
    length = file_write(file,buffer,size);
  }
  return length;
}

static void sys_seek(int fd, unsigned position)
{
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127)
    return;

  if((file = cur->fdt[fd]) != NULL)
    file_seek(file,position);
}

static unsigned sys_tell(int fd)
{
  struct file* file;
  struct thread* cur = thread_current();

  /* Check range of fd */
  if(fd < 0 || fd > 127)
    return -1;

  if((file = cur->fdt[fd]) == NULL)
    return -1;
  
  return file_tell(file);
}

static void sys_close(int fd)
{
  struct file* file;
  struct thread* cur = thread_current();

  /* Only operate when fd != 0,1,2 */
  if(fd == 0 || fd == 1 || fd ==2)
    return;

  /* Check the value of fd */
  if(fd < 0 || fd > 127)
    return;

  file = cur->fdt[fd];
  if(file != NULL)
  {
    /* Only close the file if fd is valid */
    file_close(file);
    file = cur->fdt[fd] = NULL;
  }
}

static void sys_sigaction(int signum, void (*handler) (void))
{

}

static void sys_sendsig(pid_t pid, int signum)
{

}

static void sched_yield()
{

}

/* Check the stack is enougth to store in user address, and
   get argument using esp and store */
void get_argument(void *esp, int* argument, int count)
{
  /* Check */
  if(esp - 4*count >= PHYS_BASE)
    sys_exit(-1);

  int i;
  for(i=0; i<count; i++)
  {
    argument[i] = *(uint32_t*)esp;
    esp = esp + 4;
  }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* Check the validity of esp, + 4 for the when esp set to PHYS_BASE - 4 */
  void * esp = f->esp;
  if(checkvalidity(esp+4) == false)
    sys_exit(-1);

  /* Get the number of arguments */
  int argument_number;
  int argument[64];
  argument_number = *(uint32_t*)esp;
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
      f->eax = sys_exec(argument[0]);
      break;

    case SYS_WAIT:
      get_argument(esp,argument,1);
      f->eax = sys_wait(argument[0]);
      break;

    case SYS_CREATE:
      get_argument(esp,argument,2);
      f->eax = sys_create(argument[0], argument[1]);
      break;

    case SYS_REMOVE:
      get_argument(esp,argument,1);
      f->eax = sys_remove(argument[0]);
      break;

    case SYS_OPEN:
      get_argument(esp,argument,1);
      f->eax = sys_open(argument[0]);
      break;

    case SYS_FILESIZE:
      get_argument(esp,argument,1);
      f->eax = sys_filesize(argument[0]);
      break;

    case SYS_READ:
      get_argument(esp,argument,3);
      f->eax = sys_read(argument[0], argument[1], argument[2]);
      break;

    case SYS_WRITE:
      get_argument(esp,argument,3);
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
      break;

    case SYS_SENDSIG:
      get_argument(esp,argument,2);
      break;

    case SYS_YIELD:
      get_argument(esp,argument,0);
      break;

    default:
      sys_exit(-1);
    // /* Project 3 */
    // case SYS_MMAP:
    //   get_argument(&f->esp,argument,1);
    // case SYS_MUNMAP:
    //   get_argument(&f->esp,argument,1);

    // /* Project 4 */
    // case SYS_CHDIR:
    // case SYS_MKDIR:
    // case SYS_READDIR:
    // case SYS_ISDIR:
    // case SYS_INUMBER:
  }
}
