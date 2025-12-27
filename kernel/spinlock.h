// Mutual exclusion lock.
struct spinlock {
  uint locked;  // Is the lock held?

  // For debugging:
  char* name;       // Name of lock.
  struct cpu* cpu;  // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#ifdef LAB_LOCK
// Reader-writer lock.
/*
struct rwspinlock {
  // Replace this with your implementation.
  struct spinlock l;
  int r_count;  // 读者计数
  int w_count;  // 写者计数
  int flag;     // 锁是否被写者持有
};
*/
// kernel/spinlock.h
struct rwspinlock {
  uint readers;          // 当前读者数量
  uint writer;           // 是否有写者持有锁 (0/1)
  uint waiting_writers;  // 等待的写者数量
  struct spinlock lock;  // 保护内部状态的自旋锁
  char* name;            // 调试名称
};
#endif
