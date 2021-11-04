#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
    arg[i] = *(int32_t *)(esp + 1 + i);
  }
}

static void
syscall_handler (struct intr_frame *f)
{
  if (!validate_addr (f->esp))
  {
    sys_exit (-1);
  }
  
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
      break;
    case SYS_WAIT:
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
      break;

    case SYS_WRITE:
      get_argument (f->esp, &argv[0], 3);
      if (!validate_addr ((void*) argv[1])) 
        sys_exit (-1);

      f->eax = sys_write ((int) argv[0], (const void*) argv[1], (unsigned) argv[2]);
      break;

    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
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
  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}

pid_t 
sys_exec (const char *cmd_line)
{

}

int 
sys_wait (pid_t pid)
{

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
  
  if (file == NULL || !validate_addr (file)) {
    sys_exit (-1);
  }

  file_ = filesys_open (file);
  if (file_ == NULL) 
    return -1;

  t->pcb->fd_table[t->pcb->fd_count++] = file_;

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

}

int 
sys_write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf(buffer, size);
    return size;
  }

  return -1;
}

void 
sys_seek (int fd, unsigned position)
{

}

unsigned 
sys_tell (int fd)
{

}

void 
sys_close (int fd)
{

}
