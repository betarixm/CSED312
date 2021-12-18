#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/malloc.h"

struct fte
  {
    void *kpage;
    void *upage;

    struct thread *t;

    struct list_elem list_elem;
  };

void frame_init (void);
void *falloc_get_page(enum palloc_flags, void *);
void  falloc_free_page (void *);
struct fte *get_fte (void* );

#endif
