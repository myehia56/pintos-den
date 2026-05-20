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

/* Initializes semaphore SEMA to VALUE. A semaphore is a
   nonnegative integer coupled with a list of threads blocked on
   it. */
void sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to be positive and then decrements it.  If SEMA's value is 0,
   the current thread blocks.

   This function must be called with interrupts turned off.  This
   is necessary for atomicity.  Before sleeping, we re-enable
   interrupts. */
void sema_down (struct semaphore *sema) 
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
   semaphore's value is currently positive.  Returns true if
   successful, false otherwise. */
bool sema_try_down (struct semaphore *sema) 
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
   and wakes up one of the threads waiting on SEMA, if any.

   This function may be called from an interrupt context. */
void sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  struct thread *unblocked_thread = NULL;
  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    {
      /* If using priority scheduling, unblock the highest priority waiter */
      list_sort (&sema->waiters, thread_compare_priority, NULL);
      unblocked_thread = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
      thread_unblock (unblocked_thread);
    }
  sema->value++;
  if (unblocked_thread != NULL && thread_current ()->priority < unblocked_thread->priority) 
    {
      if (!intr_context ())
        thread_yield ();
      else
        intr_yield_on_return ();
    }
  intr_set_level (old_level);
}

/* Initializes LOCK. A lock is a semaphore with a value of 1.
   A lock is held by at most one thread at a time.  Our
   implementation is non-recursive. */
void lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock is non-recursive.

   The lock may not be acquired by a thread that already holds
   it.  This is a self-deadlock. */
void lock_acquire (struct lock *lock)
{
  struct thread *cur = thread_current ();
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  /* Priority donation only applies if MLFQS is off */
  if (!thread_mlfqs && lock->holder != NULL && lock->holder != cur) 
    {
      cur->lock_waiting = lock;
      struct lock *l = lock;
      while (l != NULL && l->holder != NULL)
        {
          struct thread *holder = l->holder;
          if (holder->priority < cur->priority) 
            {
              holder->priority = cur->priority;
              l = holder->lock_waiting;
            }
          else
            break;
        }
    }

  sema_down (&lock->semaphore);
  cur = thread_current ();
  cur->lock_waiting = NULL;
  lock->holder = cur;
  list_push_back (&cur->locks_held, &lock->elem);
  
  if (!thread_mlfqs)
    thread_update_priority (cur); 
}

/* Tries to acquire LOCK without sleeping.  Returns true if
   successful, false otherwise. */
bool lock_try_acquire (struct lock *lock)
{
  bool success;
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));
  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be held by the current thread. */
void lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  list_remove (&lock->elem);
  lock->holder = NULL;
  
  if (!thread_mlfqs)
    thread_update_priority (thread_current ());

  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise. */
bool lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/* Initializes CONDITION to be empty. */
void cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

/* Added: Comparator function to sort condition variable waiters by highest priority */
bool cond_sema_cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
  return sa->thread->priority > sb->thread->priority;
}

/* Releases LOCK and waits for CONDITION to be signaled.
   After waking up, re-acquires LOCK. */
void cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  /* Crucial Fix: The semaphore MUST be initialized before putting it on the wait list */
  sema_init (&waiter.semaphore, 0); 
  waiter.thread = thread_current ();
  
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on CONDITION, signals one of them. */
void cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
    {
      /* Crucial Fix: Sort the condition waiters so we wake the highest priority thread first */
      list_sort (&cond->waiters, cond_sema_cmp_priority, NULL);
      struct semaphore_elem *waiter = list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
      sema_up (&waiter->semaphore);
    }
}

/* Signals all threads waiting on CONDITION. */
void cond_broadcast (struct condition *cond, struct lock *lock)
{
  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}