#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define STACK_ACCESS_LIMIT 32
#define MAX_STACK_SIZE (1<<26)
/* 2^26 bits === 8MB. */

/* Lock for file system calls. */
struct lock f_lock;

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *, void *, bool);

#endif /* userprog/process.h */
