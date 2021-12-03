#include "vm/frame.h"
#include "threads/synch.h"

static struct list frame_table;
static struct lock frame_lock;

void
frame_init ()
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  struct fte *e;
  void *kpage;
  lock_acquire (&frame_lock);
  kpage = palloc_get_page (flags);
  if (kpage != NULL)
  {
    e = (struct fte *)malloc (sizeof *e);
    e->kpage = kpage;
    e->upage = upage;
    e->t = thread_current ();
    list_push_back (&frame_table, &e->list_elem);
  }
  lock_release (&frame_lock);
  return kpage;
}

void
falloc_free_page (void *kpage)
{
  struct fte *e;
  lock_acquire (&frame_lock);
  e = get_fte (kpage);
  if (e == NULL)
    sys_exit (-1);

  list_remove (&e->list_elem);
  palloc_free_page (e->kpage);
  pagedir_clear_page (e->t->pagedir, e->upage);
  free (e);

  lock_release (&frame_lock);
}

struct fte *
get_fte (void* kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    if (list_entry (e, struct fte, list_elem)->kpage == kpage)
      return list_entry (e, struct fte, list_elem);
  return NULL;
}