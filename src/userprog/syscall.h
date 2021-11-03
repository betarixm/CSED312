#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define PHYS_BASE 0xc0000000
#define STACK_BOTTOM 0x8048000

#include <stdbool.h>

typedef int pid_t;

void syscall_init (void);

void get_argument (void *esp, int *arg, int count);
bool validate_addr (void * addr);

void sys_halt (void);
void sys_exit (int status);
pid_t sys_exec (const char *cmd_line);
int sys_wait (pid_t pid);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);

#endif /* userprog/syscall.h */
