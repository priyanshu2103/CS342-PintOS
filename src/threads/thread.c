#include "fixed_point_real_arithmetic.h"
#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "devices/timer.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in THREAD_BLOCK state, that is, processes
    that are waiting */
static struct list sleepers;

static int64_t next_wakeup_time;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/*Manager thead*/
static struct thread *manager_thread;

/* Lock used by allocate_tid(). */
static struct thread *bsd_scheduler_thread;
static struct lock tid_lock;
static int load_avg;
static bool schedule_004;          // schedules at every slice (4th tick)
static bool schedule_100;	   // schedules at every sec (100th tick)
static void bsd_scheduler (void);

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

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static void manager_func ();
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

void bsd_scheduler ()						// function for bsd_scheduler, called only if thread_mlfqs is true
{
	  bsd_scheduler_thread = thread_current ();
	  enum intr_level old_level;
	  struct list_elem * temp;
	  while (true)
	  {
		      old_level = intr_disable ();
		      thread_block ();
		      intr_set_level (old_level);


		      if (schedule_100)                        // update load average and recent cpu at every 100th tick
		      {
			        thread_update_load_avg ();
			        for (temp = list_begin (&all_list); temp != list_end (&all_list); temp = list_next (temp))
			        {
				          struct thread * item = list_entry (temp, struct thread, allelem);
				          if (item != manager_thread &&  item != bsd_scheduler_thread &&   item != idle_thread)
				          { 
				          	thread_update_recent_cpu (item);
				          }
			        }
			        
			        schedule_100 = false;
		      }
	      
		      if (schedule_004)			      // update priority at every 4th tick
		      {
		        
			        for (temp = list_begin (&all_list); temp != list_end (&all_list); temp = list_next (temp))
			        {
				          struct thread * item = list_entry (temp, struct thread, allelem);
				          if (item != manager_thread &&  item != bsd_scheduler_thread && item != idle_thread)
				          {
				        	  thread_update_priority (item);
				          }
			        }
			       
			        schedule_004 = false;
		      }
		  
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
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleepers);
  next_wakeup_time = INT64_MAX;
  load_avg = 0;
  schedule_100 = false;   
  schedule_004 = false;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

void timer_wakeup ()
{
  while (!list_empty (&sleepers))
  {
    struct thread *temp = list_entry (list_front (&sleepers),struct thread, s_elem);
    if (temp->wakeup_time <= next_wakeup_time)
    {
      list_pop_front(&sleepers);
      thread_unblock(temp);
    }
    else
    {
        next_wakeup_time = temp->wakeup_time;
        break;
    }
  }

  if (list_empty (&sleepers))
    next_wakeup_time = INT64_MAX;
  /*else
    next_wakeup_time = list_entry(list_front(&sleepers),struct thread, s_elem)->wakeup_time;
*/                            
}

void manager_func ()					// function for manager thread
{
  manager_thread = thread_current ();

  while (true)
  {
    enum intr_level old_level=intr_disable();
    thread_block ();
    intr_set_level(old_level);
    timer_wakeup ();    
  }
}

void thread_update_priority (struct thread *temp)
{
  
  int aux = ADD_INT (DIVIDE_INT (temp->recent_cpu, 4), 2*temp->nice);
  temp->priority = TO_INT_ZERO (INT_SUB (PRI_MAX, aux));
  
}

void thread_update_recent_cpu (struct thread * temp)
{
  int dbl = MULTIPLY_INT (load_avg, 2);
  int a = DIVIDE (dbl, ADD_INT (dbl, 1));
  int b = MULTIPLY (a, temp->recent_cpu);
  temp->recent_cpu = ADD_INT (b, temp->nice);
}

void thread_update_load_avg ()
{
  int count = 0;
  struct list_elem * temp;
  for (temp = list_begin (&ready_list); temp != list_end (&ready_list); temp = list_next (temp))
  {
    struct thread * x = list_entry (temp, struct thread, elem);
    if (x != manager_thread && x != bsd_scheduler_thread && x != idle_thread)
    {
      count++;
    }
  }
  struct thread * x  = thread_current ();
  if (x != manager_thread && x != bsd_scheduler_thread && x != idle_thread)
  {
    count++;
  }
  int64_t n = ADD_INT (MULTIPLY_INT (load_avg, 59), count);
  load_avg = DIVIDE_INT (n, 60);
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

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);

  thread_create ("manager", PRI_MAX, manager_func, NULL);
  thread_create ("bsd_scheduler", PRI_MAX, bsd_scheduler, NULL);
}

bool before(const struct list_elem *a, const struct list_elem *b,void *aux UNUSED)
{
   struct thread * temp1,* temp2;
   temp1 = list_entry(a, struct thread, s_elem);
   temp2 = list_entry(b, struct thread, s_elem);
   return temp1->wakeup_time < temp2->wakeup_time;
}

bool priority_compare (const struct list_elem *a, const struct list_elem *b,void *aux UNUSED)
{
  struct thread * temp1,* temp2;
  temp1 = list_entry (a, struct thread, elem);
  temp2 = list_entry (b, struct thread, elem);
  if(thread_mlfqs)
  {
	  return temp1->priority > temp2->priority ;
  }	
  return thread_get_effective_priority (temp1) > thread_get_effective_priority (temp2);
}

bool priority_compare_mlfqs (const struct list_elem *x, const struct list_elem *y, void *aux UNUSED)
{
  struct thread *temp1 = list_entry (x, struct thread, elem);
  struct thread *temp2 = list_entry (y, struct thread, elem);
  return temp1->priority > temp2->priority ;
}

void thread_set_next_wakeup()
{
   enum intr_level old_level;
   old_level = intr_disable();

  if (list_empty (&sleepers))
    next_wakeup_time = INT64_MAX;
  else
  {
    struct list_elem *first = list_front (&sleepers);
    struct thread *thrd = list_entry (first, struct thread, s_elem);
    if (thrd->wakeup_time <= next_wakeup_time && timer_ticks() >= next_wakeup_time)
    {
      list_pop_front (&sleepers);
      thread_unblock (thrd);

      if (list_empty (&sleepers))
        next_wakeup_time = INT64_MAX;
      else
      {
        first = list_front (&sleepers);
        thrd = list_entry (first, struct thread, s_elem);
        next_wakeup_time = thrd->wakeup_time;
      }
    }
    else
      next_wakeup_time = thrd->wakeup_time;
  }
  intr_set_level (old_level);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  t->recent_cpu = ADD_INT (t->recent_cpu,1); 

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

// unblock the manager thread if ticks is equal to next_wakeup_time
  long long ticks_till_now = timer_ticks ();
  if (ticks_till_now >= next_wakeup_time && manager_thread->status == THREAD_BLOCKED)
    {
	  thread_unblock (manager_thread);
    }
  if (ticks_till_now % TIMER_FREQ == 0)
    {
    	schedule_100 = true;
    }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
  {
     schedule_004=true;
     intr_yield_on_return ();
  }
     /*** schedule_100 for Task 2 and schedule_004 for Task 3 ***/
  if ((schedule_100 || schedule_004) && bsd_scheduler_thread->status == THREAD_BLOCKED && thread_mlfqs)
  {
    thread_unblock (bsd_scheduler_thread);
    intr_yield_on_return ();
  }
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
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

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

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  //thread_yield();            // puts the created thread into the ready list and schedules the thread

  if (intr_context ())
    intr_yield_on_return ();
  else
    thread_yield ();
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
   be important: if the cal:ler had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  /*list_insert_ordered (&ready_list, &t->elem,priority_compare,NULL);*/
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
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
     when it call schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
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
    list_push_back (&ready_list, &cur->elem);

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

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  int current_priority = thread_current()->priority;
  thread_current ()->priority = new_priority;
  if(new_priority < current_priority)
  {
	if (intr_context ())
      	    intr_yield_on_return ();
        else
      	    thread_yield ();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if (thread_mlfqs)
  {
    return thread_current()->priority;
  }
  return thread_get_effective_priority(thread_current());

}


int thread_get_effective_priority (struct thread *temp)
{
  if(!list_empty (&temp->locks_acquired))
  {
	
    int max = temp->priority;
    struct list_elem *temp2 = list_begin (&temp->locks_acquired);
    
    while(temp2 != list_end (&temp->locks_acquired))
    {
      struct lock *temp_lock = list_entry (temp2, struct lock, elem);
      struct list *w = &temp_lock->semaphore.waiters;

      if(!list_empty (w))
      {
        struct list_elem *temp3 = list_begin (w);
        while(temp3 != list_end (w))
        {
          struct thread *temp4 = list_entry(temp3, struct thread, elem);
          int x = thread_get_effective_priority(temp4);
          if(x > max)
            max = x;

	   temp3 = list_next (temp3);
        }
      }
	temp2 = list_next (temp2);
    }
    return max;
  }
  else
    return temp->priority;
}
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  struct thread * temp = thread_current ();
  temp->nice = nice;
  thread_update_priority (temp);
  //thread_yield (); 
  if (intr_context ())
    intr_yield_on_return ();
  else
    thread_yield (); 
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return TO_INT_NEAREST (MULTIPLY_INT (load_avg, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return TO_INT_NEAREST (MULTIPLY_INT (thread_current ()->recent_cpu, 100));

}


void thread_priority_temporarily_up()
{
   struct thread * temp = thread_current();
   temp->old_priority = temp->priority;
   thread_set_priority(PRI_MAX);
}

void thread_priority_restore() 
{
   struct thread * temp = thread_current();
   thread_set_priority(temp->old_priority);
}


void thread_block_till(int64_t wakeup_time)
{
   struct thread * t = thread_current();
   enum intr_level old_level;
   old_level = intr_disable();
   t->wakeup_time = wakeup_time;
   if(wakeup_time < next_wakeup_time)
	next_wakeup_time = wakeup_time;                                         
   // lock the sleepers list while we are inserting current thread in sleepers
   list_insert_ordered(&sleepers,&t->s_elem,before,NULL);
   thread_block();
   // setting the interrupt to old level
   intr_set_level(old_level);
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
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->wakeup_time = -1;
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->old_priority = priority;
  
  if (t == initial_thread)
    t->nice= 0;
  else
    t->nice = thread_current ()->nice;
    
  t->recent_cpu = 0;
  list_init (&t->locks_acquired);
  t->magic = THREAD_MAGIC;
  
  list_push_back (&all_list, &t->allelem);

  int i;
  for (i = 0; i<MAX_FILES ; i++)
  {
    t->files[i] = NULL;
  }
  

  if (t != initial_thread)
  {
    t->parent = thread_current();
    list_push_back (&(t->parent->children), &t->parent_elem);
  }
  else
    t->parent = NULL;

  list_init (&t->children);

  sema_init (&t->sema_ready, 0);
  sema_init (&t->sema_terminated, 0);
  sema_init (&t->sema_ack, 0);
  t->return_status = -1;
  t->load_complete = false;
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
  if (list_empty (&ready_list))
    return idle_thread;
  /* Ready list is no more ordered , hence remove list_min */
  else
  {
    struct list_elem *e;
    if(thread_mlfqs==true)
    {
       e= list_min (&ready_list, priority_compare_mlfqs, NULL);
    }
    else
    {
      e = list_min (&ready_list, priority_compare, NULL);
    }
    
    list_remove (e);
    return list_entry (e, struct thread, elem);
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   P\EV, the new thread is already running, and interrupts are
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
schedule_tail (struct thread *prev) 
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

   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();                    // returns the running thread
  struct thread *next = next_thread_to_run ();               // returns next thread to run from ready queue, if ready queue is empty, it returns idle thread.
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));
/* 
Switches from CUR, which must be the running thread, to NEXT,
which must also be running switch_threads(), returning CUR in
NEXT's context
*/
  if (cur != next)
    prev = switch_threads (cur, next);
  schedule_tail (prev);                                      // marks the new thread as running, if the prev thread is dying, free it from the memory space
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

struct thread *
get_child_thread_from_id (int id)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct thread *child = NULL;

  for (e = list_begin (&t->children);
       e!= list_end (&t->children); e = list_next (e))
  {
    struct thread *this = list_entry (e, struct thread, parent_elem);
    if (this->tid == id)
    {
      child = this;
      break;
    }
  }
  return child;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
