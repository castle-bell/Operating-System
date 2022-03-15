/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Compares the value of priority of threads which contains two list 
   elements A and B each, given auxiliary data AUX.  Returns true if A is
   less than B, or false if A is greater than or equal to B. */

static bool
cmp_w_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);
  struct thread *a_thread = list_entry(a,struct thread,w_elem);
  struct thread *b_thread = list_entry(b,struct thread,w_elem);
  return (a_thread->priority > b_thread->priority) ? true : false;
}

static bool
cmp_cond_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);
  struct semaphore_elem *a_sema_elem = list_entry(a,struct semaphore_elem,elem);
  struct semaphore_elem *b_sema_elem = list_entry(b,struct semaphore_elem,elem);
  struct list *a_list = &a_sema_elem->semaphore.waiters;
  struct list *b_list = &b_sema_elem->semaphore.waiters;
  /* Check there is a thread in semaphore waiter list */
  ASSERT(list_begin(a_list) != NULL);
  ASSERT(list_begin(b_list) != NULL);
  struct thread *a_thread = list_entry(list_begin(a_list),struct thread,w_elem);
  struct thread *b_thread = list_entry(list_begin(b_list),struct thread,w_elem);
  return (a_thread->priority > b_thread->priority) ? true : false;
}

int count_size(struct list *lst)
{
  struct list_elem *e = list_begin(lst);
  int i = 0;
  for(e;e!=list_end(lst);e=list_next(e))
  {
    i++;
  }
  return i;
}

/* Donate the priority of cur to the lock holder if the priority of lock_ holder is
   less than the priority of cur
   Notice: there is no current thread in waiters */
void priority_donation(struct lock *cur_wait_lock,struct thread *cur)
{
  ASSERT(cur != NULL);
  if(cur_wait_lock == NULL)
    return;
  struct thread *holder = cur_wait_lock->holder;
  if(cur->priority > holder->priority)
  {
    holder->priority = cur->priority;
    struct list *waiter = &cur_wait_lock->semaphore.waiters;
    list_sort(waiter,cmp_w_priority,NULL);
    priority_donation(holder->wait_on_lock,holder);
  }
}

/* Return highest priority of threads who wait for lock,
   if there is no threads who wait for lock, then return -1 */
int highest_priority_lock(struct lock *lock_)
{
  ASSERT(lock_ != NULL);
  struct list *waiters = &lock_->semaphore.waiters;
  if(list_empty(waiters))
    return -1;
  struct thread *first = list_entry(list_begin(waiters),struct thread, w_elem);
  return first->priority;
}

/* For thread_set_priority(), find the hightest priority of
   threads which are in lock_list of current thread */
int highest_priority_locklist(void)
{
  struct thread *cur = thread_current();
  struct lock* lock = cur->locks.head;
  int max_priority = -1;
  if(lock==NULL)
    return max_priority;
  
  for(struct lock* init = lock; init != NULL; init = locks_next(init))
  {
    if(max_priority < highest_priority_lock(init))
      max_priority = highest_priority_lock(init);
  }
  return max_priority;
}


void priority_withdrawal(struct lock *lock_)
{
  /* If there is a lock that the cur waits for, then it couldn't
     be executed */
  ASSERT(thread_current()->wait_on_lock == NULL);
  struct thread *cur = thread_current();

  /* Initialize the priority */
  cur->priority = cur->rep_priority;

  /* Find the highest priority in lock_list */
  int h_prior = highest_priority_locklist();
  if(cur->priority < h_prior)
    cur->priority = h_prior;
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_insert_ordered (&sema->waiters, &thread_current()->w_elem, &cmp_w_priority, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
  {
    list_sort(&sema->waiters,cmp_w_priority,NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, w_elem));
  }
  sema->value++;

  /* Preemption */
  preemption();

  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->next = NULL;
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current();
  cur->wait_on_lock = lock;

  /* donate priority */
  if(lock->holder != NULL)
  {
    priority_donation(lock,cur);
  }
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  thread_current()->wait_on_lock = NULL;
  lock_push_front(&thread_current()->locks,lock);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  lock_pop(&thread_current()->locks,lock);
  priority_withdrawal(lock);
  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  /* list_insert_order to current_thread priority */
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  if (!list_empty (&cond->waiters)) 
  {
    list_sort(&cond->waiters,cmp_cond_priority,NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
