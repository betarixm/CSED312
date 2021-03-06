#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
struct lock file_lock;
int read_count;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  lock_init (&file_lock);
  read_count = 0;
}

bool validate_addr (void *addr)
{
  if (addr >= STACK_BOTTOM && addr < PHYS_BASE && addr != 0)
    return true;

  return false;
}

void 
get_argument (int *esp, int *arg, int count)
{
  int i;
  for (i = 0; i < count; i++)
  {
    if (!validate_addr(esp + 1 + i)) { sys_exit(-1); }
    arg[i] = *(esp + 1 + i);
  }
}

static void
syscall_handler (struct intr_frame *f)
{
  if (!validate_addr (f->esp))
  {
    sys_exit (-1);
  }
  
  thread_current()->esp = f->esp;
  
  int argv[3];

  switch (*(int *)f->esp)
  {
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
      get_argument (f->esp, &argv[0], 1);
      sys_exit (argv[0]);
      break;

    case SYS_EXEC:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_exec (argv[0]);
      break;
    case SYS_WAIT:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_wait (argv[0]);
      break;
    case SYS_CREATE:
      get_argument (f->esp, &argv[0], 2);      
      f->eax = sys_create ((const char *) argv[0], (const char *) argv[1]);
      break;
    case SYS_REMOVE:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_remove (argv[0]);
      break;
    case SYS_OPEN:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_open (argv[0]);
      break;
    case SYS_FILESIZE:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_filesize (argv[0]);
      break;
    case SYS_READ:
      get_argument (f->esp, &argv[0], 3);
      f->eax = sys_read (argv[0], argv[1], argv[2]);
      break;
    case SYS_WRITE:
      get_argument (f->esp, &argv[0], 3);
      if (!validate_addr ((void*) argv[1])) 
        sys_exit (-1);

      f->eax = sys_write ((int) argv[0], (const void*) argv[1], (unsigned) argv[2]);
      break;

    case SYS_SEEK:
      get_argument (f->esp, &argv[0], 2);
      sys_seek (argv[0], argv[1]);
      break;
    case SYS_TELL:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_tell (argv[0]);
      break;
    case SYS_CLOSE:
      get_argument (f->esp, &argv[0], 1);
      sys_close (argv[0]);
      break;
    case SYS_MMAP:
      get_argument (f->esp, &argv[0], 2);
      f->eax = sys_mmap ((int) argv[0], (void *) argv[1]);
      break;
    case SYS_MUNMAP:
      get_argument (f->esp, &argv[0], 1);
      sys_munmap ((int) argv[0]);
      break;
  }
}

void 
sys_halt (void)
{

}

void 
sys_exit (int status)
{
  struct thread *t = thread_current ();
  t->pcb->exit_code = status;
  if (!t->pcb->is_loaded)
    sema_up (&(t->pcb->sema_load));

  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}

pid_t 
sys_exec (const char *cmd_line)
{
  pid_t pid = process_execute (cmd_line);
  struct pcb *child_pcb = get_child_pcb (pid);
  if (pid == -1 || !child_pcb->is_loaded) {
    return -1;
  }

  return pid;
}

int 
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

bool 
sys_create (const char *file, unsigned initial_size)
{
  if (file == NULL || !validate_addr (file)) {
    sys_exit (-1);
  }

  return filesys_create (file, initial_size);
}

bool 
sys_remove (const char *file)
{
  if (file == NULL || !validate_addr (file)) {
    sys_exit (-1);
  }

  return filesys_remove (file);
}

int 
sys_open (const char *file)
{
  struct file *file_;
  struct thread *t = thread_current ();
  int fd_count = t->pcb->fd_count;

  lock_acquire (&file_lock);
  if (file == NULL || !validate_addr (file)) 
  {
    lock_release (&file_lock);
    sys_exit (-1);
  }

  file_ = filesys_open (file);
  if (file_ == NULL)  
  {
    lock_release (&file_lock);
    return -1;
  }

  if (thread_current ()->pcb->file_ex && (strcmp (thread_current ()->name, file) == 0))
    file_deny_write (file_);
    
  t->pcb->fd_table[t->pcb->fd_count++] = file_;
  
  lock_release (&file_lock);

  return fd_count;
}

int 
sys_filesize (int fd)
{
  struct thread *t = thread_current ();
  struct file *file = t->pcb->fd_table[fd];

  if (file == NULL)
    return -1;

  return file_length (file);
}

int 
sys_read (int fd, void *buffer, unsigned size)
{
  if (!validate_addr(buffer)) {
    sys_exit (-1);
  }

  int fd_count = thread_current()->pcb->fd_count;
  int bytes_read;
  struct file *file = thread_current()->pcb->fd_table[fd];

  if (file == NULL || fd < 0 || fd > fd_count) {
    sys_exit (-1);
  }

  lock_acquire (&file_lock);
  bytes_read = file_read (file, buffer, size);
  lock_release (&file_lock);

  return bytes_read;
}

int 
sys_write (int fd, const void *buffer, unsigned size)
{
  int fd_count = thread_current()->pcb->fd_count;
  if (fd >= fd_count || fd < 1)
  {
    sys_exit (-1);
  } else if (fd == 1)
  {
    lock_acquire (&file_lock);
    putbuf(buffer, size);
    lock_release (&file_lock);
    return size;
  } else {
    int bytes_written;
    struct file *file = thread_current ()->pcb->fd_table[fd];

    if (file == NULL) {
      sys_exit (-1);
    }

    lock_acquire (&file_lock);
    bytes_written = file_write (file, buffer, size);
    lock_release (&file_lock);

    return bytes_written;
  }

  return -1;
}

void 
sys_seek (int fd, unsigned position)
{
  struct file *file;
  
  file = thread_current ()->pcb->fd_table[fd];
  if (file != NULL)
    file_seek (file, position);
}

unsigned 
sys_tell (int fd)
{
  struct file *file;
  
  file = thread_current ()->pcb->fd_table[fd];
  if (file == NULL)
    return -1;
    
  return file_tell (file);
}

void 
sys_close (int fd)
{
  struct file *file;
  struct thread *t = thread_current();
  int i;

  if (fd >= t->pcb->fd_count || fd < 2)
  {
    sys_exit(-1);
  }
  
  file = t->pcb->fd_table[fd];
  if (file == NULL)
    return;

  file_close(file);
  t->pcb->fd_table[fd] = NULL;
  for(i = fd; i < t->pcb->fd_count; i++)
  {
    t->pcb->fd_table[i] = t->pcb->fd_table[i + 1];
  }

  t->pcb->fd_count--;
}

int 
sys_mmap (int fd, void *addr)
{
  struct thread *t = thread_current ();
  struct file *f = t->pcb->fd_table[fd];
  struct file *opened_f;
  struct mmf *mmf;

  if (f == NULL)
    return -1;
  
  if (addr == NULL || (int) addr % PGSIZE != 0)
    return -1;

  lock_acquire (&file_lock);

  opened_f = file_reopen (f);
  if (opened_f == NULL)
  {
    lock_release (&file_lock);
    return -1;
  }

  mmf = init_mmf (t->mapid++, opened_f, addr);
  if (mmf == NULL)
  {
    lock_release (&file_lock);
    return -1;
  }

  lock_release (&file_lock);

  return mmf->id;
}

int 
sys_munmap (int mapid)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct mmf *mmf;
  void *upage;

  if (mapid >= t->mapid)
    return;

  for (e = list_begin (&t->mmf_list); e != list_end (&t->mmf_list); e = list_next (e))
  {
    mmf = list_entry (e, struct mmf, mmf_list_elem);
    if (mmf->id == mapid)
      break;
  }
  if (e == list_end (&t->mmf_list))
    return;

  upage = mmf->upage;

  lock_acquire (&file_lock);
  
  off_t ofs;
  for (ofs = 0; ofs < file_length (mmf->file); ofs += PGSIZE)
  {
    struct spte *entry = get_spte (&t->spt, upage);
    if (pagedir_is_dirty (t->pagedir, upage))
    {
      void *kpage = pagedir_get_page (t->pagedir, upage);
      file_write_at (entry->file, kpage, entry->read_bytes, entry->ofs);
    }
    page_delete (&t->spt, entry);
    upage += PGSIZE;
  }
  list_remove(e);

  lock_release (&file_lock);
}
