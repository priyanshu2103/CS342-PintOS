#ifndef VM_PAGE
#define VM_PAGE

#include <hash.h>
#include "filesys/off_t.h"
#include "filesys/file.h"

enum spte_type
  {
    CODE = 0, /* Only code is swappable. */
    FILE = 1, /* Read only executable file. */
    MMAP = 2  /* Files mapped to memory. */
  };

struct spt_entry
  {
    enum spte_type type;
    void *upage;
    void *frame;
    struct hash_elem elem;
    bool present_in_swap;

    struct file *file;
    uint32_t page_read_bytes;
    uint32_t page_zero_bytes;
    off_t ofs;
    bool writable;
 
  };

void supp_page_table_init (struct hash *);
struct spt_entry *uvaddr_to_spt_entry (void *);

bool add_stack_pages (void *);
bool create_spte_file (struct file *, off_t, uint8_t *,uint32_t, uint32_t, bool);

struct spt_entry* create_spte_mmap (struct file *, int, void *);

void remove_spt_table (struct hash *);
void free_spte_mmap (struct spt_entry *);
static void spt_elem_free (struct hash_elem *, void *);
static void spt_free (struct spt_entry *);


#endif
