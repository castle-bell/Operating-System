#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "../vm/page.h"
#include "../vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);


void argument_parsing(char *argument[], int* count, char* file_name)
{
  int i = 0;
  char *save_ptr;
  char *ptr = strtok_r(file_name," ",&save_ptr);
  while(1)
  {
    if((ptr == NULL) || i >= 64)
      break;
    argument[i] = ptr;
    i++;
    ptr = strtok_r(NULL," ",&save_ptr);
  }
  argument[i] = NULL;
  *count = i;
}

void argument_stack(char **argument, int count, void **esp)
{
  ASSERT(argument[count] == NULL);
  int i;
  int j;
  int len = 0;
  int padding = 0;

  /* First push the all argument including \0 */
  for(i=count-1; i>=0; i--)
  {
    len = strlen(argument[i]);
    for(j=len; j>=0; j--)
    {
      *esp = *esp - 1; /* To store char */
      **(char**)esp = argument[i][j];
      padding += 1;
    }
    argument[i] = *esp;
  } 
  /* Add padding */
  if((padding % 4) == 0)
    padding = 0;
  else
    padding = 4 - (padding % 4);
  
  for(i=0; i<padding; i++)
  {
    *esp = *esp - 1; /* To store char */
    **((uint8_t**)esp) = (uint8_t)0;
  }

  /* Push argument's address, pintos is 80x86 architecture,
    size of pointer is 4bytes. */
  
  for(i=count; i>=0; i--)
  {
    *esp = *esp - 4; /* To store char* */
    *(char**)*esp = argument[i];
  }
  argument = *esp;

  /* Push argument pointer */
  *esp = *esp - 4;
  **(uint32_t **)esp = (uint32_t)argument;

  /* Push # of argument */
  *esp = *esp - 4;
  **((int**)esp) = count;

  /* Push false address */
  *esp = *esp - 4;
  **((int **)esp) = (uint32_t)(void *)0;
}

struct thread *find_child(tid_t tid, struct list *sibling)
{
  struct list_elem *e;
  struct thread *child;

  for(e=list_begin(sibling); e->next != NULL; e=list_next(e))
  {
    child = list_entry(e,struct thread,s_elem);
    if(child->tid == tid)
      return child;
  }
  return NULL;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  char copy[256]; // why 4KB?
  char *token;
  char *save_ptr;
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (copy, file_name, PGSIZE);

  /* Parsing the file_name */
  token = strtok_r(copy," ",&save_ptr);
  if(token == NULL)
    token = copy;

  // /* ADDD */
  // if(filesys_open(token) == NULL){
  //   return -1;
  // }

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (token, PRI_DEFAULT, start_process, fn_copy);

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 

  /* Check load finish */
  /* If success create child, then push into the list */
  struct thread* child;
  struct thread* cur = thread_current();

  // struct thread* child;
  // struct thread* cur = running_thread();
  // child = find_thread(tid);
  // child->parent = cur;
  // list_push_back(&(cur->sibling), &(child->s_elem));
  child = find_child(tid,&cur->sibling);

  if(child == NULL)
    return -1;

  /* wait child until load success or fail */
  if(cur->child_success_load == 0)
  {
    sema_down(&(child->exec_sema));
  }

  /* Load fail */
  if(cur->child_success_load == -1)
  {
    tid = TID_ERROR;
  }
  
  cur->child_success_load = 0;

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Store running file */
  struct thread* cur = thread_current();

  /* Init the hash table */
  if(!hash_init(&cur->vm,vm_hash_func,vm_less_func,NULL))
  {
    printf("Hash table alloc failed\n");
    sys_exit(-1);
  }
  cur->is_loaded = true;

  /* Parsing the file_name */
  char *argument[64];
  int count;
  argument_parsing(&argument,&count,file_name);
  file_name = argument[0];
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  // file_close(cur->file_run);
  
  /* Push the arguments to user stack */
  if(success)
  {
    cur->parent->child_success_load = 1;
    argument_stack(argument,count,&if_.esp);
    sema_up(&(cur->exec_sema));
  }
  /* Reoperate the parent process */

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
  {
    cur->parent->child_success_load = -1;
    sema_up(&(cur->exec_sema));
    /* may be need sema */
    sys_exit(-1);
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  /* Search the descriptor of the child process */
  struct thread *cur = thread_current();
  struct thread *child = NULL;

  child = find_child(child_tid, &cur->sibling);

  if(child == NULL)
    return -1;

  enum intr_level old_level;
  old_level = intr_disable();
  /* Every child is stopped, so first sema_up */
  sema_up(&child->wait_parent);


  /* Already wait for this child */
  if((child->is_parent_wait) == true)
    return -1;
  /* Wait until the child process terminates using sema_down */
  /* Set the child process' bool is parent wait true */
  child->is_parent_wait = true;
  sema_down(&(child->sema));
  
  intr_set_level(old_level);

  // palloc_free_page(child);

  /* If killed, return -1 */
  if(!cur->child_normal_exit)
  {
    cur->child_normal_exit = false;
    return -1;
  }

  return cur->wait_exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Close all file in current process */
  int fd;
  for(fd = 3; fd<128; fd++)
  {
    struct file* file;
    if((file = cur->fdt[fd]) != NULL)
      sys_close(fd);
  }

  /* Sema up all child process and finish it */
  struct list *ls = &(cur->sibling);
  struct list_elem *t;
  t = list_begin(ls);
  for(t; t != list_end(ls); t = list_next(t))
  {
    struct thread *child = list_entry(t,struct thread,s_elem);
    /* Sema up semaphore of child */
    sema_up(&(child->wait_parent));
  }

  /* Delete vm_entry and hash_table */
  if(cur->is_loaded == true)
    remove_vm_entry();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */

  /* Deny file write */
  t->file_run = file;
  if(file != NULL)
    file_deny_write(file);
  return success;
}

/* load() helpers. */

bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Initialize and set the vm_entry */
      struct vm_entry* vm_entry = init_vm(upage, file,ofs,writable,EXEC,1);
      if(vm_entry == NULL)
        return false;

      /* Add vm_entry to hash table by hash_insert() */
      struct thread* cur = thread_current();
      if(hash_insert(&cur->vm,&vm_entry->elem) != NULL)
      {
        // printf("Already exist vm_entry\n");
        return false;
      }
      vm_entry->read_bytes = page_read_bytes;
      vm_entry->zero_bytes = page_zero_bytes;

      // printf("page: %p, permission: %d, p_type: %d, read: %d, zero: %d, flag: %d\n",
      //     vm_entry->page, vm_entry->permission, vm_entry->p_type, vm_entry->read_bytes, vm_entry->zero_bytes, vm_entry->flag);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  struct page* p = init_page(PAL_USER | PAL_ZERO);
  while(p  == NULL)
   {
      swap();
      p = init_page(PAL_USER | PAL_ZERO);
   }
  if (p != NULL) 
    {
      kpage = p->kpage;
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
      {
        *esp = PHYS_BASE;
        /* Add vm_entry of stack and add it to hash table */
        struct vm_entry* v = init_vm(((uint8_t *) PHYS_BASE) - PGSIZE,NULL,0,1,ANONYMOUS,1);
        if(v == NULL)
        {
          // printf("Alloc vm_entry failed\n");
          return false;
        }
        struct thread* cur = thread_current();
        hash_insert(&cur->vm,&v->elem);
        set_page(p,v,cur);
      }

      else
      {
        lock_acquire(&frame_lock);
        release_page(p);
        lock_release(&frame_lock);
      }
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Calculate where to put the vm_entry into the hash table */
static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  struct vm_entry *v = hash_entry(e,struct vm_entry,elem);
  return v->vpn;
}

/* Compare address values of two entered hash_elem */
static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct vm_entry *v_a = hash_entry(a,struct vm_entry,elem);
  struct vm_entry *v_b = hash_entry(b,struct vm_entry,elem);
  return ((v_a->vpn)<(v_b->vpn)) ? true : false;
}
