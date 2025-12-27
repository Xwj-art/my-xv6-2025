// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void pt(char* s) {
  printf("====================\n");
  printf("==========%s==========\n", s);
  printf("====================\n");
}
void paddr(int a) {
  printf("====================\n");
  printf("==========%d==========\n", a);
  printf("====================\n");
}

void freerange(void* pa_start, void* pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run* next;
};

struct {
  struct spinlock lock;
  struct run* freelist;
} kmem[NCPU];

void kinit() {
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void freerange(void* pa_start, void* pa_end) {
  char* p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  struct run* r;

  acquire(&kmem[0].lock);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE);
    r = (struct run*)p;
    r->next = kmem[0].freelist;
    kmem[0].freelist = r;
  }
  release(&kmem[0].lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void* pa) {
  push_off();
  int id = cpuid();
  pop_off();

  struct run* r;

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

void* ksteal(int id) {
  struct run* r;
  for (int i = 0; i < NCPU; i++) {
    if (i == id) continue;
    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if (r) {
      kmem[i].freelist = r->next;  // 如果找到的CPU上的freelist有空闲块，分配出去
      release(&kmem[i].lock);
      return (void*)r;
    }
    release(&kmem[i].lock);
  }
  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void* kalloc(void) {
  push_off();
  int id = cpuid();
  pop_off();
  struct run* r;
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r) {
    kmem[id].freelist = r->next;
    release(&kmem[id].lock);
  } else {
    // 如果r为空，那么也就是说没有空的memory了，需要steal其他CPU的freelist上的memory
    release(&kmem[id].lock);
    r = (struct run*)ksteal(id);
    if (!r) return 0;
  }

  if (r) memset((char*)r, 5, PGSIZE);  // fill with junk
  return (void*)r;
}
