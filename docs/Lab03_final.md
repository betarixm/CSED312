Lab 3: Virtual Memory
===
김민석 (20180174), 권민재 (20190084) `CSED312`


# Frame Table

## Implementation

### Data Structures

#### 1. Frame Table Entry
```c
// File: vm/frame.h
struct fte {
    void *kpage;
    void *upage;

    struct thread *t;

    struct list_elem list_elem;
};
```
구조체 `fte`는 frame table의 각 entry로 삽입되며, frame 하나 하나로 여겨질 수 있도록 구현되었다. `kpage`에는 kernel 가상 page, `upage`에는 user 가상 page 정보가 저장된다. `t`는 `fte`의 주인 스레드를 가리키며, `fte`는 `frame_table`이라는 list에 삽입해 관리했기 때문에 `list_elem`을 통해 list에 접근했다.

#### 2. Frame Table
```c
static struct list frame_table;
```
`frame_table`은 `fte`로 구성된 list로, frame table을 이루는 주체이다.

#### 3. Lock for Frame Table
```c 
static struct lock frame_lock;
```
여러 프로세스에서 동시 다발적으로 `frame_table`에 접근할 경우 synchronize가 어그러질 수 있으므로 lock을 사용해 critical section을 구획하여 보호할 수 있도록 구현하였다.

#### 4. `clock_cursor`
```c 
static struct fte *clock_cursor;
```
Free 상태인 frame이 없을 경우 evict할 frame을 찾기 위한 필드이다. 이후 swapping을 설명하는 부분에서 더 자세한 내용을 다룰 것이다.

### Algorithms

#### 1. `frame_init ()`
```c
// File: vm/frame.c
void
frame_init() {
    list_init(&frame_table);
    lock_init(&frame_lock);
    clock_cursor = NULL;
}
```
```c
// File: threads/init.c
int
main (void) {
    // ...
    frame_init();
    // ...
}
```

Frame table을 관리하기 위한 여러 자료 구조들은 `frame_init ()`을 통해 초기화된다. `frame_table`은 `list_init ()`을 통해 초기화되며, `frame_lock`은 `lock_init`을 통해 초기화된다. 마지막으로 `clock_cursor`를 `NULL`으로 초기화하며 모든 초기화 과정을 마무리했다. `frame_init ()`은 thread 시스템이 초기화되는 `main ()`에서 호출된다.

#### 2. `falloc_get_page ()`
```c
// File: vm/frame.c
void *
falloc_get_page(enum palloc_flags flags, void *upage) {
    struct fte *e;
    void *kpage;
    lock_acquire(&frame_lock);
    kpage = palloc_get_page(flags);
    if (kpage == NULL) {
        evict_page();
        kpage = palloc_get_page(flags);
        if (kpage == NULL)
            return NULL;
    }

    e = (struct fte *) malloc(sizeof *e);
    e->kpage = kpage;
    e->upage = upage;
    e->t = thread_current();
    list_push_back(&frame_table, &e->list_elem);

    lock_release(&frame_lock);
    return kpage;
}
```
`falloc_get_page ()`는 새로운 frame을 `fte`의 형태로 할당하는 역할을 수행한다. `palloc_get_page ()`를 이용하여 `upage`에 대응하는 `kpage`를 할당하고 새로운 `fte`, 즉 frame 을 생성해 `frame_table`에 추가한다. 이때, frame table에 여러 프로세스가 동시 다발적으로 참조하는 것을 방지하기 위해 `frame_lock`으로 `frame_table`에 한번에 한 프로세스만 접근할 수 있도록 구현하였다.
> 새로운 `kpage`를 할당할 수 없을 때 실행되는 eviction 절차인 `evict_page ()`은 swapping을 설명할 때 살펴볼 예정이다.

#### 3. `falloc_free_page ()`
```c
void
falloc_free_page(void *kpage) {
    struct fte *e;
    lock_acquire(&frame_lock);
    e = get_fte(kpage);
    if (e == NULL)
        sys_exit(-1);

    list_remove(&e->list_elem);
    palloc_free_page(e->kpage);
    pagedir_clear_page(e->t->pagedir, e->upage);
    free(e);

    lock_release(&frame_lock);
}
```
`falloc_free_page ()`는 frame table에 할당된 프레임을 할당 해제하는 역할을 수행한다. 이 함수는 `kpage`을 인자로 받아서 그에 상응하는 `fte`, 즉 frame을 할당 해제하는 방식으로 구현되었다. 이후, `pagedir_clear_page ()`를 호출해 앞으로 `kpage`에 해당했던 `upage`로의 접근은 fault가 발생하도록 구현하였다. 참고로, 이 과정에서 frame table을 보호하기 위해 `frame_lock`을 이용하였다.

#### 4. `get_fte ()`
```c
struct fte *
get_fte(void *kpage) {
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
        if (list_entry(e, struct fte, list_elem)->kpage == kpage)
    return list_entry(e, struct fte, list_elem);
    return NULL;
}
```
`get_fte`는 간단한 frame 조회 함수이다. 이 함수는 `frame_table` 리스트를 순회하면서 주어진 `kpage`에 해당하는 `fte`를 반환한다.

#### 5. Setup Stack
```c
// File: userprog/process.c
static bool
setup_stack(void **esp) {
    // ...
    kpage = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
    if (kpage != NULL) {
        // ...
        falloc_free_page(kpage);
    }
    // ...
}
```
기존 `setup_stack ()`에서는 `palloc_get_page ()`를 이용하여 페이지를 직접 할당받았다. 하지만 이제 frame table으로 메모리를 관리하기 때문에, 새로 만든 `falloc_get_page ()` 함수를 사용해 frame을 할당하고, 스택 영역을 `frame_table`로 관리할 수 있게 구현하였다.

## Discuss

### 1. Implementing Frame Table with Bitmap

 현재 frame table은 리스트를 이용하여 관리되고 있기 때문에, 각 엔트리를 탐색하거나 수정할 때 퍼포먼스 저하가 발생할 수 있다. 각 entry에 대한 더 빠른 참조를 위해 list가 아닌 bitmap을 이용하여 frame table을 구현하는 방법 또한 존재할 것이다. 이 경우 frame table의 퍼포먼스는 현재 구현보다 더 향상될 수 있다.


# Lazy Loading

## Implementation

### Algorithms

#### 1. Loading Segment
```c 
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
    // ...
    init_file_spte(&thread_current()->spt, upage, file, ofs, page_read_bytes, page_zero_bytes, writable);
    // ...
}
```

 기존에 세그먼트들은 프로세스가 실행될 때 모두 한번에 메모리에 적재되었다. 하지만, lazy loading을 구현하기 위해 `load_segment ()`의 과정에서 메모리에 적재하는 부분을 모두 삭제하고 대신 Supplemental Page Table Entry 만을 남겨서 이후에 PF가 발생하였을 때 해당 entry 정보를 활용하여 페이지를 lazy하게 적재할 수 있도록 구현하였다.

#### 2. Load Page
```c
bool
load_page(struct hash *spt, void *upage) {
    struct spte *e;
    uint32_t *pagedir;
    void *kpage;

    e = get_spte(spt, upage);
    if (e == NULL)
        sys_exit(-1);

    kpage = falloc_get_page(PAL_USER, upage);
    if (kpage == NULL)
        sys_exit(-1);

    bool was_holding_lock = lock_held_by_current_thread(&file_lock);

    switch (e->status) {
        case PAGE_ZERO:
            memset(kpage, 0, PGSIZE);
            break;
        case PAGE_SWAP:
            swap_in(e, kpage);

            break;
        case PAGE_FILE:
            if (!was_holding_lock)
                lock_acquire(&file_lock);

            if (file_read_at(e->file, kpage, e->read_bytes, e->ofs) != e->read_bytes) {
                falloc_free_page(kpage);
                lock_release(&file_lock);
                sys_exit(-1);
            }

            memset(kpage + e->read_bytes, 0, e->zero_bytes);
            if (!was_holding_lock)
                lock_release(&file_lock);

            break;

        default:
            sys_exit(-1);
    }

    pagedir = thread_current()->pagedir;

    if (!pagedir_set_page(pagedir, upage, kpage, e->writable)) {
        falloc_free_page(kpage);
        sys_exit(-1);
    }

    e->kpage = kpage;
    e->status = PAGE_FRAME;

    return true;
}
```
`load_page ()`는 lazy loading이 실현되는데 가장 중요한 로직을 포함하는 함수이다. 이 함수는 PF 핸들러에서 호출되어 실제로 페이지가 lazy하게 로드되는 부분을 실행한다. 이 페이지는 주어진 Supplemental Page Table과 가상 주소를 참조하여 Supplemental Page Table Entry를 찾는다. <u>만약 해당 entry가 `PAGE_ZERO`라면,</u> 단순히 물리 주소를 참조하여 페이지를 0으로 설정하는 것으로 마무리한다. <u>만약 해당 entry가 `PAGE_SWAP`이라면,</u> `swap_in ()`을 호출하여 디스크에 적재되어 있는 페이지를 불러온다. 이 부분은 추후에 더 자세히 살펴볼 예정이다. <u>만약 해당 entry가 `PAGE_FILE`이라면,</u> entry를 참조하여 파일을 읽어서 물리 페이지에 적재한다. 이렇게 물리 페이지에 데이터를 적재하는 작업이 모두 종료되면, 현재 thread에 page directory를 할당하고, `pagedir_set_page ()`를 이용하여 page들을 연결한다. 마지막으로 entry에 물리 페이지 주소 값을 저장하고 상태를 `PAGE_FRAME`으로 지정하여 페이지를 적재하는 과정을 마무리하였다. 참고로, 파일을 다룰 때에는 `file_lock`을 이용하여 여러 프로세스에 대한 synchronize를 맞출 수 있도록 구현하였다.

#### 3. Page Fault Handler
```c
static void page_fault(struct intr_frame *f) {
    // ...
    if (load_page(spt, upage)) {
        return;
    }
    // ...
}
```

Lazy loading을 구현하기 위해 Page Fault를 활용한다. PF는 다양한 이유로 발생할 수 있다. 메모리로의 로드를 나중으로 미뤄둔 페이지를 참조할 때 또한 PF가 발생할 수 있기 때문에, PF를 핸들링하는 과정에서 `load_page ()`를 호출하여 lazy loading이 실현될 수 있도록 구현하였다. 참고로, 스택이 자랄 수 있도록 핸들링하는 과정 또한 PF 핸들러에서 처리하는데, 해당 과정은 lazy loading이 발생하기 전에 핸들링된다. 스택이 자랄 수 있도록 구현한 부분은 이후에 더 자세히 살펴볼 예정이다. 

## Discuss

### 1. Swapping

 Swapping을 page fault handler에서 처리할 수도 있지만, 우리는 swap in 하는 과정이 `load_page ()`의 일부가 될 수 있다고 생각하였다. PF가 발생하였을 때 디스크에서 페이지를 불러오는 과정이나, lazy하게 페이지를 적재하는 과정은 유사하다고 생각하였기 때문이다. 그렇기 때문에 SPTE의 상태 변수로 `PAGE_SWAP`을 추가하였고, 이에 따라 `load_page ()`에서 swapping 과정의 일부를 처리하도록 구현하였다.


### 2. Synchronize

#### 2.1. Lock 도입
기존 구현에서 file system에 2개의 semaphore를 사용해 reader&writer problem을 해결했다. 그러나 page를 load하는 과정에서 2개의 semaphore를 사용하기 번거로움이 있어, 구현의 편의를 위해 `file_lock`이라는 한 개의 lock으로 대체하였다. 더부렁 기존 OPEN system call에서는 sync.를 맞추지 않았으나 이번 구현에서 `sys_open()`에도 `file_lock`을 사용했다.

#### 2.2. Loading Sync.

Pintos에서는 같은 lock에 대해 2번 acquire하는 것을 금지하고 있다. 그렇기 때문에, `was_holding_lock` 변수를 이용하여 현재 프로세스가 이미 파일에 대한 lock을 가지고 있다면 acquire를 요청하지 않고, 그렇지 않다면 acquire를 요청하는 방식으로 중복된 요청을 막았다. 하지만 lock을 확인하는 과정과 실제 lock을 이용 및 요청하는 과정이 멀리 떨어져있기 때문에, 즉 atomic하지 않기 때문에, TOCTOU가 발생하여 시스템이 망가질 수 있다. 이 부분에서 레이스 컨디션이 발생하지 않도록 패치를 수행해야할 필요성이 있다.

# Supplemental Page Table

## Implementation

### Data Structures

#### 1. Supplemental Page Table

```c
struct thread {
    // ...
    struct hash spt;
    // ...
}
```

Lazy loading이나 swapping 등을 구현할 때, 각 페이지에 대한 추가적인 정보가 필요하기 때문에 Supplemental Page Table을 구현하였다. SPT가 각 thread마다 hash table을 이용하여 생성되도록 구현하였다.

#### 2. Supplemental Page Table Entry

```c 
struct spte {
    void *upage;
    void *kpage;

    struct hash_elem hash_elem;

    int status;

    struct file *file;  // File to read.
    off_t ofs;  // File off set.
    uint32_t read_bytes, zero_bytes;  // Bytes to read or to set to zero.
    bool writable;  // whether the page is writable.
    int swap_id;
};
```
|    Field     | Content                                                                                               |
|:------------:|:----------------------------------------------------------------------------------------------------- |
|   `upage`    | 유저 페이지 주소                                                                                      |
|   `kpage`    | 커널 페이지 주소                                                                                      |
| `hash_elem`  | SPT에서 hash로 관리되기 위한 원소 필드                                                                |
|   `status`   | SPTE의 상태를 나타내는 필드로 `PAGE_ZERO`, `PAGE_FRAME`, `PAGE_FILE`, `PAGE_SWAP` 중 하나의 값을 가짐 |
|    `file`    | 페이지에 연관되어 있는 파일의 포인터                                                                  |
|    `ofs`     | `file`을 참조할 오프셋                                                                                |
| `read_bytes` | `file`에서 읽어야 하는 바이트 수                                                                      |
| `zero_bytes` | 0로 설정되어야 하는 바이트 수                                                                         |
|  `writable`  | 페이지 쓰기 가능 여부                                                                                 |
|  `swap_id`   | Swapping 되었을 때 페이지 식별자                                                                      |

### Algorithms

#### 1. Init

##### 1.1. Data Structure

```c 
void
init_spt(struct hash *spt) {
    hash_init(spt, spt_hash_func, spt_less_func, NULL);
}
```

> ```c
> static unsigned
> spt_hash_func(const struct hash_elem *elem, void *aux) {
>     struct spte *p = hash_entry(elem, struct spte, hash_elem);
> 
>     return hash_bytes(&p->upage, sizeof(p->kpage));
> }
> ```

> ```c
> static bool
> spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
>     void *a_upage = hash_entry(a,
>     struct spte, hash_elem)->upage;
>     void *b_upage = hash_entry(b,
>     struct spte, hash_elem)->upage;
> 
>     return a_upage < b_upage;
> }
> ```

`init_spt ()`는 Supplemental Page Table를 초기화시키는 함수이다. `hash_init ()`을 이용하여 주어진 SPT를 초기화시키는데, 이때 `spt_hash_func`과 `spt_less_func`를 참조한다. `spt_hash_func ()`는 `upage` 값을 이용하여 entry를 hash 시키는 함수이며, `spt_less_func ()`는 `upage` 값을 기준으로 entry의 대소를 비교하는 함수이다.

```c
// File: threads/thread.c
tid_t
thread_create(const char *name, int priority, thread_func *function, void *aux) {
    // ...
    init_spt(&t->spt);
    // ...
}
```
SPT는 각 thread마다 존재하기 때문에, `thread_create ()`가 `init_spt ()`를 호출하여 SPT를 초기화시킬 수 있도록 구현하였다.

##### 1.2. Supplemental Page Table Entry

###### 1.2.1. General SPTE

```c
void
init_spte(struct hash *spt, void *upage, void *kpage) {
    struct spte *e;
    e = (struct spte *) malloc(sizeof *e);

    e->upage = upage;
    e->kpage = kpage;

    e->status = PAGE_FRAME;

    hash_insert(spt, &e->hash_elem);
}
```

 일반적인 SPTE는 `init_spte ()`를 통해 할당된다. 이 함수가 호출되면 우선 `spte`에 상응하는 공간을 동적으로 할당받은 이후, 입력받은 인자에 따라 `upage`, `kpage`를 설정하고 `status`를 `PAGE_FRAME`으로 설정한다. 마지막으로, 이 `spte`를 주어진 `spt`에 `hash_insert ()`로 삽입하여 할당 과정을 마무리한다.


##### 1.2.2. Zero SPTE

```c
void
init_zero_spte(struct hash *spt, void *upage) {
    struct spte *e;
    e = (struct spte *) malloc(sizeof *e);

    e->upage = upage;
    e->kpage = NULL;

    e->status = PAGE_ZERO;

    e->file = NULL;
    e->writable = true;

    hash_insert(spt, &e->hash_elem);
}
```

 할당될 때 데이터들이 0으로 채워져야하는 SPTE는 `init_zero_spte ()`를 통해 할당된다. 주로 스택 성장을 위해 새로 할당되는 공간에 프레임을 할당할 때 이용된다. 기본적인 흐름은 `init_spte ()`와 같으나, `kpage`에 `NULL`을 할당하며, `status`로 `PAGE_ZERO`를 설정하며, 쓰기 가능하게 설정한다는 점에서 차이가 있다.


##### 1.2.3. Frame SPTE
```c
void
init_frame_spte(struct hash *spt, void *upage, void *kpage) {
    struct spte *e;
    e = (struct spte *) malloc(sizeof *e);

    e->upage = upage;
    e->kpage = kpage;

    e->status = PAGE_FRAME;

    e->file = NULL;
    e->writable = true;

    hash_insert(spt, &e->hash_elem);
}
```
 일반적인 프레임을 위한 SPTE는 `init_frame_spte ()`를 통해 할당된다. 최초 스택 할당 시에 이용된다. 기본적인 흐름은 `init_spte ()`와 같으나, 연결된 파일이 없다고 표시하고 쓰기 가능하게 설정한다는 점에서 차이가 있다.
 
##### 1.2.4. File SPTE

```c
struct spte *
init_file_spte(struct hash *spt, void *_upage, struct file *_file, off_t _ofs, uint32_t _read_bytes,
               uint32_t _zero_bytes, bool _writable) {
    struct spte *e;

    e = (struct spte *) malloc(sizeof *e);

    e->upage = _upage;
    e->kpage = NULL;

    e->file = _file;
    e->ofs = _ofs;
    e->read_bytes = _read_bytes;
    e->zero_bytes = _zero_bytes;
    e->writable = _writable;

    e->status = PAGE_FILE;

    hash_insert(spt, &e->hash_elem);

    return e;
}
```

 파일과 연관된 SPTE는 `init_file_spte ()`를 통해 할당된다. 파일을 참조할 때 필요한 정보들을 인자로 받으며, 해당 정보들은 그대로 SPTE에 저장된다. 커널 페이지는 별도로 설정되지 않으며, `status`는 `PAGE_FILE`로 설정한다. 세그먼트를 적재할 때나 MMF를 세팅할 때 사용된다.
 
 
#### 2. Search SPTE

```c
struct spte *
get_spte(struct hash *spt, void *upage) {
    struct spte e;
    struct hash_elem *elem;

    e.upage = upage;
    elem = hash_find(spt, &e.hash_elem);

    return elem != NULL ? hash_entry(elem, struct spte, hash_elem) : NULL;
}
```
주어진 SPT와 유저 페이지 주소로부터 SPTE를 반환하는 간단한 조회 함수이다. `hash_find ()`를 이용하기 위해 임시 SPTE를 만들고, 해당 entry의 `upage`를 주어진 `upage`로 설정한 후에 `hash_find ()`를 호출한다. 일련의 과정을 통해 상응하는 SPTE의 주소를 반환하며, 찾지 못했다면 NULL을 반환한다.

#### 3. Delete Page

```c
void
page_delete(struct hash *spt, struct spte *entry) {
    hash_delete(spt, &entry->hash_elem);
    free(entry);
}
```

페이지를 삭제하기 위해 주어진 SPT에서 SPTE를 삭제하는 `page_delete ()`를 구현하였다. `hash_delete ()`를 이용하여 SPT에서 entry를 삭제한 이후 entry를 할당 해제 하는 방식으로 구현되었다. 추후 설명할 `munmap` 과정에서 이용된다.

## Discuss

### 1. Implementing SPT with Hash Table

 각 thread마다 SPT 만들어서 관리할 때 가장 효율적인 방법은 hash table을 이용하는 것이라고 생각하여 `hash`를 이용하여 구현하였다. SPTE 특성 상 수없이 많이 조회될 것이기 때문에 SPT를 리스트로 구현하면 심각한 성능 저하를 겪을 것이라고 추측하였다. 그렇다고 퍼포먼스를 향상시키기 위해 bitmap을 이용하기에는 SPTE가 담아야할 정보가 많다고 생각하였다. 그렇기 때문에 각 thread 마다 존재하는 테이블이라는 점을 이용하여, upage 주소를 기준으로 삼는 hash table을 이용하여 성능과 구현 상의 편의를 모두 잡을 수 있도록 노력하였다.
 
### 2. Delete Page
기존 design report에서 `spt`에서 `spte`를 삭제하는 함수를 언급하지 않았다. Page table에서 page를 삭제할 수 있도록 추가했다.

# Stack Growth

## Implementation

### Data Structures

#### 1. ESP
```c
struct thread {
    void *esp;
}
```

 스택을 동적으로 성장시키기 위해서는 각 thread 마다 스택 포인터를 기록할 필요성이 있다. 그렇기 때문에 `esp` 필드를 추가하여 스택 포인터를 저장할 수 있도록 구현하였다.

### Algorithms

#### 1. Page Fault Handler
```c 
static void
page_fault(struct intr_frame *f) {
    // ...
    upage = pg_round_down(fault_addr);
    if (is_kernel_vaddr(fault_addr) || !not_present)
        sys_exit(-1);

    spt = &thread_current()->spt;
    spe = get_spte(spt, upage);

    esp = user ? f->esp : thread_current()->esp;
    if (esp - 32 <= fault_addr && PHYS_BASE - MAX_STACK_SIZE <= fault_addr) {
        if (!get_spte(spt, upage)) {
            init_zero_spte(spt, upage);
        }
    }
    // ...
}
```

 스택이 성장하는 과정은 `page_fault ()`에서 실현된다. PF가 발생하는 이유는 다양하지만, 스택 공간이 모자랄 경우에도 PF가 발생할 수 있다. 그렇기 때문에, 임의로 설정한 일정 기준에 따라서 스택이 성장해야한다면 PF 핸들러에서 스택을 성장시킬 수 있도록 구현하였다. PF가 발생하였을 때, PF가 발생한 주소와 현재 thread의 `esp`가 32 정도 차이로 유의미하게 가까이 있다면, 0으로 채워진 Supplemental Page Table Entry를 생성하여 스택의 여유 공간으로 할당해줄 수 있도록 구현하였다. 
 
#### 2. Setup Stack

```c 
static bool
setup_stack(void **esp) {
    uint8_t *kpage;
    bool success = false;

    kpage = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
    if (kpage != NULL) {
        success = install_page(((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);

        if (success) {
            init_frame_spte(&thread_current()->spt, PHYS_BASE - PGSIZE, kpage);
            *esp = PHYS_BASE;
        } else {
            falloc_free_page(kpage);
        }      
    }
    
    return success;
}
```

스택을 setup하는 과정도 이전과 차이가 있다. `palloc_get_page`로 페이지를 직접 할당받았던 것을 `falloc_get_page`를 통해 페이지로 할당받도록 수정되었다. 또한, `esp`에 단순히 `PHYS_BASE`를 할당받는 과정에 SPTE를 할당받는 과정도 추가되었다.

## Discuss

### 1. Bug Fix
 기존 구현에서는 디버깅 및 구현의 편의를 위해 `esp`에 `PHYS_BASE - 12`를 할당하였는데, 해당 공간을 추가적으로 확보할 필요가 없기 때문에 `- 12` 부분을 삭제하였다.


# File Memory Mapping

> 💡 MMF는 Memory Mapped File의 약자이다.

## Implementation

### Data Structures

#### 1. MMF
```c 
struct mmf {
    int id;
    struct file *file;
    struct list_elem mmf_list_elem;

    void *upage;
};
```
|      Field      | Content                                                     |
|:---------------:|:----------------------------------------------------------- |
|      `id`       | MMF를 식별하기 위해 부여되는 identifier                     |
|     `file`      | MMF에 상응하는 파일을 관리하기 위한 파일 포인터             |
| `mmf_list_elem` | 각 프로세스의 MMF들을 리스트 형태로 관리하기 위한 elem 필드 |
|     `upage`     | MMF에 상응하는 유저 페이지 주소를 저장하기 위한 필드        |

#### 2. Thread
```c 
struct thread {
    // ...
    struct list mmf_list;
    int mapid;
    // ...
}
```
 각 프로세스는 mmap을 통해 MMF를 할당받기 때문에, MMF들을 각 프로세스 별로 관리할 수 있도록 `thread` 구조체에 `mmf_list` 필드를 추가해주었다. 또한, 현재 할당되어 있는 MMF 파일들에게 id를 할당하고 관리할 수 있도록 `mapid`를 추가해주었다. `mapid`는 현재 이 프로세스에 할당된 MMF들의 개수를 나타낸다. 


### Algorithms

#### 1. Init
```c
// File: threads/thread.c
tid_t
thread_create(const char *name, int priority, thread_func *function, void *aux) {
    // ...
    list_init(&t->mmf_list);
    t->mapid = 0;
    // ...
}
```
MMF들을 관리하기 위한 자료 구조는 `thread_create ()`가 호출될 때 초기화된다. `list_init ()`을 통해 새로 생성되는 thread의 `mmf_list`를 초기화해주고, `mapid`를 0으로 설정한다.

```c 
struct mmf *
init_mmf(int id, struct file *file, void *upage) {
    struct mmf *mmf = (struct mmf *) malloc(sizeof *mmf);

    mmf->id = id;
    mmf->file = file;
    mmf->upage = upage;

    off_t ofs;
    int size = file_length(file);
    struct hash *spt = &thread_current()->spt;

    for (ofs = 0; ofs < size; ofs += PGSIZE)
        if (get_spte(spt, upage + ofs))
            return NULL;

    for (ofs = 0; ofs < size; ofs += PGSIZE) {
        uint32_t read_bytes = ofs + PGSIZE < size ? PGSIZE : size - ofs;
        init_file_spte(spt, upage, file, ofs, read_bytes, PGSIZE - read_bytes, true);
        upage += PGSIZE;
    }

    list_push_back(&thread_current()->mmf_list, &mmf->mmf_list_elem);

    return mmf;
}
```
각 MMF들은 `init_mmf ()`를 통해 초기화된다. 일단 `init_mmf ()`가 호출되면 `mmf` 구조체를 위한 공간을 `malloc`을 통해 할당받는다. 이후, 주어진 인자를 참조하여 `mmf`의 각 값을 설정한다. MMF는 파일을 수반하는 페이지이기 때문에, 이에 따른 Supplemental Page Table Entry를 생성하는 과정 또한 필요하다. 그렇기 때문에, `mmf`를 적절히 설정한 이후에는 오프셋과 읽어야 하는 바이트 값을 구해서 `init_file_spte ()`로 SPTE를 생성하는 과정을 추가적으로 구현하였다. `mmf`와 SPTE를 생성하는 과정이 모두 종료되면, 현재 thread의 `mmf_list`에 생성된 `mmf`를 추가하고, `mmf`의 주소를 반환하여 MMF를 초기화하는 과정을 마무리하도록 구현하였다.


#### 2. Get MMF
```c 
struct mmf *
get_mmf(int mapid) {
    struct list *list = &thread_current()->mmf_list;
    struct list_elem *e;

    for (e = list_begin(list); e != list_end(list); e = list_next(e)) {
        struct mmf *f = list_entry(e, struct mmf, mmf_list_elem);

        if (f->id == mapid)
            return f;
    }

    return NULL;
}
```

MMF의 식별자를 바탕으로 각 thread에서 MMF를 조회하는 기능이 필요하기 때문에, `get_mmf ()`를 구현하였다. `get_mmf ()`는 현재 thread의 `mmf_list`를 순회하면서 인자로 주어진 `mapid`와 동일한 id를 가지고 있는 MMF가 있다면 해당 MMF를 반환하고, 아니라면 NULL을 반환하는 식으로 구현되었다.


#### 3. System Call Handler
```c 
int
sys_mmap(int fd, void *addr) {
    struct thread *t = thread_current();
    struct file *f = t->pcb->fd_table[fd];
    struct file *opened_f;
    struct mmf *mmf;

    if (f == NULL)
        return -1;

    if (addr == NULL || (int) addr % PGSIZE != 0)
        return -1;

    lock_acquire(&file_lock);

    opened_f = file_reopen(f);
    if (opened_f == NULL) {
        lock_release(&file_lock);
        return -1;
    }

    mmf = init_mmf(t->mapid++, opened_f, addr);
    if (mmf == NULL) {
        lock_release(&file_lock);
        return -1;
    }

    lock_release(&file_lock);

    return mmf->id;
}
```
`sys_mmap ()`은 앞서 MMF를 할당하는 과정을 시스템콜의 형태로 이용할 수 있도록 시스템콜화 시킨 함수이다. 이 시스템콜은 `fd`와 주소를 입력받아 파일을 메모리에 매핑시킬 수 있도록 구현되어야 한다. 이를 위해 `fd`와 `fd_table`을 참조하여 연결된 파일을 구하고 해당 파일과 주어진 주소로 `init_mmf ()`를 호출하여 파일이 메모리에 mapping될 수 있도록 구현하였다. 이때, 파일이 존재하지 않거나 주소가 유효하지 않다면 오류가 발생했음을 알리도록 구현했다. 또한, 파일을 찾는데 실패하거나 MMF를 생성하는데 실패했을 때에도 오류를 반환하도록 하였다. 참고로, `mmap`은 여러 프로세스에 의해 동시다발적으로 호출되어 충돌이 발생할 수 있기 때문에 이 과정을 `file_lock`으로 보호할 수 있도록 구현하였다.

```c
int
sys_munmap(int mapid) {
    struct thread *t = thread_current();
    struct list_elem *e;
    struct mmf *mmf;
    void *upage;

    if (mapid >= t->mapid)
        return;

    for (e = list_begin(&t->mmf_list); e != list_end(&t->mmf_list); e = list_next(e)) {
        mmf = list_entry(e, struct mmf, mmf_list_elem);
        if (mmf->id == mapid)
            break;
    }
    if (e == list_end(&t->mmf_list))
        return;

    upage = mmf->upage;

    lock_acquire(&file_lock);

    off_t ofs;
    for (ofs = 0; ofs < file_length(mmf->file); ofs += PGSIZE) {
        struct spte *entry = get_spte(&t->spt, upage);
        if (pagedir_is_dirty(t->pagedir, upage)) {
            void *kpage = pagedir_get_page(t->pagedir, upage);
            file_write_at(entry->file, kpage, entry->read_bytes, entry->ofs);
        }
        page_delete(&t->spt, entry);
        upage += PGSIZE;
    }
    list_remove(e);

    lock_release(&file_lock);
}
```
`sys_munmap ()`은 MMF를 해제하는 과정을 시스템콜의 형태로 이용할 수 있도록 구현한 함수이다. 이 함수는 `mapid`를 입력받아 해당 식별자에 상응하는 MMF를 할당 해제하는 역할을 수행한다. 이 함수가 호출되면 우선 `mapid`에 상응하는 `mmf`를 찾는다. 이후, `mmf`에 연결된 파일 페이지를 순회하면서 `page_delete ()`를 이용하여 페이지를 삭제하는 과정을 반복한다. 만약 해당 페이지가 dirty하다면 페이지를 디스크에 쓰는 과정 또한 수행한다. 이후, `mmf`를 `mmf_list`에서 삭제하여 `munmap`을 마무리한다. 만약 mmf를 찾는데 실패했을 경우에는 별다른 조치 없이 함수를 바로 종료시키도록 구현하였다. 참고로, `munmap`은 여러 프로세스에 의해 동시다발적으로 호출되어 충돌이 발생할 수 있기 때문에 파일에 연관된 과정들을 `file_lock`으로 보호할 수 있도록 구현하였다.

```c 
static void
syscall_handler(struct intr_frame *f) {
    // ...
    case SYS_MMAP:
        get_argument(f->esp, &argv[0], 2);
        f->eax = sys_mmap((int) argv[0], (void *) argv[1]);
        break;
    case SYS_MUNMAP:
        get_argument(f->esp, &argv[0], 1);
        sys_munmap((int) argv[0]);
        break;
    // ...
}
```

앞서 구현한 `sys_mmap ()`과 `sys_munmap ()`은 `syscall_handler ()`를 통해 실행된다. `syscall_handler ()`는 mmap과 munmap에 상응하는 시스템콜 번호에 따라 `get_argument ()`로 인자를 받아온 후 `sys_mmap ()`이나 `sys_munmap ()`을 호출하여 각 시스템콜이 정상적으로 동작할 수 있도록 구현되었다.


## Discuss

### 1. Dirty Page

 Design report에서는 초기에 dirty한 file-backed page를 프로세스가 종료될 때 디스크에 기록하는 방식으로 구현하고자 설계하였다. 하지만, munmap 과정에서 dirty 페이지를 핸들링할 필요성이 있었다. 그래서, dirty page를 프로세스가 종료될 때 관리하는 대신 dirty page를 `sys_munmap ()`에서 관리하도록 구현하였고, 프로세스가 종료될 때는 MMF들에 대해서 `sys_munmap ()`을 호출하도록 수정하였다.
 
### 2. Synchronization
`sys_mmap()`과 `sys_munmap()`의 경우 file system에 접근하기 때문에 `file_lock`을 통해 sync.를 맞췄다.


# Swap Table

## Implementation

### Data Structures

#### 1. Swap Table
```c
static struct bitmap *swap_valid_table;
```
 Swapping을 할 때마다 디스크에 질의하여 swap할 공간을 찾는 것은 매우 비효율적이다. 그렇기 때문에, 디스크의 swap 공간을 추상화하여 운영체제 레벨에서 swapping 공간을 관리하는 추상화된 자료 구조가 필요하다. 이를 위해 `swap_valid_table`를 비트맵 형식으로 선언하였다. 해당 테이블은 0과 1의 값들로 구성되며, 1은 해당 블록으로 swap out 할 수 있음을 뜻한다.


#### 2. Swap Block
```c
static struct block *swap_disk;
```
 `swap_valid_table`을 이용하여 효율적으로 swapping 할 공간을 관리한다고는 하지만, 실제 구현은 디스크, 즉 블록에 대한 read 및 write 과정으로 일어나기 때문에 해당 블록을 포인터 형태로 저장해놓아야할 필요성이 있다. 이를 위해, 블록 포인터 형식의 `swap_disk`를 선언하여 실제 읽기 및 쓰기 작업을 수행할 수 있도록 하였다.
 
#### 3. Swap Lock
```c
static struct lock swap_lock;
```
 Swapping은 여러 프로세스에 대해 동시다발적으로 일어날 수 있기 때문에, `swap_valid_table`을 보호하기 위한 lock이 필요하다. 이를 위해 `swap_lock`을 선언하였다. 
 
### Algorithms

#### 1. Init
```c 
void init_swap_valid_table() {
    swap_disk = block_get_role(BLOCK_SWAP);
    swap_valid_table = bitmap_create(block_size(swap_disk) / SECTOR_NUM);

    bitmap_set_all(swap_valid_table, true);
    lock_init(&swap_lock);
}
```
Swapping을 위해 선언한 많은 자료 구조들은 `init_swap_valid_table ()`을 통해 그 값들이 초기화된다. 우선, `block_get_role(BLOCK_SWAP)` 을 통해 pintos에 물려있는 여러 블록들 중에서 swapping을 위해 할당된 블록을 `swap_disk`에 저장한다. 이후, `bitmap_create ()`를 이용하여 bitmap을 형성한다. 이때, 그 사이즈는 `swap_disk`의 크기를 섹터의 개수로 나눈 값을 택한다. Swap table이 처음 만들어졌을 때에는 모든 슬롯들에 swap out할 수 있는 상태이기 때문에, 모든 값을 `true`, 즉 1으로 설정하다. 마지막으로, `lock_init ()`을 통해 `swap_lock`을 초기화하여 모든 초기화 과정을 마친다.

```c 
// File: threads/init.c
int main(void) {
    // ...
    init_swap_valid_table();
    // ...
}
```
 Swapping 엔진을 초기화하는 `init_swap_valid_table ()` 함수는 thread 시스템이 초기화될 때 호출되어 엔진이 초기화된다. 즉, thread가 init될 때 불리는 main 함수에서 `init_swap_valid_table ()`을 호출한다.

#### 2. Swap In

```c 
void swap_in(struct spte *page, void *kva) {
    int i;
    int id = page->swap_id;

    lock_acquire(&swap_lock);
    {
        if (id > bitmap_size(swap_valid_table) || id < 0) {
            sys_exit(-1);
        }

        if (bitmap_test(swap_valid_table, id) == true) {
            /* This swapping slot is empty. */
            sys_exit(-1);
        }

        bitmap_set(swap_valid_table, id, true);
    }

    lock_release(&swap_lock);

    for (i = 0; i < SECTOR_NUM; i++) {
        block_read(swap_disk, id * SECTOR_NUM + i, kva + (i * BLOCK_SECTOR_SIZE));
    }
}
```
Swap in은 디스크로부터 페이지를 불러오는 작업이다. 이를 위해 인자로 Supplemental Page Table Entry와 해당 페이지의 물리 주소를 받는다. STPE를 참조하여 불러올 swapped page의 `swap_id`를 구한다. 이후, swap table에서 해당 id에 상응하는 값을 1로 설정하여 이제 해당 슬롯이 비었음을 표시한다. 이후 해당 슬롯의 값을 `block_read`를 통해 주어진 물리 주소에 읽어옴으로써 swap in 과정을 마친다. 만약 `swap_id`가 범위 바깥에 있거나, swap table에서 해당 id에 상응하는 데이터가 없다면 에러가 발생했음을 알린다. 끝으로, `swap_valid_table`을 참조하는 모든 작업들은 `swap_lock`에 의해 상호 배제적으로 작동함을 양지하자.


```c 
bool
load_page(struct hash *spt, void *upage) {
    // ...

    switch (e->status) {
        // ...
        case PAGE_SWAP:
            swap_in(e, kpage);
    }

    // ...
}
```
 앞서 살펴본 `swap_in ()`은 `load_page ()` 과정에서 호출된다. 앞서 살펴보았듯이, `load_page ()`는 page fault 시에 호출되어 페이지의 lazy loading을 돕는 함수이다. 해당 함수에서 Supplemental Page Table Entry를 참조했을 때, 해당 entry의 상태가 `PAGE_SWAP` 이라면 이 페이지는 swap in 해야하는 페이지임을 pintos가 알 수 있다. 그러므로, lazy loading 과정에서 이와 같은 상황에 `swap_in()`을 호출하여 해당 페이지를 메모리로 불러옴으로써 swap in이 실현되도록 구현하였다. 참고로, 이후에 살펴보겠지만 페이지가 evict 되는 과정에서 SPTE의 상태가 `PAGE_SWAP`으로 설정된다. 


#### 3. Swap Out
```c
int swap_out(void *kva) {
    int i;
    int id;

    lock_acquire(&swap_lock);
    {
        id = bitmap_scan_and_flip(swap_valid_table, 0, 1, true);
    }
    lock_release(&swap_lock);

    for (i = 0; i < SECTOR_NUM; ++i) {
        block_write(swap_disk, id * SECTOR_NUM + i, kva + (BLOCK_SECTOR_SIZE * i));
    }

    return id;
}
```
 Swap out은 페이지를 디스크에 저장하는 과정이다. 이를 위해 `swap_out`은 인자로 페이지의 물리 주소를 입력받는다. 우선 이 함수가 실행되면, `bitmap_scan_and_flip ()`을 호출하여 `swap_valid_table`에서 유효한 슬롯의 `id`를 받아온다. `bitmap_scan_and_flip ()`은 주어진 인자에 상응하는 공간이 있는지 확인하고 해당 값을 뒤집은 다음 해당 공간의 id를 반환하는 과정을 atomic한 하나의 함수에서 실행할 수 있는 함수이다. 이후, 해당 `id`를 바탕으로 주어진 물리 주소에 적혀있는 페이지를 디스크의swapping 공간에 작성하여 swap out이 실현되도록 구현하였다. 이때, `swap_valid_table`을 참조하는 과정은 `swap_lock`에 의해 상호배제적으로 동작함을 양지하자.

```c 
void evict_page() {
    ASSERT(lock_held_by_current_thread(&frame_lock));

    struct fte *e = clock_cursor;
    struct spte *s;

    /* BEGIN: Find page to evict */
    do {
        if (e != NULL) {
            pagedir_set_accessed(e->t->pagedir, e->upage, false);
        }

        if (clock_cursor == NULL || list_next(&clock_cursor->list_elem) == list_end(&frame_table)) {
            e = list_entry(list_begin(&frame_table),
            struct fte, list_elem);
        } else {
            e = list_next(e);
        }
    } while (!pagedir_is_accessed(e->t->pagedir, e->upage));
    /*  END : Find page to evict */

    s = get_spte(&thread_current()->spt, e->upage);
    s->status = PAGE_SWAP;
    s->swap_id = swap_out(e->kpage);

    lock_release(&frame_lock);
    {
        falloc_free_page(e->kpage);
    }
    lock_acquire(&frame_lock);
}
```

`swap_out ()`은 메모리에서 쫓아낼 페이지를 정하는 함수인 `evict_page ()`에서 호출된다. `evict_page ()`는 clock algorithm을 바탕으로 swap out할 페이지를 선정한다. Accessed bit을 바탕으로 페이지들을 순회하면서 access되지 않은 페이지를 만나면 해당 페이지를 swap out할 페이지로 선정한다. 이후, 해당 페이지의 Supplemental Page Table Entry를 구해서 해당 entry의 상태를 `PAGE_SWAP`으로 설정하고, `swap_out ()`을 호출시켜 out 시킨 뒤 SPTE에 해당 `swap_id`를 기록한다. 마지막으로 frame table에서 해당 페이지를 할당 해제하여 쫓아내는 작업을 마무리한다. 이때, frame table을 여러 프로세스의 동시다발적인 요청으로부터 안전하게 수정하기 위해 `frame_lock`으로 table을 보호했다.

```c 
void *
falloc_get_page(enum palloc_flags flags, void *upage) {
    // ...
    if (kpage == NULL) {
        evict_page();
        kpage = palloc_get_page(flags);
        if (kpage == NULL)
            return NULL;
    }
    // ...
}
```
 `evict_page ()`는 새로운 페이지를 할당하는 함수인 `falloc_get_page ()`에서 호출된다. 새로운 페이지를 할당하는 과정 중에 메모리에 더 이상 공간이 없음을 인지하면 swap_out 시켜야하기 때문이다. 이를 위해, `falloc_get_page ()`에서 `palloc_get_page ()`에 실패하면 `evict_page ()`를 호출하여 특정 페이지를 swap out 시키는 방식으로 구현하였다.

## Discuss

### 1. Lock and Disk

 Swap in 및 out 되는 모든 과정을 `swap_lock`을 통해 critical section으로 지정하면 안전하겠지만, 디스크를 참조하는 작업은 매우 오래 걸리기 때문에, 해당 과정을 critical section에 포함시킬 경우 성능 상에 매우 큰 낭비가 생길 수 있다. 그렇기 때문에, swap table을 참조하는 영역 만을 `swap_lock`으로 보호하고 디스크를 참조하는 영역은 의도적으로 보호에서 배제하여 성능 하락을 최대한 피할 수 있게 구현하였다.

### 2. File-backed Page
 기존 design report에서는 file-backed page를 위한 swap in 및 out 과정을 별도로 설계하였다. 하지만, 실제 동작 과정에서 annoymous page와 file-backed page를 나누어서 구현할 필요가 없었기 때문에 단순히 anonymous page를 위한 swap in 및 out 만을 구현하여 운용하도록 구현하였다.


# On Process Termination

## Implementation

### Algorithms

#### 1. Free Memory-Mapped Files
```c
void process_exit(void) {
    // ...
    for (i = 0; i < cur->mapid; i++) {
        sys_munmap(i);
    }
    // ...
}
```
 이전 프로젝트 2에서, 파일과 관련된 시스템콜을 구현하면서 프로세스가 종료될 때 모든 파일을 닫아주는 부분을 구현하였다. 이와 유사하게, 현재 프로세스가 연 MMF들은 프로세스가 종료될 때 모두 닫아주어야 한다. 이를 위해, 현재 프로세스의 `mapid`를 기준으로 모든 현재 프로세스에 할당된 모든 memory-mapped file들에 대해 `sys_mummap ()` 을 호출하여 모든 파일들이 정상적으로 닫힐 수 있도록 구현하였다.

#### 2. Destroy Supplemental Page Table
```c
static void
page_destutcor(struct hash_elem *elem, void *aux) {
    // Free all SPTE in SPT.
    struct spte *e;
    e = hash_entry(elem, struct spte, hash_elem);
    free(e);
}
```
```c
void
destroy_spt(struct hash *spt) {
    // Destroy SPT hash with page_destutcor.
    hash_destroy(spt, page_destutcor);
}
```

```c 
void process_exit(void) {
    // ...
    destroy_spt(&cur->spt);
    // ...
}
```

 각 프로세스에는 페이지들이 할당되어 있고, 각 페이지들은 그에 상응하는 Supplemental Page Table Entry를 가지고 있다. 또한, 이 프로세스에 상응하는 entry들은 현재 프로세스의 Supplemental Page Table에 의해 관리되고 있다. 그러므로, 이 프로세스의 SPT를 참조하여 각 entry에 대해`page_destutcor ()`를 부르는 `destroy_spt ()`를 호출하여 SPT와 그 entry들이 정상적으로 할당 해제 될 수 있도록 구현하였다.

## Discuss

### 1. Frame Table
 기존 design report에서는 frame table을 이 과정에서 할당 해제하려고 하였으나, 해당 디자인은 잘못된 디자인이었기에 실제 구현에서 적용되지 않았다. Frame table은 각 프로세스 당 관리되는 테이블이 아니라 시스템 전역적으로 이용되는 테이블이기 때문에 프로세스의 종료 과정에서 할당 해제되면 큰 오류가 발생할 수 있다. 그렇기 때문에, design report와 달리 Supplemental Page Table만 할당 해제하는 방향으로 구현되었다. 대신 `falloc_get_page ()`한 메모리는 종료 전 `falloc_free_page ()`로 해제하도록 했다.
 
### 2. Lock
기존 design report에서 프로세스가 holding 중인 lock을 list로 관리하고자 했지만 lock을 acquire한 후 release하지 않고 종료될 경우 직접 release를 하도록 했다.

# Result
![](https://i.imgur.com/XwDfjTV.png)

채점 환경에서 모든 테스트를 통과함을 확인할 수 있다.