// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PNUM (PHYSTOP >> 12)
#define PA2INDEX(pa) (((uint64)(pa)) >> 12)

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct {
  struct spinlock lock;  // 全局锁
  uint8 counts[PNUM];    // 引用计数数组
} refs;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void kinit() {
  initlock(&refs.lock, "refs");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    refs.counts[PA2INDEX(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP) panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  int ref;
  acquire(&refs.lock);
  ref = --(refs.counts[PA2INDEX(pa)]);
  release(&refs.lock);
  // 如果ref>0，返回；否则释放
  if (ref > 0) return;

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

// 分配内存时，设置引用计数为1
void *kalloc() {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) kmem.freelist = r->next;
  release(&kmem.lock);

  acquire(&refs.lock);
  refs.counts[PA2INDEX(r)] = 1;
  release(&refs.lock);
  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}

// 针对uvmcopy只添加引用计数来写一个函数
void adjustref(uint64 pa) {
  acquire(&refs.lock);
  ++refs.counts[PA2INDEX(pa)];
  release(&refs.lock);
}

// 用于判定当前是否为最后一个引用的cow页// 若是，则设置pte_c=0，pte_w=1
uint lastref(uint64 pa) {
  int ref;
  acquire(&refs.lock);
  ref = refs.counts[PA2INDEX(pa)];
  release(&refs.lock);
  return ref == 1;
}
