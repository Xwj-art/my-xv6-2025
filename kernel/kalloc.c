// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PNUM (PHYSTOP / PGSIZE)

void freerange(void* pa_start, void* pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run* next;
};

struct {
  struct spinlock lock;
  struct run* freelist;
} kmem;

struct {
  struct spinlock reflock;
  uint8 ref[PNUM];
} refs;

void kinit() {
  initlock(&kmem.lock, "kmem");
  initlock(&refs.reflock, "refs");
  freerange(end, (void*)PHYSTOP);
}

void freerange(void* pa_start, void* pa_end) {
  char* p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for (int i = 0; i < PNUM; i++) {
    refs.ref[i] = 1;
  }
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void* pa) {
  struct run* r;

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  uint64 index = (uint64)pa / PGSIZE;

  // 引用计数-1，若减为0，释放
  acquire(&refs.reflock);
  --refs.ref[index];
  if (refs.ref[index] > 0) {
    release(&refs.reflock);
    return;
  }
  release(&refs.reflock);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
  printf("kfree: pa=%p, ref=%d\n", pa, refs.ref[index]);
}

// 调整引用计数+1
void addref(uint64 pa) {
  uint64 index = pa / PGSIZE;
  acquire(&refs.reflock);
  ++refs.ref[index];
  release(&refs.reflock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void* kalloc(void) {
  struct run* r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) kmem.freelist = r->next;
  release(&kmem.lock);

  uint64 index;
  if (r) {
    index = (uint64)r / PGSIZE;
    acquire(&refs.reflock);
    refs.ref[index] = 1;
    release(&refs.reflock);
  }
  memset((char*)r, 5, PGSIZE);  // fill with junk
  printf("kalloc: pa=%p, ref=%d\n", r, refs.ref[index]);

  return (void*)r;
}

// 获取引用计数
uint64 subref(uint64 pa) {
  acquire(&refs.reflock);
  uint64 ref = --refs.ref[pa / PGSIZE];
  release(&refs.reflock);
  return ref;
}
uint64 getref(uint64 pa) {
  acquire(&refs.reflock);
  uint64 ref = refs.ref[pa / PGSIZE];
  release(&refs.reflock);
  return ref;
}
