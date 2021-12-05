#include "vm/frame.h"
#include "threads/synch.h"
#include "vm/swap.h"

static struct list frame_table;
static struct lock frame_lock;
static struct fte *clock_cursor;

void
frame_init ()
{
  list_init (&frame_table);
  lock_init (&frame_lock);
  clock_cursor = NULL;
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

    evict_page();
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

void evict_page() {
  ASSERT(lock_held_by_current_thread(&frame_lock));

  struct fte *e = clock_cursor;

  /* BEGIN: Find page to evict */
  while (true) {
    if (clock_cursor == NULL || list_next(&clock_cursor->list_elem) == list_end(&frame_table)) {
      e = list_entry(list_begin(&frame_table), struct fte, list_elem);
    } else {
      e = list_next (e);
    }

    if(!pagedir_is_accessed(e->t->pagedir, e->upage)) {
      break;
    }

    pagedir_set_accessed(e->t->pagedir, e->upage, false);
  }
  /*  END : Find page to evict */

  swap_out(e->kpage);

  lock_release(&frame_lock); {
    falloc_free_page(e->kpage);
  } lock_acquire(&frame_lock);
}