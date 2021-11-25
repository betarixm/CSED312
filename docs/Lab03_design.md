Lab 3: Virtual Memory
===
김민석 (20180174), 권민재 (20190084) `CSED312`

# Introduction

## Background

### Terminology
메모리 관리에 사용되는 기본 용어를 정리하고 넘어 가겠다.

#### 1. Pages
*Virtual Page*로도 불리며, 4,096 bytes(the *page size*) 길이의 연속적인 가상 메모리 공간을 말한다. 한 page는 반드시 *page-aligned*되어 있어야 하는데, 이는 page size로 균등하게 나눌 수 있는 가상 주소에서 시작해야됨을 의미한다. 따라서 32-bit 가상 주소의 경우 20-bit의 *page number*와 12-bit의 *page offset*(*offset*)으로 나뉠 수 있다.
```
               31               12 11        0
              +-------------------+-----------+
              |    Page Number    |   Offset  |
              +-------------------+-----------+
                       Virtual Address
```
각 프로세스는 가상 주소상 PHYS_BASE 아래에 독립적인 *user (virtual) pages* set을 보유한다. 반면 *kernel (virtual) pages* set의 경우 global이다. 커널은 user와 kernel pages를 접근할 수 있고, user는 자신의 user pages만 접근 할 수 있다. 

#### 2. Frames
*Physical frame*이나 *page frame*으로도 불리며, 물리 메모리의 연속적인 공간을 의미한다. Page와 마찬가지로, frame 또한 page-size이고, page-aligned되어야 한다. 따라서 32-bit의 물리 주소 는 20-bit의 *frame number*와 12-bit의 *frame offset*(*offset*)으로 나뉠 수 있다.
```
               31               12 11        0
              +-------------------+-----------+
              |    Frame Number   |   Offset  |
              +-------------------+-----------+
                       Physical Address
```
Pintos는 kernel 가상 메모리를 직접적으로 물리 메모리에 mapping 한다. Kernel의 가상 메모리의 첫번째 page는 물리 메모리의 첫번째 frame과 map되어있고, 두번째 page는 두번째 frame과 map되어 있는 형식이다. 
```c
// File: threads/vaddr.h
/* Returns kernel virtual address at which physical address PADDR
   is mapped. */
static inline void *
ptov (uintptr_t paddr)
{
  ASSERT ((void *) paddr < PHYS_BASE);

  return (void *) (paddr + PHYS_BASE);
}

/* Returns physical address at which kernel virtual address VADDR
   is mapped. */
static inline uintptr_t
vtop (const void *vaddr)
{
  ASSERT (is_kernel_vaddr (vaddr));

  return (uintptr_t) vaddr - (uintptr_t) PHYS_BASE;
}
```
Pintos 상에 물리 주소를 가상 주소로, 가상 주소를 물리 주소로 변환하는 함수가 정의 되어 있다.

#### 3. Page Table
CPU가 가상 주소를 물리 주소로 (page를 frame으로) 변환할 때 사용하는 데이터 구조이다.
아래 diagram은 page와 frame 같의 관계를 나타낸다. 
```
	
                         +----------+
        .--------------->|Page Table|-----------.
       /                 +----------+            |
   0   |  12 11 0                            0   V  12 11 0
  +---------+----+                          +---------+----+
  |Page Nr  | Ofs|                          |Frame Nr | Ofs|
  +---------+----+                          +---------+----+
   Virt Addr   |                             Phys Addr   ^
                \_______________________________________/
```
`pagedir.c`에 page table 관리 코드가 존재한다.

#### 4. Swap Slots
*Swap slot*은 디스크의 swap partition에 존재하는 연속적인 page-size 공간을 말한다. Swap slot 또한 page-align 된다.

### Page Allocation

#### 0. `install_page ()`
```c 
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
```
`install_page`는 전달받는 페이지 주소를 바탕으로 해당 페이지를 현재 실행 중인 스레드의 페이지 디렉토리에 연결하는 작업을 수행한다. `upage`가 현재 스레드의 디렉토리에 비어있음을 확인하고, `pagedir_set_page ()`를 통해 디렉토리에 페이지를 할당한다.

#### 1. `palloc_get_multiple ()`
```c 
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }

  return pages;
}
```
`palloc_get_multiple ()`은 플래그와 함께 할당받을 페이지 개수를 입력받아 그에 상응하는 페이지 주소를 반환하는 함수이다. 플래그에 따라 user pool, kernel pool 중에서 pool을 선택한 후, `bitmap_scan_and_flip`을 통해 새로운 페이지가 들어갈 자리를 찾는다. 이후 할당에 성공했다면 주소를 반환하고, 아니라면 커널 패닉을 일으키는 방식으로 작동한다.

#### 2. `palloc_get_page ()`
```c 
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}
````
`palloc_get_page ()`는 앞서 소개한 `palloc_get_multiple ()`을 이용하여 페이지 1개를 할당받고 해당 주소를 반환하는 함수이다.

#### 3. `palloc_free_multiple ()`
```c 
/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}
```
`palloc_free_multiple ()`은 여러개의 페이지를 할당 해제하는 함수이다. 주어진 페이지가 kernel 혹은 user pool 중 어떤 pool에 속한 페이지인지 확인한 이후, 풀에서 할당 해제한다. `bitmap_set_multiple ()`을 이용하여 pool의 used map을 업데이트한다.

#### 4. `palloc_free_page ()`
```c 
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}
```
`palloc_free_page ()`는 앞서 소개한 `palloc_free_multiple ()`를 이용하여 주어진 페이지 1개를 할당해제하는 함수이다.

#### 5. `load_segment ()`
```c
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}
```
프로세스가 일단 실행되면, `load_segment ()`가 호출되어 세그먼트들이 셋업된다. 이 과정에서, 실행 파일을 저장하기 위한 커널 페이지와 유저 페이지는 한번에 로드된 후 `install_page`를 통해 매핑된다. 이러한 일련의 과정에서, 세그먼트들이 메모리에 로드되는 과정은 한번에 이루어지고 있다. 즉, 현재 pintos에 lazy loading이 구현되어 있지 않은 것으로 파악할 수 있다. 

### Stack
#### 0. Memory Layout
```
  0xffffffff +----------------------------------+
             |       kernel virtual memory      |
   PHYS_BASE +----------------------------------+
             |            user stack            |
             |                 |                |
             |                 |                |
             |                 V                |
             |          grows downward          |
             |                                  |
             |                                  |
             |                                  |
             |                                  |
             |           grows upward           |
             |                 ^                |
             |                 |                |
             |                 |                |
             +----------------------------------+
             | uninitialized data segment (BSS) |
             +----------------------------------+
             |     initialized data segment     |
             +----------------------------------+
             |           code segment           |
  0x08048000 +----------------------------------+
             |                                  |
             |                                  |
             |                                  |
             |                                  |
             |                                  |
           0 +----------------------------------+
```
Pintos의 가상 메모리는 kernel 가상 메모리 공간과 user 가상 메모리 공간으로 나눠져 있다. User의 가상 메모리는 `PHYS_BASE`(`0xc0000000`)부터 `0x08048000`으로 고정되어 있다.

#### 1. Stack Initialize
```c 
/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}
```
Pintos에서 프로세스의 스택은 `setup_stack ()`을 통해 구성된다. 하지만, 이 과정은 단순히 `palloc_get_page ()`를 통해 페이지를 할당받은 이후에 `install_page`로 메모리에 매핑하는 과정으로 구성되어 있다. 즉, 현재의 구현 상태는 스택의 성장을 고려하지 않은 디자인이라고 말할 수 있다.

### Page Table

#### 0. Page Directory

Intel processor documentation에서, 하드웨어 page table의 abstract interface를 *page directory*라고 명시하고 있다.

#### 1. 기본 함수

##### 1.1. `pagedir_create()`
```c
uint32_t *
pagedir_create (void) 
{
  uint32_t *pd = palloc_get_page (0);
  if (pd != NULL)
    memcpy (pd, init_page_dir, PGSIZE);
  return pd;
}
```
`pagedir_create ()`는 새로운 page table을 생성하고 반환한다. 새로운 page table은 user의 가상 mapping이 아닌 pintos의 kernel 가상 page mapping을 가지고 있다. 만약 메모리를 할당 할 수 없다면 `null pointer`를 반환한다.

##### 1.2. `pagedir_destroy()`
```c
void
pagedir_destroy (uint32_t *pd) 
{
  uint32_t *pde;

  if (pd == NULL)
    return;

  ASSERT (pd != init_page_dir);
  for (pde = pd; pde < pd + pd_no (PHYS_BASE); pde++)
    if (*pde & PTE_P) 
      {
        uint32_t *pt = pde_get_pt (*pde);
        uint32_t *pte;
        
        for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++)
          if (*pte & PTE_P) 
            palloc_free_page (pte_get_page (*pte));
        palloc_free_page (pt);
      }
  palloc_free_page (pd);
```
`pd`가 참조하는 모든 자원들을 `free`한다. 이때, 자원으로는 자기 자신 뿐만 아니라 mapping하는 프레임들이 포함된다.

##### 1.3. `pagedir_activate()`
```c
void
pagedir_activate (uint32_t *pd) 
{
  if (pd == NULL)
    pd = init_page_dir;
  asm volatile ("movl %0, %%cr3" : : "r" (vtop (pd)) : "memory");
}
```
`pd`를 활성화 한다. 활성화된 page directory은 CPU가 메모리 참조를 해석하기 위해 사용하는 page directory를 뜻한다.

#### 2. 검사 & 갱신 함수
##### 2.1. `pagedir_set_page()`
```c
bool
pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)
{
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_user_vaddr (upage));
  ASSERT (vtop (kpage) >> PTSHIFT < init_ram_pages);
  ASSERT (pd != init_page_dir);

  pte = lookup_page (pd, upage, true);

  if (pte != NULL) 
    {
      ASSERT ((*pte & PTE_P) == 0);
      *pte = pte_create_user (kpage, writable);
      return true;
    }
  else
    return false;
}
```
`pd`에 user page `upage`부터 kernel 가상 주소 `kpage` mapping을 추가한다. `pagedir_set_page()`을 호출한 시첨에 `pd`에서 `upage`는 mapping이 안된 page여야 한다. Mapping의 성공 여부를 반환한다.

##### 2.2. `pagedir_get_page()`
```c
void *
pagedir_get_page (uint32_t *pd, const void *uaddr) 
{
  uint32_t *pte;

  ASSERT (is_user_vaddr (uaddr));
  
  pte = lookup_page (pd, uaddr, false);
  if (pte != NULL && (*pte & PTE_P) != 0)
    return pte_get_page (*pte) + pg_ofs (uaddr);
  else
    return NULL;
}
```
User 가상 메모리 주소 `uaddr`이 map된 frame을 찾는다. `uaddr`이 map된 frame이 존재한다면 그 frame의 kernel 가상 메모리 주소를 반환하고 frame이 존재하지 않는다면 `null pointer`를 반환한다.

##### 2.3. `pagedir_clear_page()`
```c
void
pagedir_clear_page (uint32_t *pd, void *upage) 
{
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  pte = lookup_page (pd, upage, false);
  if (pte != NULL && (*pte & PTE_P) != 0)
    {
      *pte &= ~PTE_P;
      invalidate_pagedir (pd);
    }
}
```
`pd`의 `upage`를 `not presetnt`로 표시한다. 이후 `upage`의 접근은 `fault`가 발생한다. 만약 `upage`가 map되지 않았다면 아무 동작도 하지 않는다.

#### 3. Accessed / Dirty Bits

Page replacement algorithms을 구현하기 위해 page table entry(PTE)에 page의 상태를 저장하는 두개의 flag bit을 도입했다. Page에 read나 write 동작이 실행되면 CPU는 `accessed bit`을 `1`로 초기화 한다. Page에 wirte 동장이 실행되면 CPU는 `dirty_bit`을 `1`로 초기화 한다. CPU는 `accessed bit`과 `dirty_bit`을 `0`으로 초기화할 수 없고, 오직 OS만이 `0`으로 처리할 수 있다.

```c
// File: userprog/pagedir.h
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
```

`pagedir.c`에 이 flag bits의 getter/setter가 존재한다.

#### 4. Page 구조

가장 상위 paging 데이터 구조는 `page dirtectory`(PD)라고 불리는 page 이다. PD는 32-bit page directory entries(PDEs) 1024개로 이뤄진 배열이다. PD의 각 entry는 4MB의 가상 메모리 공간을 나타낸다. PDE는 `page table`(PT)이라고 불리는 또 다른 page의 물리 주소를 가르킬 수 있는데, PT는 32-bit page table entries(PTEs) 1024개로 이뤄진 배열이다. PT의 각 entry는 한 4kB의 virtual page를 물리 page로 변환된다.

##### 4.1. 주소 변환

가상 주소에서 물리 주소로의 변환은 아래 diagram에 묘사된 세 단계를 따른다.
```
 31                  22 21                  12 11                   0
+----------------------+----------------------+----------------------+
| Page Directory Index |   Page Table Index   |    Page Offset       |
+----------------------+----------------------+----------------------+
             |                    |                     |
     _______/             _______/                _____/
    /                    /                       /
   /    Page Directory  /      Page Table       /    Data Page
  /     .____________. /     .____________.    /   .____________.
  |1,023|____________| |1,023|____________|    |   |____________|
  |1,022|____________| |1,022|____________|    |   |____________|
  |1,021|____________| |1,021|____________|    \__\|____________|
  |1,020|____________| |1,020|____________|       /|____________|
  |     |            | |     |            |        |            |
  |     |            | \____\|            |_       |            |
  |     |      .     |      /|      .     | \      |      .     |
  \____\|      .     |_      |      .     |  |     |      .     |
       /|      .     | \     |      .     |  |     |      .     |
        |      .     |  |    |      .     |  |     |      .     |
        |            |  |    |            |  |     |            |
        |____________|  |    |____________|  |     |____________|
       4|____________|  |   4|____________|  |     |____________|
       3|____________|  |   3|____________|  |     |____________|
       2|____________|  |   2|____________|  |     |____________|
       1|____________|  |   1|____________|  |     |____________|
       0|____________|  \__\0|____________|  \____\|____________|
                           /                      /
```
1. The most-significant 10 bits(bits 22 ~ 31)은 page directory의 index이다. 만약 PDE가 `present`라면 `page table`의 물리 주소를 읽어온다. 만약 PDE가 `present`가 아니면 `page fault`가 발생한다.
2. 그 다음 10 bits(bits 12 ~ 21)은 page tabled의 index이다. 만약 PTE가 `present`라면 한 data page의 물리 주소를 읽어온다. 만약 PTE가 `present`가 아니면 `page fault`가 발생한다.
3. The least-significant 12 bits(bits 0 ~ 11)이 data page의 물리 주소에 추가되며 최종 물리 주소가 반환된다.

##### 4.2. Page Table Entry Format (Page Directory Entry Format)
```
 31                                   12 11 9      6 5     2 1 0
+---------------------------------------+----+----+-+-+---+-+-+-+
|           Physical Address            | AVL|    |D|A|   |U|W|P|
+---------------------------------------+----+----+-+-+---+-+-+-+
```
Page table과 page directory의 각 entry는 위와 같은 format을 가진다. Page table은 page-align되어 있기 때문에 the least-significant 12 bits(bits 0 ~ 11)은 `0`으로 초기화 된다. 따라서 이 열두 bits에 entry의 정보를 저장한다. Bits 9~11의 `AVL`은 OS가 사용 가능한지를 나타낸다. Bit 6의 `D`은 앞서 다룬 dirty bit, bit 5의 `A`은 앞서 다룬 acceseed bit을 나타낸다. Bit 2은 "user/supervisor" bit이다. `1`일 때 user processes가 page에 접근 할 수 있고, `0`이면 kernel만 page에 접근 가능(user accesses는 page fault)하다.  Bit 1의 "read/write" bit은 `1`일 때 page가 writable임을 나타내고, `0` 일때 write attempts는 page fault. Bit 0의 "present" bit은 `1`일 때 entry가 유효함을 의미하고, `0`인 entry의 접근은 page fault가 발생한다.
```c
// File: threads/pte.h
// ...
#define PTE_FLAGS 0x00000fff    /* Flag bits. */
#define PTE_ADDR  0xfffff000    /* Address bits. */
#define PTE_AVL   0x00000e00    /* Bits available for OS use. */
#define PTE_P 0x1               /* 1=present, 0=not present. */
#define PTE_W 0x2               /* 1=read/write, 0=read-only. */
#define PTE_U 0x4               /* 1=user/kernel, 0=kernel only. */
#define PTE_A 0x20              /* 1=accessed, 0=not acccessed. */
#define PTE_D 0x40              /* 1=dirty, 0=not dirty (PTEs only). */
// ...
```
기존 Pintos에서 구현되어 있는 구조를 확인할 수 있다.

### Page Fault

Page fault는 프로세스가 접근 하고자 하는 가상 메모리에 해당하는 page가 물리 메모리에 load되지 않았을 때 발생한다. 

```c
static void
page_fault (struct intr_frame *f) 
{
  // ...

  sys_exit (-1); // Project 2 당시 추가한 code

  // ...
    
  kill (f); // Pintos 기존 구현 내용
}
```

기존 pintos의 구현 상 page fault가 발생하면 무조건 프로세스가 종료하게 된다. 하지만, page fault handler를 통해 disk에서 해당 페이지를 찾아 메모리에 load 할 수도 있다. 메모리에 빈 공간이 없을 때 기존 page를 replace 하기 위한 알고리즘을 고안해야 한다.


# Frame Table

## Problem Description

Frame을 효과적으로 관리하기 위해서 **Frame table**을 구현해야 한다. Frame table은 user page를 가지고 있는 frame 하나 당 하나의 entry를 갖는다. Frame table의 각 entry는 page를 가르키는 pointer를 포함한다. 이때, 이 frame table에 free 상태인 frame이 없을 때 evict할 page를 고름으로써 eviction policy가 구현된다.

## Analysis

앞서 살펴본 것 처럼 기존 pintos는 free 상태인 frame이 없을 경우 프로세스가 원하는 동작을 실행할 수 없다. 기존 pintos의 page table 관리는 앞서 살펴본 `userprog/pagedir.c`에 구현 되어 있다. page table의 allocation 및 deallocation은 앞서 살펴본 `threads/palloc.c`에 구현 되어 있다. Frame table을 통해 이미 할당된 frame을 evict해 새로운 page를 할당 할 수 있다.

## Solution

### Data Structure

#### 1. Frame Table Entry
```c
// File: vm/frame.h
/* A frame table entry. */
struct fte
  {
    void *kpage;  // Kernel virtual page. 
    void *upage;  // User virtual page.
  
    struct thread *t;  // Owner thread of the fte.
  
    struct list_elem list_elem;  // List element for the frame table.
  };
```
`struct fte`를 통해 frame table의 entry를 관리하다. `kpage`와 `upage`는 각각 커널 가상 페이지와 유저 가상 페이지 주소를 가리킨다. 이때, 페이지가 속한 스레드가 표시될 필요가 있기 때문에 스레드 포인터 `t`를 통해 해당 스레드를 마킹할 수 있도록 하였다. 마지막으로, `fte` 인스턴스들은 table 리스트의 원소로서 운용될 수 있어야 하기 때문에 `fte_list_elem` 필드를 추가해주었다.


#### 2. Frame Table
```c
// File: vm/frame.c
static struct list frame_table;
```
앞서 선언한 `fte`를 `list` 형식으로 관리한다.

#### 3. Frame Table Lock
```c
// File: vm/frame.c
static struct lock frame_lock;
```
`lock`을 통해 global인 `frame_table`의 synchronization을 관리한다. 여러 프로세스에서 동시에 하나의 프레임 테이블에 접근할 경우 그 내용의 신뢰도가 떨어질 수 있기 때문이다.

### Algorithms

#### 0. `frame_init ()`
```c
// File: vm/frame.c
void
frame_init ()
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}
```
`frame_init`을 통해 새로운 `frame`을 초기화 한다. `frame_table`과 `frame_lock`을 초기화 하는 과정이 수반되는 것이다.

#### 1.	Allocate/deallocate Frames

##### 1.1. `falloc_get_page ()`
```c
void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  struct fte *e;
  void *kpage;
  
  lock_acquire (&frame_lock);
  
  kpage = palloc_get_page (flags);
  
  if (kpage != NULL)
  {
    e = (struct fte *)malloc (sizeof (struct fte));
    e->kpage = kpage;
    e->upage = upage;
    
    e->t = thread_current ();
    
    list_push_back (&frame_table, e->list_elem);
  }
  
  lock_release (&frame_lock);
  
  return kpage;
}
```
`upage`의 frame을 allocate 하고 그 주소, 즉 `kpage`를 반환한다. 이때, `frame_lock`을 사용해 critical section을 보호할 수 있도록 디자인하였다.

##### 1.2. `falloc_free_page ()`
```c
void
falloc_free_page (void *kpage)
{
  struct fte *e;
  
  lock_acquire (&frame_lock);
  
  e = get_fte (kpage);
  if (e == NULL)
    sys_exit (-1);
  
  list_remove (e->list_elem);
  palloc_free_page (e);
  pagedir_clear_page (e->holder->pagedir, e->upage);
  
  lock_releas (&frame_lock);
}
```
`kpage`에 해당하는 frame table entry `e`을 할당 해제한다. `frame_talbe`에서 `e`를 삭제하고, `e`의 "present" bit을 `0`으로 초기화 한다. `frame_lock`을 사용해 critical section을 보호한다.

###### 1.2.1. `get_fte ()`
```c
struct fte *
get_fte (void* kpage)
{
  for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    if (list_entry (e, struct fte, elem)->kpage == kpage)
      return list_entry (e, struct fte, elem);
  
  return NULL;
}
```
`kpage`에 해당하는 `fte`를 반환한다.

#### 2. Choose a Victim
Swap Table에서 구현하도록 하겠다.
```c
// File: userprog/process.c
static bool 
setup_stack(void **esp)
{
  uint8_t *kpage;
  bool success = false;
  kpage = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if (kpage != NULL)
  {
    success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      falloc_free_page(kpage);
  }
return success;
}
```
Eviction이 구현된 후에는 `setup_stack ()`의 메모리 allocation을 falloc으로 처리해 stack growth를 적용한다.

#### 3. Search frames used by user process

# Lazy Loading

## Problem Description

**Lazy loading**이란 프로세스가 시작하며 메모리 할당을 위해 로드되는 시점에  stack setup 부분만 로드 되는 방식을 말한다. 다른 부분은 메모리에 로드 되지 않고, page만 할당 된다. 그 후 page fault가 발생하면 해당 page를 메모리에 로드한다. Page fault handler는 처리가 끝나면 다시 정상적으로 프로세스의 동작이 수행 될 수 있도록 한다.필요한 데이터만 메모리에 로드해 메모리를 절약할 수 있다.

## Analysis

앞서 pintos 코드에서 봤듯이 기존 pintos는 프로세스가 시작함과 동시에 executable codes가 모두 메모리로 load 되는 방식으로 구현되어 있다. 이 방식은 사용하지 않는 데이터도 메모리에 로드하기 때무에 메모리 낭비가 발생한다. 또한 page fault 발생 시 무조건 프로세스를 종료하거나 kernel panic을 야기한다.

## Solution
### Data Structure
#### Supplemental Page Table Entry
```c
// File: vm/page.h
struct spte
  {
    struct file *file;  // File to read.
    off_t ofs;  // File off set.
    uint32_t read_bytes, zero_bytes;  // Bytes to read or to set to zero.
    bool writable;  // whether the page is writable.
  };
```
프로세스 실행 시 executable codes는 메모리에 로드 되지 않고, page로 나눠 관리한다. 필요에 의해 page를 로드 할 수 있도록 page의 추가적인 정보를 저장하는 SPT의 entry 구조체를 선언해 이 정보를 추적 한다. 구조체는 이후 섹션에서 자세히 다루도록 하겠다.

### Algorithms

#### 1. `init_file_spte ()`
```c
struct spte *
init_file_spte (struct list *spt, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct spte *e;
  
  e = (struct spte *)malloc (sizeof (struct spte));
  
  // Initialize E with passed parameters.
  
  e->status = PAGE_FILE;
  
  // Put e to the Supplemental Page Table. 
  
  return e;
}
```
주어진 file 정보를 이용하여 *Supplemental Page Table Entry*를 만들어 반환한다.

##### 1.1. `load_segment ()`
```c
// File: userprog/process.c
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  // ...

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
      init_file_spte (spt, upage, kpage, read_bytes, zero_bytes, writable);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += page_read_bytes;
    }
  return true;
}
```
메모리 page를 가져와 load하는 대신 supplemental page table frame entry를 만들어 `spt`에 삽입한다.

##### 1.2. `page_fault ()`
```c
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  struct hash *spt;
  void *upage;
  
  // ... 
  upage = pg_round_down(fault_addr);
  
  if (is_kernel_vaddr (fault_addr) || !not_present)
    sys_exit (-1);
  
  if (load_page (spt, upage))
    return;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}
```
User mode에서 page fault가 발생할 시 즉시 프로세스를 종료 시키지 않고, `fault_addr`이 kernel 가상 메모리 주소 이거나 `not_present`가 false일 경우에만 종료 시킨다. 그 외의 경우엔 lazy load 될 수 있기 때문이다. `load_page`를 통해 lazy load를 시도한다. Lazy loading이 성공했을 경우 `return`을 통해 다시 프로세스의 정상 동작으로 돌아간다.

###### 1.2.1. `load_page ()`
```c
// File: vm/page.c
bool
load_page (struct hash *spt, void *upage)
{
  struct spte *e;
  struct lock *file_lock;
  uint32_t *pagedir;
  void *kpage;
  
  e = get_spte (spt, upage);
  kpage = falloc_get_page (PAL_USER, upage);
  // pagedir = 현 thread의 pagedir
  
  if (!pagedir_set_page (pagedir, upage, kpage, e->writable))
  {
    // free kpage frame
    sys_exit (-1);
  }
  
  e->kpage = kpage;
  e->status = PAGE_FRAME;
  
  return true;
}
```
`upage`에 해당하는 가상메모리의 page를 물리 메모리로 load하고 프로세스의 `spt`에 추가해 관리할 수 있도록 한다. faulting virtual address의 page table entry가 물리 page를 point하도록 한다. 이때, `pagedir_set_page()`를 사용한다.

# Supplemental Page Table

## Problem Description

기존 pintos의 page table format은 한계점이 존재한다. page table의 부가적인 정보를 저장하기 위해 "supplemental" page table을 도입한다.

## Analysis

앞서 살펴본 Page Table Entry Format을 보면 present, read/write, user/supervisor, accessed, dirty, availablity 정보 밖에 담지 못한다. page fault를 handle 하기 위해 추가적인 정보가 필요하다. 프로세스 종료 시 free할 자원을 결정하기 위해 추가적인 정보가 필요하다.

## Solution

### Data Structure

#### 1. Supplemental Page Table Entry
```c
// File: vm/page.h
struct spte
  {
    void *upage;
    void *kpage;
  
    struct hash_elem hash_elem;
  
    int status;  
  
    struct file *file;  // File to read.
    off_t ofs;  // File off set.
    uint32_t read_bytes, zero_bytes;  // Bytes to read or to set to zero.
    bool writable;  // whether the page is writable.
  };
```
`spte` 구조체에 가상 주소를 저장하는 필드와 `struct hash spt`를 관리하기 위한 `hash_elem` 필드를 추가했다.

##### 1.1. Page Status
```c
// File: vm/page.h
#define PAGE_ZERO 0
#define PAGE_FRAME 1
#define PAGE_FILE 2
...
```
page의 각 상태를 상수로 선언해 관리하도록 한다.

### Algorithms
- page fault handler 구현 (page_fault() 수정해서 구현)

#### 0. Basic Managing 
##### 0.1. Field for Thread
```c
struct thread
  {
    // ...
    struct hash spt; /* Supplemental page table. */
    // ...
  };
```
각 스레드마다 할당된 페이지들을 관리할 수 있도록 supplemental page table 필드를 스레드 구조체에 추가한다.

##### 0.2. `get_spte ()`
```c
// File: vm/page.c
struct spte *
get_spte (struct hash spt, void *upage)
{
  // Return corresponding SPTE of UPAGE in spt
  // Return NULL if theres no corresponding SPTE of UPAGE in spt
}
```
Supplemental page table에서 upage에 따른 entry를 찾을 수 있도록 도와주는 함수를 디자인하였다.

#### 1. `init_spt ()`
```c
void
init_spt (struct hash *spt)
{
  // hash_init을 이용하여 spt를 초기화한다.
}
```

```c
static void 
start_process (void *file_name_)
{
  // ...
  init_spt (thread_current()->spt);
  // ...
}
```
`hash_init`을 이용하여 각 스레드에 설치된 `spt`를 초기화하는 함수를 선언하였다. 각 스레드가 생성될 때 이 함수가 호출되어 `spt`가 적절한 형식으로 초기화될 수 있도록 한다.


#### 2. Page Initialization
##### 2.1. `init_frame_spte ()`
```c
void
init_spte (struct hash *spt, void *upage, void *kpage)
{
  struct stpe *e;
  e = (struct spte *) malloc (sizeof stpe);
  
  e->upage = upage;
  e->kpage = kpage;
  
  e->status = PAGE_FRAME;
  
  hash_insert (spt, &e->hash_elem);
}
```
새로운 page frame을 할당한 후, `spt`에 프레임을 삽입할 수 있도록 구현하였다.

#### 2.2. `init_zero_spte ()`
```c
void
init_zero_spte (struct hash *spt, void *upage)
{
  struct stpe *e;
  e = (struct spte *) malloc (sizeof stpe);
  
  e->upage = upage;
  e->kpage = NULL;
  
  e->status = PAGE_ZERO;
  
  e->file = NULL;
  e->writable = true;
  
  hash_insert (spt, &e->hash_elem);
}
```
새로운 zero page를 `spt`에 `spte`의 형식으로 할당할 수 있도록 함수를 설계하였다.

#### 3. `setup_stack ()`
```c
// File: userprog/process.c
static bool 
setup_stack(void **esp)
{
  uint8_t *kpage;
  bool success = false;
  struct hash spt = thread_current ()->spt;
  kpage = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if (kpage != NULL)
  {
    success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
    {
      init_frame_spte (&spt, PHYS_BASE - PGSIZE, kpage);
      *esp = PHYS_BASE;
    }
    else
      falloc_free_page(kpage);
  }
return success;
}
```
`install_page ()`가 성공할 경우 `init_frame_spte ()`를 통해 새로운 frame을 `spte`의 형태로 할당할 수 있도록 디자인하였다. 이를 통해, 메모리에 파일을 바로 로드하지 않을 수 있다.

# Stack Growth

## Problem Description

프로세스가 더 많은 스택 영역을 필요로 한다면, 운영체제가 스택 영역을 넓혀주어야 하지만 pintos에는 그러한 구현이 존재하지 않는다. 그렇기 때문에, 프로세스의 필요에 따라 스택 영역을 관리하기 위한 페이지가 더 늘어날 수 있도록 구현해야한다.

## Analysis

현재 pintos는 스택의 크기가 4KB로 고정되어 있다. 즉, 프로세스가 현재 고정된 스택의 영역 바깥에 접근할 경우에는 page fault가 발생하게 된다. 이 점을 이용하여, page fault가 발생했을 때, 해당 fault가 스택이 자라야하는 방향의 주소를 참조하여 일어난 것이라면 스택이 자랄 수 있도록 구현하여 이 문제를 해결할 수 있다. 

## Solution

### Algorithms

#### Page Fault Handler
##### Idea
Page fault가 발생했을 경우, 발생한 주소가 현재 `esp`로부터 일정 수준 가깝다면 page fault handler가 스택을 확장할 수 있도록 구현한다. 하지만 page fault는 다양한 원인으로 발생할 수 있다. 그렇기 때문에, 파일로부터 데이터를 읽어야 할 때나 스와핑을 할 때를 제외한 경우에 스택이 자라게 만들 수 있도록 구현해야 한다.

##### Impl.
```c
static void
page_fault (struct intr_frame *f) 
{
  // ...
  struct hash *spt;
  void *upage;
  
  void *esp;
  
  // ... 
  upage = pg_round_down(fault_addr);
  
  if (is_kernel_vaddr (fault_addr) || !not_present)
    sys_exit (-1);
  
  esp = user ? f->esp : thread_current()->esp;
  if (esp - 32 <= fault_addr && PHYS_BASE - MAX_STACK_SIZE <= fault_addr)
    if (!get_spte(spt, upage))
      init_zero_spte (spt, upage);
  
  if (load_page (spt, upage))
    return;

  // ...
}
```
Page fault가 발생한 지점이 `esp`와 가깝다면 `spt`에 zero entry를 추가하고, 메모리에 로드하도록 하여 스택이 자랄 수 있도록 할 수 있다.

# File Memory Mapping

## Problem Description

메모리의 페이지는 기본적으로 스택이나 힙과 같은 세그먼트에 연결되지만, 경우에 따라서는 파일에 기록되어 있는 데이터와 연결되어야할 필요가 있다. 예를 들어, 코드 영역의 경우는 executable file과 연결되어야 하기 때문에 파일과 연결되어야하는 페이지라고 할 수 있다. Memory mapped file들을 위해 `mmap`, `munmap` 시스템 콜을 구현해야한다. 

## Analysis

Annoymous page의 경우 swapping이 일어날 때 swap file을 통해 디스크 영역에 페이지를 저장해야해야 한다. 이와 달리, file-backed page는 원본 파일이 디스크에 존재하기 때문에 swapping 과정에서 swap file을 생성하여 데이터를 저장할 필요가 없다. 그렇기 때문에, 각 스레드마다 mmap을 통해 로드한 파일들을 관리하는 시스템을 통해 file memory mapping을 해결할 수 있다.

## Solution

### Data Structure

#### Memory Mapped File (MMF)

```c
struct mmf {
  int id;
  struct file* file;
  struct list_elem mmf_list_elem;
    
  struct list page_list;
}
```

`struct mmf`는 Memory Mapped File을 관리하기 위한 가장 작은 단위의 구조체이다. `id`는 MMF들을 효율적으로 관리할 수 있도록 이들 각각을 구별해줄 필드이다. 각 스레드에 리스트 형태로 `mmf`의 원소들이 저장될 것이기 때문에, element로서 역할할 수 있도록 `mmf_list_elem`을 추가해주었다. 마지막으로, 이 파일이 불러와질 페이지들의 목록을 하나의 `mmf`가 관리해야하기 때문에, 페이지들의 목록인 `page_list` 필드를 추가하였다.

#### Thread
```c
struct thread {
  // ...
  struct list mmf_list;
  // ...
}
```
각 스레드마다 MMF를 관리할 수 있도록, `mmap ()`을 통해 연 파일들의 목록인 `mmf_list` 필드를 추가해야 한다.

### Algorithms

#### `mmap`
어떠한 파일을 매핑한다는 요청을 들어왔을 때, 우선 파일이 적절한 파일인지 확인해야한다. 그 후, MMF ID를 생성하여 그에 따른 `struct mmf` 인스턴스를 생성하고 스레드에 추가한다. `mmf`에 대한 페이지들을 생성한 후, `id`를 반환한다.

#### `munmap`

어떠한 파일을 해제해야 한다는 요청이 들어왔을 때, `mmf_list`를 탐색하여 해당 파일에 상응하는 `struct mmf`를 찾는다. 그 후, 해당 인스턴스에 연결된 페이지들을 모두 할당 해제하고 `mmf`를 삭제하여 munmap을 처리한다. 마지막으로 파일을 닫는다.


# Swap Table

## Problem Description
Swapping 하고자 하는 페이지가 스택이나 데이터 영역의 페이지라면, 해당 페이지는 swap file을 통해 저장되어야 한다. 해당 페이지는 demand paging을 통해 다시 메모리에 로드될 수 있도록 해야한다. 이를 위해, swap table을 통해 사용되고 있는, free 상태인 swap slots을 추적하여 LRU를 바탕으로 페이지를 교체할 수 있도록 구현해야 한다.

## Analysis
현재 pintos에는 물리 frame을 evict하는 구현이 없기 때문에, swapping이 불가능하다. 메모리 공간이 없을 경우 page allocation은 실패하고 프로세스는 정상적으로 동작할 수 없다. 

**Annoymous page**의 경우, 데이터와 연결된 파일이 존재하지 않기 때문에, swap out 될 때 별도의 swap file을 생성하여 데이터를 저장하는 과정이 필요하다. 이때, 우리는 swap disk를 이용하여 해당 파일을 저장할 것이다.

**File-mapped page**의 경우, 데이터와 연결된 파일이 존재하기 때문에, swap out 될 때 해당 파일에 변경된 데이터를 적는 과정이 필요하다. 

## Solution

### Data Structure
#### Table
```c 
struct bitmap *swap_table;
```
Swapping 영역을 비트맵으로 관리하기 위해 pintos에서 기본적으로 제공하는 구조체인 `bitmap`을 활용하여 `swap_table`을 선언하였다. 

#### Page 
```c 
struct spte {
    // ...
    // Add field for swap table index.
}
```
Swapping 구현을 효율적으로 하기 위해서, 앞서 저의한 `struct stpe`를 활용한다. 해당 엔트리들을 탐색하면서 swap out 혹은 in 할 페이지를 결정하고 처리할 것이다. 다만, 페이지마다 swap table의 어느 인덱스에 저장되는지를 기록해야할 필요성이 있기 때문에 `spte`에 해당 필드를 추가해주어야 한다.

### Algorithms

#### Initialize table
```c 
void init_swap_table() {
    // 1. Get swap block via block_get_role ()
    // 2. Get size of swap block via block_size () and divide it into "Sectors per Page".
    // 3. Create bitmap with 2's division quotient via bitmap_create ()
}
```
우선 비트맵을 초기화하는 과정이 필요하다. `block_get_role ()`를 이용하여 swap block의 크기를 구한 후, 그 크기를 섹터 당 페이지 수로 나눠서 swap block에 들어갈 수 있는 총 페이지 수를 구한다. 해당 페이지 수로 `bitmap_create ()`를 호출하여 `swap_table`을 초기화한다.

#### Swap In

```c 
static bool anon_swap_in (struct spte *page, void *kva) {
  // 1. 페이지로부터 swap index를 획득한다.
  // 2. swap_table에서 bitmap_test를 이용하여 페이지의  swap index에 해당하는 비트가 채워져 있는지 확인한다.
  // 3. 만약 비트가 채워져 있지 않다면 false를 반환한다. 
  // 4. swap index를 참조하여 swap block에서 값을 읽어서 kva에 넣는다.
  // 5. swap_table을 false로 설정한다.
  // 6. true를 반환한다.
}
```

Swap in 과정의 핵심은 주어진 spte에서 읽어올 페이지의 swap index를 확인하여 swap block의 데이터를 물리 메모리에 로드하는 것이다. 이를 위한 일련의 과정의 위의 코드 블럭에 명기하였다. 하지만 위의 과정은 annoymous page에 해당하는 swap in 알고리즘이다. File-backed page의 경우에는, swap index를 찾아서 swap_table을 찾는 과정을 file을 읽어오는 과정으로 대체하면 된다.

#### Swap Out
```c 
static bool anon_swap_out (struct spte *page) {
  // 1. bitmap_scan_and_flip을 통해 페이지가 저장될 빈 자리를 찾는다. 즉, swap index를 찾는다.
  // 2. swap index를 참조하여 swap block에 페이지의 데이터를 작성한다.
  // 3. 페이지의 swap index를 앞서 구한 인덱스로 설정한다.
  // 4. 페이지를 할당 해제한다.
}
```
Swap out 과정의 경우, bitmap에서 적절한 빈 자리를 찾아서 해당 자리에 데이터를 작성하는 과정을 통해 이루어진다. 이때, 빈 자리는 first fit을 통해 찾도록 디자인하였다. 이 알고리즘은 annoymous page에 해당하는 swap otu 알고리즘이다. File-backed page의 경우, dirty bit를 확인하여 dirty한 페이지라면 swap block 대신 연결된 파일에 작성하는 방식으로 구현할 수 있다.

# On Process Termination

## Problem Description
프로세스가 종료할때 앞선 구현으로 할당된 자원들을 모두 해제해 메모리 누수를 방지한다. 또한 `lock`을 홀딩한 채로 종료될 시 deadlock이 발생 할 수 있으므로 종료시 보유중인 `lock`이 있다면 모두 `release`해야한다.

## Analysis
앞선 구현 내용에서 메모리 할당을 해제하는 부분이 부재한다. 또한 dirty bit이 `1`인 page의 경우 close 하면서 file에 새롭게 write 해줘야 한다.

## Solution

### Data Structure

#### Lock List

Lock List를 도입해 스레드, 즉 프로세스가 보유중인 lock을 관리하도록 한다.
```c
struct lock
  {
    ...
    struct list_elem list_elem; /* List element for locks list. */
  };
```
```c
struct thread
  {
    ...
    /* Shared between threads/synch.c and userprog/process.c. */
    struct list lock_list; /* List of locks acquired. */
    ...
  };
```
`lock`을 `acquire`하거나 `release`할 때 `lock_list`에 추가, 제거한다.

### Algorithms

#### MMF

프로세스가 종료될 때, 스레드의 `mmf_list`를 확인하여 현재 스레드가 가지고 있는 MMF들에 대한 할당 해제 과정이 이루어져야 한다. 

#### Tables

프로세스가 종료될 때, 프로세스의 `spt`, `frame_table`의 모든 자원을 할당 해제하도록 한다. 만약 dirty page가 존재할 경우 write 동작을 실행한다. 또한, swap table을 참조하여 현재 프로세스의 annoymous page에 대한 swap file을 모두 제거해야 한다.

#### Impl.
```c
void
process_exit (void)
{
  // ...
  
  // Free elements of MMF_LIST
  
  // Free elements of SPT, FRAME_TABLE
  // Write any dirty page
  
  // Releas all locks in LOCK_LIST

  cur->pcb->is_exited = true;
  sema_up (&(cur->pcb->sema_wait));
}
```