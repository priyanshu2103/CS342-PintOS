#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/pagedir.h"

#include "userprog/process.h"

#include "vm/page.h"
#include "filesys/filesys.h"



static void syscall_handler (struct intr_frame *);
static void check_validity (const void*, const void *);

static void check_sanity (const void*, const void *, size_t);
static void check_string (const void *, const char *);

static bool check_page_validity (void *);


// Functions related to files and filesys are pre-written in src/filesys/
// eg. filesys_open, file_length etc.
static int halt (void *esp)
{
  power_off ();
}

int exit (void *esp)
{
  int status=0;
  if (esp != NULL)
  {
    check_sanity (esp, esp, sizeof(int));
    status = *((int *)esp);
    esp += sizeof (int);
  }
  else 
  {
    status = -1;
  }

  struct thread *th = thread_current ();

  int i;
  for (i = 2; i<MAX_FILES; i++)
  {
    if (th->files[i] != NULL)
    {
	lock_acquire (&f_lock);
   	file_close (th->files[i]);
    	th->files[i] = NULL;
    	lock_release (&f_lock);
    }
  }

  remove_spt_table (&th->supp_page_table);
  char *name = th->name;
  char *temp;
  name = strtok_r (name, " ", &temp);
  lock_acquire (&f_lock);
  printf ("%s: exit(%d)\n", name, status);
  lock_release (&f_lock);

  th->return_status = status;

  /* Preserve the kernel struct thread just deallocate user page.
     struct thread will be deleted once parent calls wait or parent terminates.*/
  process_exit ();

  enum intr_level old_level = intr_disable ();
  th->no_yield = true;
  sema_up (&th->sema_terminated);
  thread_block ();
  intr_set_level (old_level);

  thread_exit ();
  NOT_REACHED ();

}


static int exec (void *esp)
{
  check_sanity (esp, esp, sizeof (char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  check_string (esp, file_name);

  lock_acquire (&f_lock);
  tid_t tid = process_execute (file_name);
  lock_release (&f_lock);

  struct thread *child = get_child_thread_from_id (tid);
  if (child == NULL)
    return -1;

  sema_down (&child->sema_ready);
  if (!child->load_complete)
    tid = -1;

  sema_up (&child->sema_ack);
  return tid;
}

static int wait (void *esp)
{

  check_sanity (esp, esp, sizeof (int));
  int pid = *((int *) esp);
  esp += sizeof (int);

  
  struct thread *child = get_child_thread_from_id (pid);

    /* Either wait has already been called or 
     given pid is not a child of current thread. */
  
  if (child == NULL) 
    return -1;

  sema_down (&child->sema_terminated);
  int status = child->return_status;
  list_remove (&child->parent_elem);
  thread_unblock (child);
  return status;
}

static int create (void *esp)
{
  check_sanity (esp, esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  check_string (esp, file_name);

  check_sanity (esp, esp, sizeof(unsigned));
  unsigned initial_size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  lock_acquire (&f_lock);
  int status = filesys_create (file_name, initial_size);
  lock_release (&f_lock);

  return status;
}

static int remove (void *esp)
{
  check_sanity (esp, esp, sizeof(char *));
  const char *file_name = *((char **) esp);
  esp += sizeof (char *);

  check_string (esp, file_name);

  lock_acquire (&f_lock);
  int status = filesys_remove (file_name);
  lock_release (&f_lock);

  return status; 
}

static int open (void *esp)
{  
  check_sanity (esp, esp, sizeof(char *));
  const char * file_name = *((char **) esp);
  esp += sizeof (char *);

  check_string (esp, file_name);

  lock_acquire (&f_lock);
  struct file * temp = filesys_open (file_name);
  lock_release (&f_lock);

  if (temp == NULL) 
  {	
	return -1;
  }

  struct thread * th = thread_current ();
  int i;
  for (i = 2; i<MAX_FILES; i++)		// from 2 as 0 and 1 are reserved for STDIN and STDout resp.
  {
    if (th->files[i] == NULL)
    {
 	th->files[i] = temp;
      	break;
    }
  }

  if (i == MAX_FILES) return -1;
  return i;
}

static int filesize (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);

  struct thread * th = thread_current ();

  if (fd >= 0 && fd < MAX_FILES && th->files[fd] != NULL)
  {  
    lock_acquire (&f_lock);
    int size = file_length (th->files[fd]);
    lock_release (&f_lock);
    return size;
  }
  return -1;
}

static int read (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  check_sanity (esp, esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);

  check_sanity (esp, esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  check_sanity (esp, buffer, size);

  struct thread *th = thread_current ();
  if (fd == STDIN_FILENO)
  {
    lock_acquire (&f_lock);

    int i;
    for (i = 0; i<size; i++)
    {
      *((char *) buffer+i) = input_getc ();
    }
    lock_release (&f_lock);
    return i;
  }
  else if (fd < MAX_FILES && fd >=2 && th->files[fd] != NULL)
  {
    
    struct spt_entry *spte = uvaddr_to_spt_entry (buffer);
    if (spte->type == FILE && !spte->writable)
    exit (NULL);
    lock_acquire (&f_lock);
    int read = file_read (th->files[fd], buffer, size);
    lock_release (&f_lock);
    return read;
  }
  return 0;
}

static int write (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  check_sanity (esp, esp, sizeof(void *));
  const void *buffer = *((void **) esp);
  esp += sizeof (void *);
  
  check_sanity (esp, esp, sizeof(unsigned));
  unsigned size = *((unsigned *) esp);
  esp += sizeof (unsigned);

  check_sanity (esp, buffer, size);

  struct thread * th = thread_current ();

  if (fd == STDOUT_FILENO)         // fd==0 => STDIN(read only)     fd==1 => STDOUT  
  {
    lock_acquire (&f_lock);

    int i;
    for (i = 0; i<size; i++)
    {
      putchar (*((char *) buffer + i));
    }
    lock_release (&f_lock);
    return i;
  }
  else if (fd >= 0 && fd < MAX_FILES && fd >=2 && th->files[fd] != NULL)
  {
    lock_acquire (&f_lock);
    int temp = file_write (th->files[fd], buffer, size);
    lock_release (&f_lock);
    return temp;
  }
  return 0;
}

static void seek (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  check_sanity (esp, esp, sizeof(unsigned));
  unsigned pos = *((unsigned *) esp);
  esp += sizeof (unsigned);

  struct thread *th = thread_current ();

  if (fd >= 0 && fd < MAX_FILES && th->files[fd] != NULL)
  {
    lock_acquire (&f_lock);
    file_seek (th->files[fd], pos);
    lock_release (&f_lock);
  }
}

static int tell (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  struct thread *th = thread_current ();

  if (fd >= 0 && fd < MAX_FILES && th->files[fd] != NULL)
  {
    lock_acquire (&f_lock);
    int pos = file_tell (th->files[fd]);
    lock_release (&f_lock);
    return pos;
  }
  return -1;
}

static void close (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *) esp);
  esp += sizeof (int);
 
  struct thread *th = thread_current ();

  if (fd >= 0 && fd < MAX_FILES && th->files[fd]!=NULL)
  {
    lock_acquire (&f_lock);
    file_close (th->files[fd]);
    th->files[fd] = NULL;
    lock_release (&f_lock);
  }
}

static int mmap (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int fd = *((int *)esp);
  esp += sizeof (int);

  if(fd < 0 || fd >= MAX_FILES)
    return -1;
  if (fd==0 || fd==1)
    return -1;

  check_sanity (esp, esp, sizeof(void *));
  const void *address = *((void **) esp);
  esp += sizeof (void *);

  if (!check_page_validity (address))
    return -1;

  struct thread *th = thread_current();
  struct file* temp = th->files[fd];

  if (temp == NULL)
    return -1;

  struct file *fp = file_reopen (temp);
  if (fp == NULL)
    return -1;

  int size = file_length (fp);

  struct spt_entry *spte = create_spte_mmap (fp, size, address);
  if (spte == NULL)
    return -1;

  int i;
  for (i = 0; i<MAX_FILES; i++)
  {
    if (th->mmap_files[i] == NULL){
      th->mmap_files[i] = spte;
      break;
    }
  }

  if (i == MAX_FILES)
    return -1;
  else
    return i;
}

static int munmap (void *esp)
{
  check_sanity (esp, esp, sizeof(int));
  int map_id = *((int *)esp);
  esp += sizeof (int);

  if (map_id >= 0 && map_id < MAX_FILES)
  {
    struct thread *th = thread_current();
    struct spt_entry *spte = th->mmap_files[map_id];

    if (spte != NULL)
      free_spte_mmap (spte);
  }
}

static int chdir (void *esp)
{
  return 0;
}

static int mkdir (void *esp)
{
  return 0;
}

static int readdir (void *esp)
{
  return 0;
}

static int isdir (void *esp)
{
  return 0;
}

static int inumber (void *esp)
{
  return 0;
}

static int (*syscalls []) (void *) =
  {
    halt,
    exit,
    exec,
    wait,
    create,
    remove,
    open,
    filesize,
    read,
    write,
    seek,
    tell,
    close,
    mmap,
    munmap,
    chdir,
    mkdir,
    readdir,
    isdir,
    inumber
  };

const int num_calls = sizeof (syscalls) / sizeof (syscalls[0]);

void syscall_init (void) 
{
  lock_init (&f_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall"); 
}

static void syscall_handler (struct intr_frame *f UNUSED) 
{  
  void * esp = f->esp;

  check_sanity (esp, esp, sizeof(int));
  int syscall_num = *((int *) esp);
  esp += sizeof(int);

  /* Just for sanity, we will anyway be checking inside all functions. */
  check_sanity (esp, esp, sizeof(int));

  if (syscall_num >= 0 && syscall_num < num_calls)
  {
    int (*aux_func) (void *) = syscalls[syscall_num];
    int ret = aux_func (esp);
    f->eax = ret;
  }
  else
  {
    printf ("\nError, invalid syscall number.");
    exit (NULL);
  }
}



// checks whether the given string (char pointer) is valid or not
static void check_string (const void *esp, const char *s)
{
  check_sanity (esp, s, sizeof(char));
  while (*s != '\0')
  {
    check_sanity (esp, s, sizeof(char));
    s++;
  }
}

// checks validity of starting and ending pointer references
static void check_sanity (const void *esp, const void *ptr, size_t size)
{
  check_validity (esp, ptr);
  if(size != 1)
  check_validity (esp, ptr + size - 1);
}

// checks whether given ptr is valid or not
static void check_validity (const void *esp, const void *ptr)
{
  uint32_t *pd = thread_current ()->pagedir;
  if (ptr == NULL || !is_user_vaddr (ptr))
    exit (NULL);
  if(pagedir_get_page (pd, ptr) == NULL)
  {
    struct spt_entry *spte = uvaddr_to_spt_entry (ptr);
    if (spte != NULL)
    {
      if (!install_load_page (spte))
        exit (NULL);
    }
    else if (!(ptr >= esp - STACK_ACCESS_LIMIT && add_stack_pages (ptr)))
      exit (NULL);
  }
}

//upage must not be 0 and it should be page aligned
static bool
check_page_validity (void *upage)
{
  if ((uintptr_t) upage % PGSIZE != 0)
    return false;  

  if (upage == 0)
    return false;

  return true;
}

