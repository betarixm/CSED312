Lab 1: Threads
===
김민석 (20180174), 권민재 (20190084) `CSED312`

# Alarm Clock

## Implementation

### Data Structures

#### 1. Sleep List

```c
static struct list sleep_list; 
```
각 스레드마다 지정된 시간에 대해서, 현재 block된 상태로 자고 있는 스레드들을 관리하기 위한 리스트를 추가했다.


#### 2. Awake Tick

```c
struct thread
  {
    // ...
    int64_t awake_tick;
    // ...
  };
```
스레드가 일어나야할 틱을 저장하기 위해, 스레드 구조체에 해당 값을 저장할 수 있는 필드를 추가한다. 다른 틱들의 자료형을 고려하여, `int64_t` 로 선언하였으며, 일반성을 잃지 않기 위해서 `magic` 보다 앞에서 선언될 수 있도록 하였다.


### Algorithms

#### 1. Init thread system

```c 
void
thread_init (void) 
{
  // ...
  list_init (&sleep_list);
  // ...
}
```
스레드 시스템이 초기화될 때, 다른 리스트가 초기화되는 곳을 고려하여 `sleep_list` 또한 리스트로서 역할을 할 수 있도록 `list_init()`을 이용하여 초기화해주었다.


#### 2. Sleep thread

```c
void
thread_sleep (int64_t ticks)
{
  enum intr_level old_level = intr_disable ();
  struct thread *t = thread_current ();

  ASSERT (is_thread (t));
  ASSERT (t != idle_thread);

  t->awake_tick = ticks;

  list_insert_ordered (&sleep_list, &t->elem, compare_thread_awake_tick, 0); 

  thread_block ();

  intr_set_level (old_level);
}
```
서로 다른 스레드에서 동시에 sleep을 요청하여 레이스컨디션이 발생하는 것을 막기 위하여 인터럽트가 disable 된 영역 사이에서 로직이 작동할 수 있도록 작업하였다. 현재 스레드가 일어나야할 틱을 인자로 주어진 틱으로 설정하고, `list_insert_ordered()` 를 이용하여 `sleep_list`에 일어나야할 틱이 작은 순서대로 삽입될 수 있도록 구현하였다. 앞선 디자인 과정에서 누락되었던 `compare_thread_awake_tick()` 함수를 이용하여 이 과정이 실현된다. 이후 `thread_block()`을 이용하여 현재 실행 중인 스레드를 block 시켜서 awake 하기 전까지는 준비된 상태로 전이하지 않도록 만들어주었다.

```c
void
timer_sleep (int64_t ticks) 
{
  // ...
  // Busy waiting 로직 삭제
  thread_sleep(start + ticks)
}
```
이후, 기존 `timer_sleep()`에서 앞서 구현한 `thread_sleep()`을 호출하여 특정 틱까지 해당 스레드를 잠재울 수 있도록 구현하였다.


#### 3. Awake thread

```c 
void
thread_awake (int64_t ticks)
{
  struct list_elem *elem_t;
  struct thread *t;

  for (elem_t = list_begin (&sleep_list); elem_t != list_end (&sleep_list);) {
    t = list_entry (elem_t, struct thread, elem);

    if (t->awake_tick > ticks) { break; }

    elem_t = list_remove (elem_t);
    thread_unblock (t);
  }
}
```
`thread_awake()`는 타이머 인터럽트마다 호출되어 깨어나야할 스레드가 있을 때 해당 스레드를 unblocking 해줄 수 있도록 구현되었다. 매 인터럽트마다 `sleep_list ()`를 순회하면서 현재 틱보다 일어나야할 틱이 작은 스레드가 있다면 해당 스레드를 unblocking 하고 `sleep_list`에서 해당 스레드를 삭제한다. `awake_tick`이 중간에 바뀌는 경우가 없으므로, 별다른 정렬을 해줄 필요가 없다. 또한, 현재 `sleep_list`는 `awake_tick`을 기준으로 정렬되어 있기 때문에, 리스트의 모든 원소를 순회할 필요도 없다.

``` c 
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  // ...
  thread_awake(ticks);
}
```
앞서 언급한 것 처럼, 매 인터럽트마다 일어나야할 스레드들을 깨울 수 있도록, `timer_interrupt`에서 `thread_awake()`를 호출한다.


## Discussion 
### 1. `compare_thread_awake_tick()` 추가
```c
bool 
compare_thread_awake_tick (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    return list_entry (a, struct thread, elem)->awake_tick
      < list_entry (b, struct thread, elem)->awake_tick;
}
```

기존 디자인 리포트에서는 `list_insert_ordered()`를 위한 `awake_tick` 비교 함수가 누락되어 있었다. `thread_sleep()`에서 `sleep_list`에 스레드를 삽입할 수 있도록, 위와 같은 비교 함수를 추가로 구현하였다.

### 2. `thread_block()`을 이용하지 않는 구현
```
/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
```

현재 구현에서는 스레드를 block 시키기 위해 `thread_block()`을 이용하고 있지만, `thread_block`의 주석을 읽어보면 이 함수를 호출하는 것보다는 synchronization primitives를 이용하는 것을 권장하고 있다. 그렇기에, 세마포어나 락을 이용하여 스레드 blocking을 관리하는 방법도 생각해볼 수 있을 것 같다.


# Priority Scheduling

## Implementation
### Data Structures

#### 1. Priority Donation

```c
struct thread
  {
    // ...
    int init_priority;
    struct lock *released_lock;
    struct list dontations;
    struct list_elem donation_elem;
    // ...
  };
```

|      이름       | 내용                                                    |
|:---------------:|:------------------------------------------------------- |
| `init_priority` | 스레드의 초기 우선 순위 값                              |
| `released_lock` | 스레드가 현재 가지려고 하는 `lock`                        |
|   `donations`   | 다중 donation 내역을 기록하기 위한 `list`               |
| `donation_elem` | 다중 donation 내역을 기록하기 위한 `list`의 `list_elem` |


### Algorithms

#### 1. Compare thread priority

```c 
bool 
compare_thread_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    return list_entry (a, struct thread, elem)->priority
      > list_entry (b, struct thread, elem)->priority;
}
```
`ready_list` 등의 `list`에 스레드를 우선 순위에 따라서 삽입시키기 위해서, 우선 순위가 높은 순서대로 정렬시킬 수 있는 정렬자를 구현하였다.


#### 2. Preempt Thread

```c 
void
thread_preempt (void)
{
  if (list_empty (&ready_list)) { return; }

  struct thread *t = thread_current ();
  struct thread *ready_list_t = list_entry (
    list_front (&ready_list), struct thread, elem
  );

  if (t->priority < ready_list_t->priority) {
    thread_yield (); 
  }
}
```

현재 실행 중인 스레드와 `ready_list`의 첫번째 스레드의 우선 순위를 비교하여, 그 우선 순위에 따라서 thread를 yield할 수 있도록 구현하였다. `ready_list`가 비어있을 때에는 함수를 바로 종료할 수 있도록 하였다. 다만, 이 함수는 별도의 정렬 로직을 가지고 있지 않기 때문에, 이 함수가 호출되기 전에 비교하고자 하는 모든 대상 스레드가 `ready_list`에 정렬된 채로 삽입되어 있는지 확인하여야 한다.


##### 2.1. When Creating Thread

```c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  // ...
  thread_preempt();
  
  return tid;
}
```

스레드가 만들어졌을 때는 전체 스레드 풀에서 우선 순위들이 변경되는 시점 중 하나이다. 그렇기 때문에, 앞서 만들어둔 `thread_preempt()`를 호출하여 양보 로직이 실행될 수 있도록 해야 한다. 앞서 호출되는 `thread_unblock()`에서 새로 만들어지는 스레드가 `ready_list`에 삽입됨을 양지하자.


##### 2.2. When Priority Modified

```c 
void
thread_set_priority (int new_priority) 
{
  // ...
  thread_preepmt();
}
```
스레드의 우선 순위가 변경되었을 때 또한 선점이 일어날 수 있는 상황이기 때문에, `thread_preepmt()`를 이용하여 선점 여부를 확인하고 수행할 수 있어야 한다. 다만, 이 함수는 현재 실행중인 스레드의 우선 순위를 변경하는 것이기 때문에, `ready_list`를 정렬할 필요는 없다.


#### 3. `ready_list` Insertion
```c 
void
thread_unblock (struct thread *t) 
{
  // ...
  list_insert_ordered (&ready_list, &t->elem, compare_thread_priority, NULL);

  // ...
}
```
```c
void
thread_yield (void) 
{
  // ...
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, compare_thread_priority, NULL);
  // ...
}
```

`ready_list`를 변경하는 함수들에서 `thread_compare_priority`와 `list_insert_ordered`를 이용하여 우선 순위 순서대로 스레드가 삽입되게 할 수 있도록 만들었다.


#### 4. Semaphore

##### 4.1. Down

```c
void
sema_down (struct semaphore *sema) 
{
  // ...
  while (sema->value == 0) 
    {
      list_insert_ordered (&sema->waiters, &thread_current ()->elem, compare_thread_priority, NULL);
      thread_block ();
    }
  // ...
}
```
`waiters`에 각 스레드들이 우선 순위 순서대로 삽입될 수 있도록 `list_insert_ordered`와 `compare_thread_priority`를 이용하여 구현하였다. 


##### 4.2. Up

```c
void
sema_up (struct semaphore *sema) 
{
  // ...
  if (!list_empty (&sema->waiters)) {
    list_sort (&sema->waiters, compare_thread_priority, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }
  // ...
  thread_preepmt ();
  intr_set_level (old_level);
}
```

`waiters` 중에서 가장 앞에 있는 스레드를 unblocking 하기 이전에, 우선 순위들이 변경되었을 수 있기 때문에 `waiters`를 재정렬해주었다. 또한, 이 unblock에 따라 선점이 발생해야할 수 있기 때문에, 함수 종료 직전에 `thread_preepmt()`를 호출해주었다.


##### 4.3 Compare Priority of Semaphore

```c
int
sema_waiters_head_thread_priority (struct semaphore *sema)
{
  if (list_empty (&(sema->waiters))) { return PRI_MIN - 1; }
  return list_entry (list_front (&(sema->waiters)), struct thread, elem)->priority;
}

bool
compare_sema_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  return (sema_waiters_head_thread_priority (&(list_entry (a, struct semaphore_elem, elem)->semaphore)))
    > (sema_waiters_head_thread_priority (&(list_entry (b, struct semaphore_elem, elem)->semaphore)));
}
```
각 세마포어에 대해서 `list_insert_ordered`를 이용하기 위한 비교자를 구현하였다. 이 함수가 호출되기 이전에 `waiters`가 정렬되어있어야 함을 양지해야한다.


#### 5. Condition Variable


##### 5.1 cond_wait 

```c 
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  // ...
  list_insert_ordered (&cond->waiters, &waiter.elem, compare_sema_priority, NULL);
  // ...
}
```
`condition`의 세마포어들이 `list_insert_ordered()`와 `compare_sema_priority()`를 통해 정렬된 상태로 삽입될 수 있도록 구현하였다.


##### 5.2 cond_signal 

```c 
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  // ...
  if (!list_empty (&cond->waiters)) {
     list_sort (&cond->waiters, compare_sema_priority, NULL);
     // ...
  }
}
```
시그널을 받았을 때, 이전에 삽입할 때와 우선 순위가 달라졌을 수 있기 때문에 `list_sort`를 통해 정렬하는 과정을 추가하였다. 

#### 6. Priority Donation

```c
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ...
  t->init_priority = priority;
  t->released_lock = NULL;

  list_init (&t->donations);
  ...
}
```
*Data Structures*란에서 언급한 리스트와 변수를 통해 priority donation 정보를 관리한다.

##### 6.1 lock_aquire

**T** = 현재 스레드
**L** = 획득하고자 하는 `lock`
```flow
st=>start: T->lock_aquire(L)
cond1=>condition: L->holder
op1=>operation: T->released_lock = L
op2=>operation: L->holder->donations <- T
op3=>operation: T가 L->holder에게 priority donation함
op4=>operation: L->holder->release_lock()
op6=>operation: T->released_lock = NULL
opLH=>operation: L->holder = T
e=>end: L 획득!

st->cond1
op1(bottom)->op2(bottom)->op3(bottom)->op4(bottom)->op6(left)->opLH
opLH->e
cond1(yes@true, right)->op1
cond1(no@false)->opLH

```

```c
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (thread_mlfqs) 
  {
    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
    
    return;
  }

  struct thread *t = thread_current ();

  printf("acquire tid = %d\n", t->tid);

  if (lock->holder) {
    t->released_lock = lock;
    list_push_back (&lock->holder->donations, &t->donation_elem);
    donate_priority ();
  }

  sema_down (&lock->semaphore);

  t->released_lock = NULL;
  lock->holder = t;
}
```
**T**의 우선 순위가 변경 되었을 때 **T**에게 우선순위를 기부 받은 스레드들의 우선순위도 갱신하기 위해 `donations` 리스트에 T에게 기부 받은 스레드를 기록한다.

###### 6.1.1 donate_priority
```c
void
donate_priority (void)
{
  int depth = 0;
  struct thread *t = thread_current ();
  
  for (depth = 0; t->released_lock != NULL && depth < DONATION_DEPTH_MAX; ++depth, t = t->released_lock->holder) {
    if (t->released_lock->holder->priority < t->priority) {
      t->released_lock->holder->priority = t->priority;
    }
  }
}
```
T의 우선순위를 T가 대기중인 `lock`(L)의 `holder`(h)에게 기부한다. 즉, h가 대기중인 `lock`(**L'**)의 `holder`(h')가 존재하면 h의 우선순위를 h'에게 기부해야한다. 위를 깊이 8까지 진행해 *nested donation*을 구현한다.


##### 6.2 lock_release
```c
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;

  if (!thread_mlfqs) 
  {
    remove_threads_from_donations (lock);
    update_priority ();
  }
  
  sema_up (&lock->semaphore);
}
```

###### 6.2.1 remove_thread_from_donation
```c
void
remove_threads_from_donations (struct lock *lock)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&t->donations); e != list_end (&t->donations);) {
    if(list_entry (e, struct thread, donation_elem)->released_lock == lock) {
      e = list_remove (e);
    } else {
      e = list_next (e);
    }
  }
}
```
**L**을 담보로 기부 받은 우선 순위를 철회하기 위해 `donations`에서 **L**을 기다리는 스레드를 제거한다.

###### 6.2.2 update_priorty
```c
void
update_priority (void)
{
  struct thread *t = thread_current ();

  int target_priority = t->init_priority;
  int test_priority;

  if (!list_empty (&t->donations)) {
    test_priority = list_entry (list_max(&t->donations, compare_thread_priority, NULL), struct thread, donation_elem)->priority;
    target_priority = (test_priority > target_priority) ? test_priority : target_priority;
  }

  t->priority = target_priority;
}
```
`donations` 리스트에 변동이 있을 때, priority donation 받을 값을 갱신 한다.

```c
/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  ...
  update_priority ();
  thread_preepmt ();
}
```
**T**의 우선 순위가 변경 되었을 때도 호출해 새로운 우선 순위와 기부 받은 우선 순위 중 높은 우선 순위를 취한다.

## Discussion 

### 1. Priority Donation
#### 1.1. 기부 관계 파악
Design report에 기부를 하는 스레드와 받는 스레드 관계 파악에 착오가 있어 반대로 명시한 부분이 있다.

#### 1.2. Data Structure

기존: `released_lock` = 스레드가 `release`한 `lock`
개선: `released_lock` = 스레드가 현재 가지려고 하는(`lock_acquire()`를 호출했지만 `holer`는 아닌) `lock`
현재 스레드가 `lock`을 실제로 획득하기 전 현재 `holder`에게 `lock`을 담보로 우선 순위를 기부하고 `holder`가 `lock_release()`하면서 해당 `lock`을 담보로 받은 기부를 철회하는 방식이다.
Nested donation을 구현하기 위해서 추적해야할 `lock`은 현재 스레드가 획득하려는(`lock_acquire()`를 호출했지만 `holer`는 아닌) `lock`이다.

#### 1.3. Timing
`lock_acquire()`에서 priority donation이 발생하는 타이밍을 잘못 파악해 위와 같은 실수를 범했다. 직접 priority donation을 구현하면서 실수를 깨닫고 기부 절차를 제대로 이해할 수 있었다.

# Advanced Scheduler

## Implementation

### 1. Priority

Priority Scheduling에서 낮은 우선 순위 스레드는 높은 우선 순위의 스레드가 모두 종료될 때까지 실행 될 수 없다. MLFQS는 이런 문제를 해결하기 위해 새로운 우선 순위 계산법을 도입해 일정 기간마다 우선 순위를 갱신한다.

```c
// File: threads/thread.c
/* Calculate MLFQS priority. 
   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
void
mlfqs_priority (struct thread *t)
{
  if (t == idle_thread)
    return; 
  
  int priority = fp_to_int_round (add_fp_int (div_fp_int (t->recent_cpu, -4), PRI_MAX - t->nice * 2));

  if (priority > PRI_MAX) {
    t->priority = PRI_MAX;
  }
  else if (priority < PRI_MIN) {
    t->priority = PRI_MIN;
  } else 
    t->priority = priority;
}
```

```c
/* Recalculate and update all threads' MLFQS priority and recent_cpu. */
void 
mlfqs_update_priority (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_priority (t);
  }

  list_sort (&ready_list, compare_thread_priority, NULL);
}
```

#### 1.1 nice

*스레드가 다른 스레드에게 얼마나 친절한가*를 나타낸다. 스레드는 -20 부터 20까지의 `nice` 값을 가질 수 있으며 `nice` 값이 높을 수록 낮은 우선순위의 스레드에게 CPU를 양보하고, `nice` 값이 낮을 수록 높은 우선순위의 스레드에게 CPU를 뺏어온다.

```c
// File: threads/thread.h
struct thread
  {
    ...
    int nice;
    ...
  }
```

```c
// File: threads/thread.c
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ...
  t->nice = 0;
  ...
}
```

```c
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
    enum intr_level old_level;
    old_level = intr_disable();
    
    struct thread *t = thread_current();
    t->nice = nice;
    mlfqs_priority (t);

    list_sort (&ready_list, compare_thread_priority, NULL);

    if (t != idle_thread)
      thread_preepmt ();
    
    intr_set_level(old_level);
}
```

#### 1.2 recent_cpu
```c
recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
```
최근 한 스레드가 사용한 CPU 시간을 의미한다.

```c
// File: threads/thread.h
struct thread
  {
    ...
    int recent_cpu;
    ...
  }
```

```c
// File: threads/thread.c
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ...
  t->recent_cpu = 0;
  ...
}
```

```c
/* Calculate MLFQS recent_cpu value. 
   recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice */
void
mlfqs_recent_cpu (struct thread *t)
{
  t->recent_cpu = add_fp_int (mult_fp (div_fp (mult_fp_int (load_avg, 2), add_fp_int (mult_fp_int (load_avg, 2), 1)), t->recent_cpu), t->nice);
}
```
```c
/* Recalculate and update all threads' MLFQS priority and recent_cpu. */
void 
mlfqs_update_recent_cpu (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_recent_cpu (t);
  }
}
```
```c
/* Increment recent_cpu value by 1. */
void 
incr_recent_cpu (void)
{
  struct thread *t = thread_current ();
  if (t != idle_thread) 
    t ->recent_cpu = add_fp_int (thread_current ()->recent_cpu, 1);
}
```

#### 1.3 load_avg
```c
// File: threads/threads.c
int load_avg;
```
```c
load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads
```
`load_avg`는 최근 1분간 실행할 준비가 된 스레드의 개수를 나타내며, 시스템 전역 변수로 선언되어 관리된다.

```c
void
thread_start (void)
{
  ...
  load_avg = 0;
  ...
}
```

```c
/* Calculate MLFQS load_avg value.
   load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads */
void
mlfqs_load_avg (void)
{
  int ready_threads = list_size (&ready_list);
  if (thread_current () != idle_thread) 
    ready_threads++;
    
  load_avg = add_fp (mult_fp (div_fp_int (int_to_fp (59), 60), load_avg), 
              mult_fp_int (div_fp_int (int_to_fp (1), 60), ready_threads));
}
```

#### 1.4 Update
```c
// File: devices/timer.c
/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();
  
  if (thread_mlfqs) 
  {
    incr_recent_cpu ();
    if (ticks % TIMER_FREQ == 0)
    {
      mlfqs_load_avg ();
      mlfqs_update_recent_cpu ();
    }
    if (ticks % 4 == 0) 
    {
      mlfqs_update_priority ();
    }
  }

  thread_awake (ticks);
}
```
매 틱 마다 현재 실행 중인 스레드의 `recent_cpu` 값을 1 증가시킨다.
4틱 마다 모든 스레드의 우선 순위를 갱신하고, 1초 마다 모든 스레드의 `recent_cpu` 값을 갱신한다. 일정 주기마다 MLFQS의 요소를 갱신하고, 우선 순위 또한 다시 계산하여 의도한 동작을 수행하도록 구현하였다.

#### 1.5 Fixed Point Arithmetic

Pintos는 부동 소수점 연산을 지원하지 않는다.
17.14 fixed point 연산을 구현해 우선 순위를 계산하도록 구현하였다.

```c
// File: threads/fixed_point.h (new file)
/*
  17.14 fixed point implementation using a 32 bit signed int.
  +---+----------- --------+------ ------+
  |   |           ~        |      ~      |
  +---+----------- --------+------ ------+
  31  30                   13            0
   sign    before point      after point
   bit
*/
...
   
```
(x, y) = fixed point, n = integer, f = 1 in 17.14 fixed point format

| Action                                      |                     Implementation                      |
|:------------------------------------------- |:-------------------------------------------------------:|
| Convert n to fixed point                    |                         `n * f`                         |
| Convert x to integer (rounding toward zero) |                         `x / f`                         |
| Convert x to integer (rounding to nearest)  | `(x + f / 2) / f if x >= 0, (x - f / 2) / f if x <= 0.` |
| Add x and y                                 |                         `x + y`                         |
| Subtract y from x                           |                         `x - y`                         |
| Add x and n                                 |                       `x + n * f`                       |
| Subtract n from x                           |                       `x - n * f`                       |
| Multiply x by y                             |                 `((int64_t) x) * y / f`                 |
| Multiply x by n                             |                         `x * n`                         |
| Divide x by y                               |                 `((int64_t) x) * f / y`                 |
| Divide x by n                               |                         `x / n`                         |

### 2. Synch.

MLFQS에서 우선 순위는 수식으로 인한 갱신 외에 임의로 변경될 수 없다. 또한, priority donation도 발생하지 않아야한다. 그렇기 때문에, MLFQS를 사용할때는 위 priority scheduling에서 구현한 priority donation과 임의로 우선 순위를 변경하는 `set_priority()` 의 실행을 막을 수 있도록 구현하였다. 이는 MLFQS 동작 여부를 저장하는 `bool thread_mlfqs`를 사용해 판단한다.
```c
// File: threads/synch.c
void
lock_acquire (struct lock *lock)
{
  ...
  if (thread_mlfqs) 
  {
    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
    
    return;
  }
  ...
}

void
lock_release (struct lock *lock) 
{
  ...
  if (!thread_mlfqs) 
  {
    remove_threads_from_donations (lock);
    update_priority ();
  }
  ...
}
```

```c
// File: threads/thread.c
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs) 
    return;
  ...
}
```

## Discussion 

### 1. Data Structure
기존 `ready_list`를 우선 순위로 정렬해 사용하면 각 우선 순위마다 `queue`를 생성할 필요가 없었다. Priority가 변경 될때마다 `ready_list`를 정렬하여 MLFQS를 구현했다.

### 2. Timing
각 요소의 갱신 주기와 갱신 조건을 정교하게 조정해야 안정적으로 모든 test를 통과할 수 있었다.

# Result

![](https://i.imgur.com/PkrOgdj.png)

위 구현 내용을 통해 Grading server에서 Lab1의 27개 테스트를 모두 통과했다.
