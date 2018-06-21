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
      list_push_back (&sema->waiters, &thread_current ()->elem);
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
  bool success;

  ASSERT (sema != NULL);

  if (sema->value > 0) 
    {
      //sema->value--;  //we don't need to change sema
      success = true; 
    }
  else
    success = false;

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

  if (!list_empty (&sema->waiters)) {
        int compare_type=compare_effective_max;
        list_less_func *comparator=&list_less;
        struct list_elem *max = list_max(&sema->waiters, comparator, &compare_type);
        list_remove(max);
        thread_unblock (list_entry (max,struct thread, elem));
  }
  sema->value++;
  intr_set_level (old_level);
  preemption_test(); /* we need to test preemtion as because we added new thread to ready list */
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
  sema_init (&lock->semaphore, 1);

  lock->holder = NULL;
  /************************************************************/
  lock->max_waiter_priority = 0;
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

/************************************************************/
  enum intr_level old_level;
  old_level = intr_disable ();


  struct thread *cur = thread_current();
  if(!lock_try_acquire(lock)){ // if current thread will be blocked (becomes waiting on current lock)

    cur->waited_lock = lock; // points to current lock to keep track of it's holder
    struct thread *holder;
    while (cur->waited_lock != NULL){ // walk through nested locks 
      holder = cur->waited_lock->holder;
      /* 
      if cur thread to be blocked effective priority is higher than
       max_waiter_priority of the lock that means that this waiting
      thread has the highest priority in the list of waiters on that lock
      */
      if(cur->waited_lock->max_waiter_priority < cur->effective_priority)
          cur->waited_lock->max_waiter_priority = cur->effective_priority;
      /* 
      donate to holder if it has lower effective priority
      */       

      if(holder-> effective_priority<cur->effective_priority)
          holder-> effective_priority=cur->effective_priority;
      else
           break;   
      cur = holder;
     }
   

    
  }else {
    list_push_front(&cur->holded_locks,&lock->elem);
  }
  intr_set_level(old_level);  

  sema_down (&lock->semaphore);

  lock->holder = thread_current ();
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
///////////////////////////////////////////////////////////
  struct thread *cur = thread_current();
  enum intr_level old_level;
  old_level = intr_disable ();

  remove_lock(&cur->holded_locks,&lock->elem);
/* 
if no holded locks then their is no waiting threads on me , 
return effective priority to original priority
*/
  if(list_empty(&cur->holded_locks))
      cur->effective_priority=cur->priority;
  else { //effective = max of holded locks priorities
    list_less_func *less = &comparer;
    cur->effective_priority=list_entry(
      list_max (&cur->holded_locks, less, NULL),struct lock,elem)->max_waiter_priority;

  }
///////////////////////////////////////////////////////////
  lock->holder = NULL;
  intr_set_level(old_level);
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

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

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
  enum intr_level old_level;

  struct thread *cur = thread_current();
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  list_push_back (&cond->waiters, &cur->elem);
  lock_release (lock);
  old_level = intr_disable ();//interrupt disable for thread block (important)
  thread_block();
  intr_set_level(old_level);
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

  if (!list_empty (&cond->waiters)) {
        int compare_type=compare_effective_max;
        list_less_func *comparator=&list_less;
        struct list_elem *max = list_max(&cond->waiters, comparator, &compare_type);
        list_remove(max);
        thread_unblock (list_entry (max,struct thread, elem));
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
/****************************************************************/


void remove_lock(struct list *list,struct list_elem *removed_elem){
struct list_elem *e;
struct lock *removed_lock = list_entry (removed_elem, struct lock, elem);
enum intr_level old_level;
    old_level = intr_disable ();    
  // interrupt disable .........
  for (e = list_begin (list); e != list_end (list);e=list_next(e))
    {
      struct lock *l = list_entry (e, struct lock, elem);
      if(l == removed_lock)
      {
        list_remove(e);
        return;
      }
      
    }
     intr_set_level(old_level);//important
}
bool comparer(const struct list_elem *a,const struct list_elem *b,void *aux UNUSED){

  struct lock *t1=list_entry(a,struct lock ,elem);
  struct lock *t2=list_entry(b,struct lock ,elem);
 
    if(t1->max_waiter_priority<t2->max_waiter_priority)
      return true;
   
  
  return false;

}