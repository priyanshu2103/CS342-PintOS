#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <malloc.h>

struct list frame_table;
struct lock frame_table_lock;

static void *allocate_frame (enum palloc_flags);
 
void frame_table_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_table_lock);
}

void * allocate_frame_to_page (enum palloc_flags flags, struct spt_entry *spte)
{
  if (flags & PAL_USER == 0)
    return NULL;

  void *frame = allocate_frame (flags);

  if (frame != NULL && spte != NULL)
  {
    add_to_frame_table (frame, spte);
    return frame;
  }
  else return NULL;
}

static void add_to_frame_table (void *frame, struct spt_entry *spte) {
  struct frame_table_entry *ft_entry = (struct frame_table_entry *) malloc (sizeof (struct frame_table_entry));
  ft_entry->frame = frame;
  ft_entry->spte = spte;
  ft_entry->t = thread_current ();
  list_push_back (&frame_table, &ft_entry->elem);
}


/* Returns kernel virtual address of a kpage taken from user_pool. */
static void * allocate_frame (enum palloc_flags flags)
{
  if (flags & PAL_USER == 0)
    return NULL;

  void *frame = palloc_get_page (flags);
  if (frame != NULL)
    return frame;
  else
    return NULL;
}
