#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "threads/fixed-point.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Ready to run but not yet scheduled. */
    THREAD_BLOCKED,     /* Blocked, waiting for an event. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

      4 kB +---------------------------------+
           |          kernel stack           |
           |                |                |
           |                v                |
           |                                 |
           |                                 |
           |                                 |
           |                                 |
           |                                 |
           |                                 |
           |                                 |
           |                                 |
           +---------------------------------+
           |              magic              |
           |                :                |
           |                :                |
           |          status, name, etc.     |
           |                                 |
           |                                 |
      0 kB +---------------------------------+

   The upshot of this is dual:

      1. A stack overflow will disrupt the thread structure's
         `magic' member.  Don't tell us we didn't warn you.

      2. You can get the struct thread for the running thread by
         rounding the stack pointer down to a page boundary.
         The `running_thread()' macro in thread.c does this. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    
    /* Donation fields used for Priority Scheduling */
    int base_priority;                  /* Base priority before any donations. */
    struct list locks_held;             /* List of locks held by this thread. */
    struct lock *lock_waiting;          /* The lock this thread is currently blocked on. */

    /* Advanced Scheduler (MLFQS) fields */
    int nice;                           /* Nice value determining thread cooperativeness. */
    int recent_cpu;                     /* Real number estimating CPU time received recently (Fixed-point). */

    /* Shared between thread.c and synch.c. */
    struct list_elem allelem;           /* List element for all threads list. */
    struct list_elem elem;              /* List element for ready list or waiters list. */

    int64_t wake_up_tick;               /* Execution tick for alarm clock wake up. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs an action on a thread. */
typedef void thread_action_func (struct thread *thread, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
bool thread_compare_priority (const struct list_elem *a, const struct list_elem *b, void *aux);

/* MLFQS formula calculation declarations */
void mlfqs_calculate_priority (struct thread *t);
void mlfqs_calculate_recent_cpu (struct thread *t);
void mlfqs_calculate_load_avg (void);
void mlfqs_increment_recent_cpu (void);
void mlfqs_recalculate_all (void);
void thread_mlfqs_sort_ready_list (void);
void thread_update_priority (struct thread *t);

#endif /* threads/thread.h */