#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "../devices/timer.h"
#include "threads/malloc.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Advanced Scheduler(mlfqs) */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

int load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of ready queues for multi ready queues on MLFQS option */
static struct multi_ready multi_ready;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are slept. */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
bool thread_report_latency;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Compares the value of wakeup ticks of threads which contains two list 
   elements A and B each, given auxiliary data AUX.  Returns true if A is
   less than B, or false if A is greater than or equal to B. */

static bool
less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);
  struct thread *a_thread = list_entry(a,struct thread,elem);
  struct thread *b_thread = list_entry(b,struct thread,elem);
  return (a_thread->wakeup_ticks < b_thread->wakeup_ticks) ? true : false;
}

/* Compares the value of priority of threads which contains two list 
   elements A and B each, given auxiliary data AUX.  Returns true if A is
   less than B, or false if A is greater than or equal to B. */

static bool
cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);
  struct thread *a_thread = list_entry(a,struct thread,elem);
  struct thread *b_thread = list_entry(b,struct thread,elem);
  return (a_thread->priority > b_thread->priority) ? true : false;
}

/* Preemption if the highest priority of ready list is bigger than
   the priority of running thread */
void preemption(void)
{
  if(!thread_mlfqs)
  {
    if(!list_empty(&ready_list))
    {
      list_sort(&ready_list, cmp_priority, NULL);
      struct thread *cmp = list_entry(list_begin(&ready_list),struct thread, elem);
      if(thread_current()->priority < cmp->priority)
        thread_yield();
    }
  }
  else
  {
    struct list* ready;
    if((ready = find_ready_max(&multi_ready)) != NULL)
    {
      struct thread *cmp = list_entry(list_begin(ready),struct thread, elem);
      if(thread_current()->priority < cmp->priority)
        thread_yield();
    }
  }
}

/* Print the thread elements */
void print_thread(struct thread *t, struct lock *lock)
{
  printf("@@@@@@@@@@@@@@@@\n");
  printf("Thread name is [%s]\n", t->name);
  printf("Thread priority is [%d]\n", t->priority);
  printf("Thread rep_priority is [%d]\n", t->rep_priority);
  if(lock->holder == NULL){
    printf("There is no lock holder\n");
  }
  printf("%s thread holds the lock\n",lock->holder->name);
  printf("@@@@@@@@@@@@@@@@\n");
}

/* Wake up the threads whose wakeup_ticks are less or equal to
   timer ticks */
void thread_wakeup(int64_t w_ticks)
{
  ASSERT(w_ticks>=0);

  enum intr_level old_level;
  old_level = intr_disable();

  struct list_elem *start = list_begin(&sleep_list);
  while(start != list_end(&sleep_list))
  {
    struct thread *t = list_entry(start,struct thread,elem);
    if(t->wakeup_ticks <= w_ticks)
    {
      list_pop_front(&sleep_list);
      thread_unblock(t);
      start = list_begin(&sleep_list);
      if(start != list_end(&sleep_list))
        min_ticks = list_entry(start,struct thread,elem)->wakeup_ticks;
      else
        min_ticks = INT64_MAX;
    }
    else
      break;
  }

  intr_set_level(old_level);

}

/* Fixed_point number arithmetic function */
int int_to_fp(int n)
{
  return n*F;
}
int fp_to_int(int x)
{
  return x/F;
}
int fp_to_int_round(int x)
{
  if(x>=0) return (x+F/2)/F;
  return (x-F/2)/F;
}
int add_fp(int x, int y)
{
  return x+y;
}
int add_mixed(int x, int n)
{
  return x+n*F;
}
int sub_fp(int x, int y)
{
  return x-y;
}
int sub_mixed(int x, int n)
{
  return x-n*F;
}
int mult_fp(int x, int y)
{
  return ((int64_t) x) *y/F;
}
int mult_mixed(int x, int n)
{
  return x*n;
}
int div_fp(int x, int y)
{
  return ((int64_t) x) * F/y;
}
int div_mixed(int x, int n)
{
  return x/n;
}

/* initialize multi_ready_queue */
void init_multi(struct multi_ready *multi)
{
  ASSERT(multi != NULL);
  list_init(&multi->head);
  list_init(&multi->tail);
  for(int i = PRI_MIN; i < PRI_MAX+1; i++)
  {
    struct list ready_list;
    list_init(&ready_list);
    multi_push_front(multi,&ready_list);
  }
}


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  
  lock_init (&tid_lock);
  if(thread_mlfqs)
    init_multi(&multi_ready);
  else
    list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->nice = NICE_DEFAULT;
  initial_thread->recent_cpu = RECENT_CPU_DEFAULT;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Change the state of the caller thread to  "blocked" and put
   it to the sleep queue. */
void thread_sleep(int64_t ticks)
{
  struct thread *t = thread_current ();

  enum intr_level old_level;
  old_level = intr_disable();

  ASSERT(t != idle_thread);
  t->wakeup_ticks = ticks;
  list_insert_ordered(&sleep_list,&(t->elem),&less,NULL);
  min_ticks = list_entry(list_begin(&sleep_list),struct thread,elem)->wakeup_ticks;
  thread_block();

  intr_set_level(old_level);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /* compare the priorities of the currently running thread and
     the newly inserted one. Yield the CPU if the newly arriving
     thread has higher priority */
  preemption();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if(!thread_mlfqs)
    list_insert_ordered(&ready_list, &t->elem, &cmp_priority, NULL);
  else
    list_push_front(find_ready_prior(&multi_ready,t->priority), &t->elem);
  t->status = THREAD_READY;
  /* For latency measurement */
  if(thread_report_latency)
  {
    int64_t time = timer_ticks();
    if(time < t->latency)
    {
      t->latency = time;
    }
  }
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  /* For latency measurement */
  if(thread_report_latency)
  {
    struct thread* cur = thread_current();
    printf("Thread <%s> completed in <%lld> ticks\n",cur->name,timer_ticks()-cur->latency);
  }
  if(thread_mlfqs)
  {
    struct list *begin = (&multi_ready)->head.next;
    while(begin != &((&multi_ready)->tail))
    {
      free(begin);
    }
  }
  thread_current ()->status = THREAD_DYING;

  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  {
    if(!thread_mlfqs)
      list_insert_ordered(&ready_list, &cur->elem, &cmp_priority, NULL);
    else
      list_push_front(find_ready_prior(&multi_ready,cur->priority), &cur->elem);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. and if current
   thread priority is less than max_priority in ready_list, then yield() */
void
thread_set_priority (int new_priority) 
{
  /* Advanced Scheduler */
  if(thread_mlfqs == true)
  {
    /* Do nothing in this funciton */
    return;
  }
  struct thread *cur = thread_current ();
  cur->priority = new_priority;
  cur->rep_priority = new_priority;

  /* Priority set highest priority in lock list */
  if(highest_priority_locklist() > cur->priority)
    cur->priority = highest_priority_locklist();

  preemption();
  
  list_sort(&ready_list,&cmp_priority,NULL);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Using value of recent_cpu and nice, calculate the priority */
void mlfqs_priority(struct thread *t)
{
  ASSERT(thread_mlfqs);
  /* Check the thread is idle thread */
  int priority;
  int cpu_comp = div_mixed(t->recent_cpu,4);
  int nice_comp = (t->nice)*2;

  if(t != idle_thread)
  { 
    priority = int_to_fp(PRI_MAX)-cpu_comp-int_to_fp(nice_comp);
    priority = fp_to_int_round(priority);
    if(priority > PRI_MAX)
      priority = PRI_MAX;
    if(priority < PRI_MIN)
      priority = PRI_MIN;
    t->priority = priority;
  }
}

void mlfqs_prior_reset(struct multi_ready* multi_list)
{
  struct list* ready;
  for(int i = 0; i < 64; i++)
  {
    ready = find_ready_prior(multi_list,i);
    struct list_elem* ready_order = list_begin(ready);
    while(ready_order != list_end(ready))
    {
      struct thread* t = list_entry(ready_order,struct thread, elem);
      if(t->priority != i)
      {
        struct list_elem* backup = ready_order;
        ready_order = list_remove(ready_order);
        list_push_back(find_ready_prior(multi_list,t->priority),backup);
      }
      else
        ready_order = list_next(ready_order);
    }
  }
}

void mlfqs_recent_cpu (struct thread *t)
{
  ASSERT(thread_mlfqs);
  if(t != idle_thread)
  {
    int decay = div_fp(mult_mixed(load_avg,2), add_mixed(mult_mixed(load_avg,2),1));
    t->recent_cpu = add_mixed(mult_fp(decay,t->recent_cpu),t->nice);
  }
}

void mlfqs_load_avg (void)
{
  ASSERT(thread_mlfqs);
  int num_ready = 0;
  for(int i = 0; i<64; i++)
  {
    num_ready += list_size(find_ready_prior(&multi_ready,i));
  }
  if(thread_current() != idle_thread)
    num_ready = num_ready + 1;
  int coef1 = div_fp(int_to_fp(59),int_to_fp(60));
  int coef2 = div_fp(int_to_fp(1),int_to_fp(60));
  load_avg = add_fp(mult_fp(load_avg,coef1),mult_mixed(coef2,num_ready));
  if(load_avg < 0)
    load_avg = 0;
}

void mlfqs_increment (void)
{
  ASSERT(thread_mlfqs);
  if(thread_current() != idle_thread)
  {
    thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu,1);
  }
}

/* Recalculate the recent_cpu(if set == 1), 
   priority(if set == 0) of all threads */
void mlfqs_recalc (int set)
{
  ASSERT(thread_mlfqs);
  struct list_elem *start;
  struct list_elem *begin = list_begin(&all_list);
  for(start = begin; start != list_end(&all_list); start = list_next(start))
  {
    struct thread *t = list_entry(start,struct thread,allelem);
    if(set == 1)
      mlfqs_recent_cpu(t);
    if(set == 0)
      mlfqs_priority(t);
  }
  /* reordering the multi ready queue */
  mlfqs_prior_reset(&multi_ready);
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  /* Not yet implemented. */
  enum intr_level old_level;
  old_level = intr_disable ();
  thread_current()->nice = nice;
  mlfqs_priority(thread_current());
  /* Preemption can be occured, because the priority of current thread
  is changed */
  preemption();
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  enum intr_level old_level;
  old_level = intr_disable ();
  int nice = thread_current()->nice;
  intr_set_level(old_level);
  return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  int load_avg_100 = mult_mixed(load_avg,100);
  load_avg_100 = fp_to_int_round(load_avg_100);
  intr_set_level(old_level);
  return load_avg_100;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  int cur_recent_cpu = mult_mixed(thread_current()->recent_cpu,100);
  cur_recent_cpu = fp_to_int_round(cur_recent_cpu);
  intr_set_level(old_level);
  return cur_recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  /* initialize the variables that use in priority donation */
  t->rep_priority = priority;
  t->wait_on_lock = NULL;
  locks_init(&(t->locks));

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);

  /* Advanced Scheduler */
  t->nice = running_thread()->nice;
  t->recent_cpu = running_thread()->recent_cpu;

  /* Latency */
  t->latency = INT64_MAX;
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if(!thread_mlfqs)
  {
    if (list_empty (&ready_list))
      return idle_thread;
    else
      return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
  else
  {
    int num_ready = 0;
    for(int i = 0; i<64; i++)
    {
      num_ready += list_size(find_ready_prior(&multi_ready,i));
    }
    if(num_ready == 0)
      return idle_thread;
    else
      return list_entry(list_pop_front(find_ready_max(&multi_ready)),struct thread, elem);
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
