#include "defs.h"
#include "elf.h"
#include "fs.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

void paddr(uint64 a) {
  printf("==============%lx============\n", a);
}
void pt(char *s) {
  printf("================%s===============\n", s);
}
// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();

  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) {
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      if (PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0) return 0;
  if ((*pte & PTE_V) == 0) return 0;
  if ((*pte & PTE_U) == 0) return 0;
  pa = PTE2PA(*pte);
  return pa;
}
/*
page table 0x0000000087f22000
 ..0x0000000000000000: pte 0x0000000021fc7801 pa 0x0000000087f1e000
 .. ..0x0000000000000000: pte 0x0000000021fc7401 pa 0x0000000087f1d000
 .. .. ..0x0000000000000000: pte 0x0000000021fc7c5b pa 0x0000000087f1f000
 .. .. ..0x0000000000001000: pte 0x0000000021fc705b pa 0x0000000087f1c000
 .. .. ..0x0000000000002000: pte 0x0000000021fc6cd7 pa 0x0000000087f1b000
 .. .. ..0x0000000000003000: pte 0x0000000021fc6807 pa 0x0000000087f1a000
 .. .. ..0x0000000000004000: pte 0x0000000021fc64d7 pa 0x0000000087f19000
 ..0x0000003fc0000000: pte 0x0000000021fc8401 pa 0x0000000087f21000
 .. ..0x0000003fffe00000: pte 0x0000000021fc8001 pa 0x0000000087f20000
 .. .. ..0x0000003fffffd000: pte 0x0000000021fd4813 pa 0x0000000087f52000
 .. .. ..0x0000003fffffe000: pte 0x0000000021fd00c7 pa 0x0000000087f40000
 .. .. ..0x0000003ffffff000: pte 0x0000000020001c4b pa 0x0000000080007000
 */
// #if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)
void nextlevel(pagetable_t pagetable, int level, uint64 base) {
  for (uint64 i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if (!(pte & PTE_V)) continue;
    switch (level) {
    case 2:
      printf("..");
      break;
    case 1:
      printf(".. ..");
      break;
    case 0:
      printf(".. .. ..");
      break;
    }
    printf("%p: pte %p pa %p\n", (void *)PA2VA(level, i, base), (void *)pte, (void *)PTE2PA(pte));
    // 如果非叶子，继续递归下一级
    if (level > 0 && (PTE_FLAGS(pte) == PTE_V))
      nextlevel((pagetable_t)(PTE2PA(pte)), level - 1, PA2VA(level, i, base));
  }
}

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  nextlevel(pagetable, 2, 0);
}
// #endif

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0) panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
pte_t *superpagemap(pagetable_t pagetable, uint64 va) {
  pte_t *pte = &pagetable[PX(2, va)];
  if (*pte & PTE_V) {
    pagetable = (pagetable_t)PTE2PA(*pte);
  } else {
    if ((pagetable = (pde_t *)kalloc()) == 0) return 0;
    memset(pagetable, 0, PGSIZE);
    *pte = PA2PTE(pagetable) | PTE_V;
  }
  return &pagetable[PX(1, va)];
}

int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0) panic("mappages: size not aligned");

  if (size == 0) panic("mappages: size");
  a = va;
  // 分配超级页
  if (size == SUPERPGSIZE) {
    pte = superpagemap(pagetable, a);
    *pte = (PA2PTE(pa) | perm | PTE_V);
    return 0;
  }
  while (size != 0) {
    pte = walk(pagetable, a, 1);
    *pte = (PA2PTE(pa) | perm | PTE_V);
    size -= PGSIZE;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap_high(pagetable_t pagetable, uint64 a) {
  pte_t *pte = walk(pagetable, a, 0);
  *pte = 0;
}
void uvmunmap(pagetable_t pagetable, uint64 newsz, uint64 oldsz, int do_free) {
  // 记录超页的物理地址，可能会用来降级
  uint64 a, pa;
  pte_t *pte;
  int sz = PGSIZE;
  int perms = 0;

  if ((newsz % PGSIZE) != 0) {
    panic("uvmunmap: not aligned");
  }
  // 新的大于旧的，无需删除索引
  if (newsz >= oldsz) return;
  for (a = newsz; a < oldsz; a += sz) {
    // 首先判断这是否是一个超页
    pte = superpagemap(pagetable, a);
    if (pte == 0)
      continue;  // 二级页表未写入过数据
    else {
      // 判断是否是超页还是普通页
      if (PTE_FLAGS(*pte) & PTE_V) {
        if (PTE_FLAGS(*pte) == PTE_V) {  // 这是个普通页
          // 查看普通页是否有效
          pte = walk(pagetable, a, 0);
          if (pte == 0) continue;
          if (*pte & PTE_V) {
            sz = PGSIZE;
          } else
            continue;
        } else {
          // 超页
          sz = SUPERPGSIZE;
          // 并保存它的perms，pa，因为如果要降级，会用到
          perms = PTE_FLAGS(*pte);
          pa = PTE2PA(*pte);
        }
      } else
        continue;
    }

    if (PTE_FLAGS(*pte) == PTE_V) panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      if (sz == PGSIZE)
        kfree((void *)pa);
      else
        superfree((void *)pa);
    }
    *pte = 0;
  }
  // 如果最后a>newsz，超过了我们要取消映射的范围，那么
  // 我们需要将newsz到a这一部分重新映射起来
  if (a > oldsz) {
    // 将超页取消映射以后，我们要重新建立普通页的映射
    if (do_free) {
      // 如果释放了物理内存，那么我们需要将其重新分配，从oldsz到a
      uvmalloc(pagetable, oldsz, a, perms);
    } else {
      // 如果只是简单的取消了映射
      mappages(pagetable, oldsz, a - oldsz, pa, perms);
    }
  }
}

// 按照指定页的大小分配页
uint64 pagemalloc(pagetable_t pagetable, uint64 a, uint64 pgsz, int xperm) {
  char *mem;
  if (pgsz == SUPERPGSIZE) {
    if ((mem = superalloc()) == 0) {
      return 1;
    }
    memset(mem, 0, pgsz);
    if (mappages(pagetable, a, pgsz, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
      superfree(mem);
      uvmdealloc(pagetable, a + pgsz, a);
      return 1;
    }
  } else {
    if ((mem = kalloc()) == 0) {
      return 1;
    }
    memset(mem, 0, pgsz);
    if (mappages(pagetable, a, pgsz, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
      kfree(mem);
      uvmdealloc(pagetable, a + pgsz, a);
      return 1;
    }
  }
  return 0;
}
// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  if (newsz < oldsz) return oldsz;
  /*
  #define SUPERPGSIZE (2 * (1 << 20)) // bytes per page
  #define SUPERPGROUNDUP(sz)  (((sz)+SUPERPGSIZE-1) & ~(SUPERPGSIZE-1))
  #define SUPERPGROUNDDOWN(sz) (SUPERPGROUNDUP(sz)-SUPERPGSIZE)
  */
  // 页对齐，从这个位置开始
  oldsz = PGROUNDUP(oldsz);
  uint64 a = oldsz;
  uint64 superbegin = SUPERPGROUNDUP(oldsz), superend = SUPERPGROUNDDOWN(newsz);

  if (superbegin < superend) {
    // 先分配普通页，若需要分配超级页，则分到超级页对齐的地方就停止，开始分配超级页
    // 分配完超级页以后，继续分配普通页直到分配完毕
    for (; a < superbegin; a += PGSIZE)
      if (pagemalloc(pagetable, a, PGSIZE, xperm) != 0) goto err;
    for (; a < superend; a += SUPERPGSIZE)
      if (pagemalloc(pagetable, a, SUPERPGSIZE, xperm) != 0) goto err;
  }
  pt("alloc begin");
  for (; a < newsz; a += PGSIZE)
    if (pagemalloc(pagetable, a, PGSIZE, xperm) != 0) goto err;
  pt("alloc end");
  return newsz;
err:
  // 分配页失败，则从oldsz开始释放页直到地址a的位置，返回0
  uvmdealloc(pagetable, a, oldsz);
  return 0;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    uvmunmap(pagetable, PGROUNDUP(newsz), PGROUNDUP(oldsz), 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0) uvmunmap(pagetable, 0, sz, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc = PGSIZE;

  for (i = 0; i < sz; i += szinc) {
    pte = superpagemap(old, i);
    if (pte != 0 && PTE_FLAGS(*pte) != PTE_V) {
      szinc = SUPERPGSIZE;
      goto copy;
    }
    pte = walk(old, i, 0);
    if (pte != 0 && (*pte & PTE_V) == 1) {
      szinc = PGSIZE;
      goto copy;
    }
  copy:
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if (szinc == SUPERPGSIZE) {
      if ((mem = superalloc()) == 0) goto err;
      memmove(mem, (char *)pa, SUPERPGSIZE);
      if (mappages(new, i, SUPERPGSIZE, (uint64)mem, flags) != 0) {
        superfree(mem);
        goto err;
      }
    } else {
      if ((mem = kalloc()) == 0) goto err;
      memmove(mem, (char *)pa, PGSIZE);
      if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
        kfree(mem);
        goto err;
      }
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA) return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if ((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }

    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0) return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max) n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
// vmfault缺页处理，分配的虚拟地址空间大小为为p->sz，但是分配得到的页可能会被换出，所以物理页可能并未分配需要重新分配
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
  uint64 mem;
  struct proc *p = myproc();

  // 访问范围错误
  if (va >= p->sz) return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va)) {
    return 0;
  }
  // 这个实验好像没有用到懒页分配，直接返回1试一试。
  // 判断va是否在超级页范围内
  uint64 base_va = SUPERPGROUNDDOWN(va);
  if (base_va + SUPERPGSIZE <= p->sz) {
    mem = (uint64)superalloc();
    if (mem == 0) return 0;
    memset((void *)mem, 0, SUPERPGSIZE);
    if (mappages(p->pagetable, base_va, SUPERPGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
      superfree((void *)mem);
      return 0;
    }
  } else {
    mem = (uint64)kalloc();
    if (mem == 0) return 0;
    memset((void *)mem, 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
      kfree((void *)mem);
      return 0;
    }
  }
  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  // 先检查是否是超级页进行映射
  pte = superpagemap(pagetable, va);
  if (pte != 0 && (*pte & PTE_V) && (PTE_FLAGS(*pte) != PTE_V)) return 1;  // 叶子，已经映射超级页
  pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V) {
    return 1;
  }
  return 0;
}

#ifdef LAB_PGTBL
pte_t *pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
