Lab 2: User Programs
===
김민석 (20180174), 권민재 (20190084) `CSED312`


# Process Management

## Implementation

### Data Structures

#### 1. Process Context Block
```c
struct pcb
  {
    int exit_code;
    bool is_exited;
    bool is_loaded;

    struct file **fd_table;
    int fd_count;
    struct file *file_ex;

    struct semaphore sema_wait;
    struct semaphore sema_load;
  };
```
```c
struct thread
  {
    // ...
#ifdef USERPROG
    // ...
    struct pcb *pcb;                    /* PCB. */
    // ...
  };
```
프로세스의 정보를 저장 및 추적하기 위해서 `pcb` 구조체를 선언해 각 프로세스의 정보를 관리할 수 있도록 구현하였다. `pcb` 구조체는 각 `thread`에 포함되어 스레드가 컨텍스트를 유지할 수 있도록 도움을 준다. `exit_code`는 `exit` 시스템콜에 의해 설정되며, `is_exited`와 `is_loaded`는 현재 프로세스의 상태를 나타내는 플래그이다. 프로세스에게 할당된 파일 디스크립터는 `fd_table`과 `fd_count`에 의해 관리되며, 프로세스가 실행을 위해 로드한 파일은 `file_ex`가 가리키게 된다. 더불어, 각 프로세스들끼리의 syncronize를 맞추기 위해 `wait`와 `load`에 대한 세마포어도 PCB에 포함시켰다. 이들의 값은 스레드가 생성될 때, 즉 `thread_create`에서 초기화된다. 


#### 2. parent-child Relationship
```c
struct thread
  {
    // ...
#ifdef USERPROG
    // ...
    struct thread *parent_process;
    struct list list_child_process;
    struct list_elem elem_child_process;
#endif
    // ...
  };
```
각 프로세스의 부모 프로세스와 자식 프로세스 정보를 저장, 추적하기 스레드 구조체에 위 필드를 추가했다. 어떤 스레드가 `exec`을 수행하면, 새로운 프로세스가 생성되는데, 이러한 프로세스들을 부모-자식 트리와 같은 형태로 표현하기 위해 위와 같은 구조를 정의하였다. `parent_process`에는 이 프로세스를 실행시킨 부모가 저장되며, `list_child_process`에 자신이 실행시킨 자식 프로세스들이 저장된다. 이들의 값 또한 스레드가 생성 될 때, 즉 `thread_create`에서 초기화된다.


### Algorithms
#### 0. New Data Structure Initialization
```c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  // ...
  t->parent_process = thread_current ();

  t->pcb = palloc_get_page (0);

  if (t->pcb == NULL) {
    return TID_ERROR;
  }

  t->pcb->fd_table = palloc_get_page (PAL_ZERO);

  if (t->pcb->fd_table == NULL) {
    palloc_free_page (t->pcb);
    return TID_ERROR;
  }

  t->pcb->fd_count = 2;
  t->pcb->file_ex = NULL;
  t->pcb->exit_code = -1;
  t->pcb->is_exited = false;
  t->pcb->is_loaded = false;

  sema_init (&(t->pcb->sema_wait), 0);
  sema_init (&(t->pcb->sema_load), 0);

  list_push_back (&(t->parent_process->list_child_process), &(t->elem_child_process));
  // ...
}
```

 프로세스의 컨텍스트 유지를 위해 앞서 추가된 필드들은 프로세스가 생성될 때 초기화되어야 한다. 그래서 `thread_create ()`가 실행될 때 앞서 살펴본 필드들이 초기화 될 수 있도록 하였다.
 
#### 1. Execute Process
```c
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *parsed_fn;
  tid_t tid;

  // ...

  parsed_fn = palloc_get_page (0);
  if (parsed_fn == NULL) {
    return TID_ERROR;
  }

  strlcpy (parsed_fn, file_name, PGSIZE);

  pars_filename (parsed_fn);

  tid = thread_create (parsed_fn, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy); 
  } else {
    sema_down (&(get_child_pcb (tid)->sema_load));
  }
  
  palloc_free_page (parsed_fn);

  return tid;
}
```
전달 받은 커맨드에서 `pars_filename ()` 함수를 호출하여 파일 이름을 파싱한 이후, 이를 이용하여 올바른 실행 파일을 가진 스레드를 생성한다. 이때, 자식이 load 되는 동안 부모가 먼저 종료 되지 않도록 semaphore `load_sema`를 이용해 자식의 load가 끝날 때 까지 부모를 `block` 하였다.

##### 1.1. Load Sync.
```c
static void
start_process (void *file_name_)
{
  // ...

  sema_up (&(thread_current ()->pcb->sema_load));

  // ...
}
```
`process_execute ()`에서 부모 프로세스가 **DOWN**한 `sema_load`는 `start_process ()`에서 자식 프로세스의 load가 끝나면 **UP**된다. 이를 통해 부모 프로세스가 자식의 load를 대기할 수 있다.

```c
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
```
혹시 로드 중 `sema_load`를 **UP**하지 못하고 종료된 프로세스가 있을 경우 부모가 무한정 대기하는 상황이 발생할 수 있다. 이를 방지하기 위해, `sys_exit ()`에서 현재 프로세스가 load 중 종료 됐을 경우 `sema_load`를 **UP** 시켜서 부모 프로세스가 무한정 대기하는 상황에서 빠져나올 수 있도록 구현하였다.

#### 2. Waiting Process
```c
int
process_wait (tid_t child_tid) 
{
  struct thread *child = get_child_thread (child_tid);
  int exit_code;

  if (child == NULL)
    return -1;
  
  if (child->pcb == NULL || child->pcb->exit_code == -2 || !child->pcb->is_loaded) {
    return -1;
  }
  
  sema_down (&(child->pcb->sema_wait));
  exit_code = child->pcb->exit_code;

  list_remove (&(child->elem_child_process));
  palloc_free_page (child->pcb);
  palloc_free_page (child);

  return exit_code;
}
```
`child_tid`를 통해 기다릴 프로세스를 입력받으면, 부모가 해당 프로세스의 `sema_wait`를 **DOWN** 시켜서 해당 프로세스가 종료될 때 까지 `wait`할 수 있도록 구현하였다. 부모 프로세스가 자식 프로세스에 대해 대기하였다는 뜻은, 자식 프로세스가 이제 온전히 종료되었음을 의미하므로, `wait` 과정에서 종료된 자식 프로세스의 리소스를 할당 해제하여 메모리 누수를 막았다.

##### 2.1. Unblocking Waiting Process
```c
void
process_exit (void)
{
  // ...
  cur->pcb->is_exited = true;
  sema_up (&(cur->pcb->sema_wait));
  // ...
}
```
자식 프로세스가 종료될 때, 즉 `process_exit ()`과정에서, 본인의 `sema_wait`을 **UP**시킨다. 이를 통해, 이 프로세스를 대기하고 있는 부모 프로세스가 unblock 될 수 있다.

#### 3. Exiting Process
```c
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  int i;

  for (i = cur->pcb->fd_count - 1; i > 1; i--)
  {
    sys_close (i);
  }

  palloc_free_page (cur->pcb->fd_table);

  // ...
}
```

프로세스가 종료될 때, 프로세스가 관리하고 있는 파일 디스크립터에 해당하는 파일들을 모두 닫아주어야 한다. 이후, 메모리 누수를 막기 위해 프로세스의 `fd_table`에 할당된 메모리를 해제해주었다.

## Discuss
### 1. Sync.
부모-자식 프로세스 간 sync를 맞추기 위해 `sema_wait`, `sema_load` semaphore를 도입했다. 이 뿐만 아니라, 프로세스의 상태를 나타낼 수 있는 flag인 `is_loaded`, `is_exited` 를 도입해 프로세스의 상태를 파악했다. 이를 통해 lock을 홀딩한 채로 종료되는 상황, 자식 프로세스가 비정상 종료되는 상황 등을 감지하고 적절하게 대응할 수 있었다.

### 2. Memory Management
메모리 누수를 막기 위해 프로세스 종료 시 할당된 모든 메모리를 해제했다. 메모리 할당 후에, 성공적으로 할당되었는지 그 여부를 확인하였다. 만약 메모리가 초과되었다면 프로세스를 종료시켜 invalid한 메모리 접근으로 인한 page fault를 방지했다.

# Process Termination Messages

## Implementation

### Algorithms

#### 1. Print Messages
```c 
void 
sys_exit (int status)
{
  // ...
  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
  // ...
}
```
단순히 `exit` 시스템콜을 핸들링하는 함수 `sys_exit ()`의 처리 과정 중에 주어진 형식대로 종료 메시지를 출력하기만 하면 된다. 

## Discuss
### 1. Debuging
```c 
printf ("%d: exit(%d)\n", t->tid, status);
```
위 형식으로 출력하면 각 스레드의 종료 상황을 더 편리하게 파악할 수 있기 때문에, 유용한 디버깅 툴로 활용할 수 있었다.

# Argument Passing

## Implementation

### Algorithms

#### 0. Parsing Command
터미널에서 전달 받은 커맨드를 적절히 parsing해서 새로운 프로세스로 전달해야하기 때문에, 인자와 파일명을 분리할 수 있는 아래의 함수들을 구현하였다.

##### 0.1. `pars_arguments`
```c
int
pars_arguments (char *cmd, char **argv)
{
  char *token, *save_ptr;

  int argc = 0;

  for (token = strtok_r (cmd, " ", &save_ptr); token != NULL;
  token = strtok_r (NULL, " ", &save_ptr), argc++)
  {
    argv[argc] = token;
  }

  return argc;
}
```
프로세스로 전달된 커맨드에서 `strtok_r`을 이용하여 공백을 기준으로 인자를 분리해 `argv`에 순서대로 저장하고, 이에 따라 검출된 인자의 개수인 `argc`를 반환한다.

##### 0.2. `pars_filename`
```c
void
pars_filename (char *cmd)
{
  char *save_ptr;
  cmd = strtok_r (cmd, " ", &save_ptr);
}
```
프로세스로 전달된 커맨드에서 제일 앞부분인 파일 이름만 추출할 수 있도록, `strtok_r ()`을 한번만 호출하여 구현하였다.

#### 1. Initialize Stack
```c 
void
init_stack_arg (char **argv, int argc, void **esp)
{
  /* Push ARGV[i][...] */
  int argv_len, i, len;
  for (i = argc - 1, argv_len = 0; i >= 0; i--)
  {
    len = strlen (argv[i]);
    *esp -= len + 1;
    argv_len += len + 1;
    strlcpy (*esp, argv[i], len + 1);
    argv[i] = *esp;
  }

  /* Align stack. */
  if (argv_len % 4)
    *esp -= 4 - (argv_len % 4);

  /* Push null. */
  *esp -= 4;
  **(uint32_t **)esp = 0;

  /* Push ARGV[i]. */
  for(i = argc - 1; i >= 0; i--)
  {
    *esp -= 4;
    **(uint32_t **)esp = argv[i];
  }

  /* Push ARGV. */
  *esp -= 4;
  **(uint32_t **)esp = *esp + 4;

  /* Push ARGC. */
  *esp -= 4;
  **(uint32_t **)esp = argc;

  /* Push return address. */
  *esp -= 4;
  **(uint32_t **)esp = 0;
}
```
`start_process ()`가 `init_stack_arg()`를 호출하면 주어진 인자를 스택에 컨벤션대로 삽입하게 된다. 이를 통해 argument passing 과정이 실현된다. 우선 각 `argv[i][]`에 해당하는 데이터가 스택에 삽입되고 esp가 변경되어 스택이 자라게 된다. 이후 align을 맞추기 위해 스택에 일부 0이 삽입된다. 그 뒤에 `argv[i]`가 삽입되고, `argv`의 주소가 삽입된다. 마지막으로 `argc`와 `ret`가 스택에 삽입되며 스택에 인자와 돌아갈 주소를 삽입하는 과정이 종료된다.

#### 2. Starting Process
```c
static void
start_process (void *file_name_)
{
  // ...

  /* Pars arguments. */
  char **argv = palloc_get_page(0);
  int argc = pars_arguments(file_name, argv);
  success = load (argv[0], &if_.eip, &if_.esp);
  if (success)
    init_stack_arg (argv, argc, &if_.esp);
  palloc_free_page (argv);

  // ...
}
```
최종적으로, `start_process ()` 과정에서 전달받은 인자들이 스택에 컨벤션대로 삽입될 수 있도록 구현한 함수들을 exploit 한다. `pars_arguments ()`를 호출하여 주어진 인자들을 미리 할당한 `argv` 배열에 삽입하고, 이 데이터를 바탕으로 `load ()`와 `init_stack_arg ()`를 호출하였다. 이를 통해, 파일을 메모리로 불러오고, 주어진 인자를 스택에 올바른 컨벤션대로 구성할 수 있다. 이 과정을 거치면 프로세스의 실행에 필요한 정보들이 메모리에 모두 탑재되기 때문에, `argv`가 더이상 필요하지 않다. 그렇기 때문에, 마지막으로, `argv`에 할당된 메모리를 해제하여 메모리 누수를 막을 수 있게 구현하였다.


# System call

## Implementation

### Data Structures

#### 1. Syncronizing
```c
struct semaphore rw_mutex, mutex;
int read_count;
```
여러 프로세스가 `read` 혹은 `write` 시스템콜을 호출함에 따라, reader-writer 문제가 발생할 수 있다. 이를 방지하기 위해, writer가 다른 reader나 writer들과 상호 배제적으로 접근할 수 있도록 제한하는 세마포어인 `rw_mutex`와, reader들이 `read_count`에 상호배제적으로 접근할 수 있도록 제한하는 세마포어인 `mutex`를 도입하였다. 수업 슬라이드를 바탕으로 구현하였으며, 자세한 알고리즘은 아래 알고리즘 파트에서 확인할 수 있다.

### Algorithms

#### 1. `validate_addr`
```c 
#define PHYS_BASE 0xc0000000
#define STACK_BOTTOM 0x8048000

bool validate_addr (void *addr)
{
  if (addr >= STACK_BOTTOM && addr < PHYS_BASE && addr != 0)
    return true;

  return false;
}
```
시스템 콜을 하는 과정에서, 일부 인자에 대해 어떤 값이 커널 공간의 주소는 아닌지 확인해야할 필요성이 있기 때문에, 이를 검증하는 함수를 작성하였다. 현재는 virtual memory의 관점이기 때문에, 주어진 주소가 스레드 스택에 해당하는 주소인지 판별하는 것으로 갈음하였다.

#### 2. `get_argument`
```c
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
```
시스템 콜을 하는 과정에서, 스택에서 인자들을 가져오는 과정은 필수적이며 반복적으로 실행된다. 그렇기 때문에, 스택에서 인자들을 추출하여 배열에 넣어주는 함수인 `get_argument`를 구성하였다. 스택에 인자는 `esp + 4`부터 담겨있기 때문에, `esp + 4`에서부터 주어진 개수만큼 인자를 가져오는 과정을 반복한다. 이때, 해당 주소가 커널 스페이스의 주소라면 곧바로 종료될 수 있도록 `validate_addr`를 통해 주소를 검증하는 과정을 포함하였다.


#### 3. `syscall_handler`
```c 
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
      // ...
    case SYS_EXEC:
      get_argument (f->esp, &argv[0], 1);
      f->eax = sys_exec (argv[0]);
      break;
    // ...
  }
}
```
시스템콜 인터럽트가 일단 걸리게 되면, 인터럽트 프레임과 함께 `syscall_handler`가 커널 모드에서 호출된다. 일단 호출되면, 인터럽트 프레임의 esp가 유저 스페이스의 주소인지 검증하여 아니라면 바로 종료하도록 구현하였다. esp가 유저 스페이스의 주소가 아니라면 올바른 시스템콜이 아니기 때문이다. 이후, `get_argument ()`를 통해 인자를 받아올 배열인 `argv`를 미리 준비하고, 인터럽트 프레임의 esp를 참조하여 syscall 번호에 따라 적합한 액션을 취한다. `exec`과 같이 인자와 리턴 값을 필요로 하는 시스템콜의 경우, `get_argument`를 통해 인자를 획득하고, 인터럽트 프레임의 `eax`를 `sys_exec`의 반환값으로 설정하여 액션을 마무리한다. 다른 시스템콜들도 이와 유사한 과정을 거친다는 사실을 양지하여 아래 알고리즘들을 보면 도움이 될 것이다.

#### 4. exit
```c
void 
sys_exit (int status)
{
  struct thread *t = thread_current ();
  t->pcb->exit_code = status;
  if (!t->pcb->is_loaded)
    sema_up (&(t->pcb->sema_load));

  // ...

  thread_exit ();
}
```
`exit` 시스템콜이 불리면, 현재 스레드의 pcb에 주어진 상태를 기록하고, `load`된 상태가 아니라면 `sema_up (&(t->pcb->sema_load));`를 통해 대기 프로세스들이 작동할 수 있도록 한다. 이후, `thread_exit ()`를 호출하여 스레드가 종료될 수 있도록 한다.

#### 5. exec
```c
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
```
`exec` 시스템콜이 불리면, 우선 `process_execute ()`를 통해 프로세스를 생성한다. 이후, `get_child_pcb ()`로 생성된 프로세스의 PCB를 획득한 후 정상적으로 프로세스가 생성되고 로드되었는지 확인한다. 만약 오류가 없었다면 프로세스의 pid를 반환하고, 아니라면 -1를 반환한다.

#### 6. wait
```c
int 
sys_wait (pid_t pid)
{
  return process_wait (pid);
}
```
`wait` 시스템콜이 불리면, 단순히 주어진 pid를 통해 `process_wait ()`를 호출하는 것으로 마무리할 수 있다.

#### 7. create
```c
bool 
sys_create (const char *file, unsigned initial_size)
{
  if (file == NULL || !validate_addr (file)) {
    sys_exit (-1);
  }

  return filesys_create (file, initial_size);
}
```
`create` 시스템콜을 처리하기 위해서 우선 주어진 파일이 적합한 파일인지 확인하는 과정이 필요하다. 적합하지 않은 파일이었다면 바로 오류를 반환한다. 만약 적합한 파일이었다면, `filesys_create ()`를 호출하여 파일을 생성한다.

#### 8. remove
```c
bool 
sys_remove (const char *file)
{
  if (file == NULL || !validate_addr (file)) {
    sys_exit (-1);
  }

  return filesys_remove (file);
}
```
`remove` 시스템콜을 처리하기 위해서 우선 주어진 파일이 적합한 파일인지 확인하는 과정이 필요하다. 적합하지 않은 파일이었다면, 바로 오류를 반환한다. 만약 적합한 파일이었다면, `filesys_remove ()`를 호출하여 파일을 지운다. 


#### 9. open
```c
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

  // ...
    
  t->pcb->fd_table[t->pcb->fd_count++] = file_;

  return fd_count;
}
```
`open` 시스템콜이 일단 불리면 적절한 파일인지 검사하여 적절하지 않다면 오류를 반환한다. 또한, 주어진 파일이 `filesys_open ()`를 통해 정상적으로 열리지 않은 경우에도 오류를 반환해야 한다. 이 외의 경우에는 프로세스의 `fd_table`에 주어진 파일을 등록시키고, 파일 디스크립터를 반환하여 마무리한다.

#### 10. filesize
```c
int 
sys_filesize (int fd)
{
  struct thread *t = thread_current ();
  struct file *file = t->pcb->fd_table[fd];

  if (file == NULL)
    return -1;

  return file_length (file);
}
```
`filesize` 시스템콜을 처리하기 위해서, 우선 입력받은 파일 디스크립터가 유효한지 그 여부와 정상적으로 파일을 가리키고 있는지 그 여부를 확인한다. 만약 아니라면 오류를 반환해야 한다. 정상적인 경우라면, `file_length ()`를 호출하여 그 결과를 반환하도록 구현하였다.


#### 11. read
```c
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

  sema_down (&mutex);
  read_count++;
  if (read_count == 1) 
    sema_down (&rw_mutex);
  sema_up (&mutex);
  bytes_read = file_read (file, buffer, size);
  sema_down (&mutex);
  read_count--;
  if (read_count == 0)
    sema_up (&rw_mutex);
  sema_up (&mutex);

  return bytes_read;
}
```
`read` 시스템콜이 우선 호출되었을 때, 주어진 파일 디스크립터가 유효하지 않거나 디스크립터가 가리키는 파일이 존재하지 않으면 오류를 반환해야 한다. 그 외의 경우에는 `file_read ()`를 호출하여 파일을 읽어들이고, 읽은 바이트의 수를 반환할 수 있도록 해야한다.
이때, 다양한 프로세스가 `read` 혹은 `write` 시스템콜을 호출함에 따라 reader-writer 문제가 발생할 수 있다. 이를 해결하기 위해, 앞서 소개한 `mutex`와 `rw_mutex`를 이용한다. `read_count`를 이용하여 처음으로 읽기가 시작되는 시퀀스라면, 즉 `read_count`가 1이라면, `sema_down (&rw_mutex)`를 통해 다른 프로세스가 `write`할 수 없도록 만든다. 이후, 모든 읽기가 온전히 종료되었다면, `sema_up (&rw_mutex)`를 통해 다른 프로세스가 `write`할 수 있도록 만든다. 이때, `read_count`는 `mutex`를 통해 다른 reader들과의 상호배제성이 보호된다. 참고로, 모든 reader는 읽기 전에 `read_count`를 1 증가시키고, 읽은 후에 `read_count`를 1 감소시킨다.

#### 12. write
```c
int 
sys_write (int fd, const void *buffer, unsigned size)
{
  int fd_count = thread_current()->pcb->fd_count;
  if (fd >= fd_count || fd < 1) {
    sys_exit (-1);
  } else if (fd == 1) {
    putbuf(buffer, size);
    return size;
  } else {
    int bytes_written;
    struct file *file = thread_current ()->pcb->fd_table[fd];

    if (file == NULL) {
      sys_exit (-1);
    }

    sema_down (&rw_mutex);
    bytes_written = file_write (file, buffer, size);
    sema_up (&rw_mutex);

    return bytes_written;
  }

  return -1;
}
```
`write` 시스템콜을 처리하기 위해서는 크게 3가지 경우를 고려해야 한다. 첫째로, 주어진 파일 디스크립터가 올바른 파일 디스크립터가 아닌 경우에는 곧바로 오류를 반환해야 한다. 이를 위해 `sys_exit (-1)`를 호출하였다. 둘째로, 주어진 파일 디스크립터가 1, 즉 표준 입력일 경우에는 파일이 아니라 입력 (stdin)을 처리해야 한다. 이를 위해 `putbuf`를 이용하여 키보드 등의 stdin을 처리할 수 있도록 하였다. 마지막으로 세번째 경우가 바로 파일에 쓰는 과정을 수반한다. 이때, 해당 파일 디스크립터가 파일을 포함하는지 확인하여 파일과 연관되어 있지 않다면 오류를 반환하는 과정이 포함되어야 한다. 정상적인 파일이라면, `file_write ()`를 호출하여 쓴 바이트의 수를 반환하여야 한다.
이때, 다양한 프로세스가 `read` 혹은 `write` 시스템콜을 호출함에 따라 reader-writer 문제가 발생할 수 있다. 이를 해결하기 위해, `rw_mutex`를 이용하여 파일을 쓰는 프로세스가 해당 파일에 접근하는 다른 프로세스들과 상호배제적으로 동작할 수 있도록 구현하였다.

#### 13. seek

```c
void 
sys_seek (int fd, unsigned position)
{
  struct file *file;
  
  file = thread_current ()->pcb->fd_table[fd];
  if (file != NULL)
    file_seek (file, position);
}
```
`seek` 시스템콜을 구현하기 위해서는 단순히 현재 프로세스가 관리하고 있는 파일 디스크립터를 통해 파일을 획득한 후, `file_seek ()`를 호출하면 된다. 별도의 반환 값을 가지지 않기 때문에, 값을 반환할 필요는 없다.

#### 14. tell
```c
unsigned 
sys_tell (int fd)
{
  struct file *file;
  
  file = thread_current ()->pcb->fd_table[fd];
  if (file == NULL)
    return -1;
    
  return file_tell (file);
}
```
`tell` 시스템콜을 구현하기 위해서는 단순히 현재 프로세스가 관리하고 있는 파일 디스크립터를 통해 파일을 획득한 후, `file_tell ()`을 호출하여 그 결과를 반환하기만 하면 된다. 다만, 해당 파일이 존재하지 않는 경우를 핸들링해주었다.

#### 15. close
```c
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
```
`close` 시스템콜은 `sys_close ()`의 실행을 통해 실현된다. 주어진 파일 디스크립터가 표준 입출력을 가리키거나, 현재 프로세스가 관리하고 있는 파일 디스크립터가 아닌 경우에, 시스템콜은 오류를 반환한다. 만약, 주어진 파일 디스크립터에 파일이 할당되어 있다면, `file_close ()`를 이용하여 해당 파일을 닫고 프로세스가 관리 중인 파일 디스크립터의 개수를 줄이는 과정을 수반한다.

## Discuss
### 1. Reader-Writer Problem

다양한 프로세스의 호출에서 비롯되는 reader-writer 문제를 타개할 방법은 사실 다양하게 존재한다. 에를 들어, 위와 같이 세마포어를 이용하지 않더라도 lock을 이용하여 `read`와 `write`를 제한하여 이 문제를 해결할 수 있다. 하지만, lock을 이용하여 제한할 경우 reader와 writer 모두 동시에 하나만 작동할 수 있기 때문에 전체적인 퍼포먼스에 이슈가 생길 수 있다. 이를 개선하기 위해 세마포어를 이용하여 여러 reader가 동시에 접근할 수 있도록 위와 같은 알고리즘을 채택하여 구현하였다.

### 2. Page Fault
시스템콜 핸들링 중에 페이지 폴트가 발생하면 pintos가 다운되어버리는 문제가 있었다. 하지만, 해당 페이지 폴트는 pintos가 다운되어야 할 문제가 아니며 단순히 핸들링에 실패한 것이기 때문에 `page_fault (struct intr_frame *f)`에서 `sys_exit (-1)`을 호출하도록 수정하였다.


# Denying Writes to Executables

## Implementation

### Data Structures

#### 1. Save the file being used for execution
```c
struct pcb
  {
    // ...
    struct file *file_ex;
    // ...
  };
```
`pcb` 구조체에 `file_ex` 파일 포인터를 추가하여 현재 프로세스가 실행 중인 파일을 기록할 수 있도록 하였다. 다른 프로세스들이 이 포인터를 참조하여 파일에 대한 쓰기가 가능한지 확인할 것이다.

### Algorithms

#### 1. On Process Start
```c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  // ...
  t->pcb->file_ex = NULL;
  // ...
}
```

`thread_create ()`를 통해 스레드가 생성될 때, 이 스레드의 `pcb` 에서 `file_ex`가 `NULL`로 초기화 될 수 있도록 설정해주었다. 이러한 초기화 과정을 통해 쓰레기 값을 없애고 차후 파일을 가지고 있는지 그 여부를 판단하는 과정을 편리하게 만들 수 있었다.

#### 2. On Executable Load
```c 
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  // ...
  t->pcb->file_ex = file;
  // ...
}
```
프로세스가 실행되고 해당 프로세스의 실행 파일을 불러올 때 `file_ex` 필드를 현재 실행하고자 하는 파일을 가리킬 수 있도록 설정해주어야 한다. 그렇기 때문에, 우선 주어진 `file_name`의 파일을 여는데 성공했다면, `file_ex`를 해당 파일로 설정해주는 과정을 추가하였다.

#### 3. On Open
```c 
int 
sys_open (const char *file)
{
  // ...
  if (thread_current ()->pcb->file_ex && (strcmp (thread_current ()->name, file) == 0))
    file_deny_write (file_);
  // ...
}
```
 현재 실행 중인 파일에 대한 쓰기를 막는 과정은 이 알고리즘을 통해 실현된다. `open` 시스템콜이 호출되었을 때, 현재 프로세스의 이름과 현재 프로세스가 실행 중인 파일의 이름이 같다면 `file_deny_write ()`를 통해 파일에 대한 쓰기를 막는다. 이것은 Pintos에서 현재 프로세스의 이름이 현재 실행 중인 파일의 이름으로 지정되어 있다는 점을 이용한 것이다.
 
 
## Discuss
### 1. Deny on init, Allow on exit.

 현재 이 프로젝트는 `open` 시스템콜이 불렸을 때 그제서야 파일에 대한 쓰기를 금지시키는 다소 lazy한 방식으로 구현되어 있다. 이러한 구현 방식 외에, 기존 디자인 과정에서 서술하였던 것 처럼, 프로세스가 시작될 때에 로드한 파일에 대한 쓰기를 금지시키고, 종료될 때 로드한 파일에 대한 쓰기를 다시 허용시키는 식으로 구현할 수도 있다. 하지만, `open` 과정에서 쓰기 가능 여부를 확인하는 것이 더 강력하게 쓰기를 제한할 수 있는 방법이라고 판단하여서, 위와 같이 구현하였다. 
 
# Result
![](https://i.imgur.com/EcKOUG1.png)
위와 같은 구현을 통해 주어진 환경의 서버에서 모든 테스트를 통과한 것을 확인할 수 있다.