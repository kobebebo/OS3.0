#ifndef _MEMLAYOUT_H_
#define _MEMLAYOUT_H_

#include "types.h"

#define UART0    0x10000000L   // QEMU virt 上的 UART0
#define KERNBASE 0x80000000L   // 内核加载物理地址
#define PHYSTOP  (KERNBASE + 128*1024*1024L)  // 先假定只用前 128MB

#define PGSIZE   4096
#define MAXVA    (1L << (9+9+9+12-1))

#define PGROUNDUP(sz)  ((((uint64)(sz)) + PGSIZE - 1) & ~((uint64)PGSIZE - 1))
#define PGROUNDDOWN(a) (((uint64)(a)) & ~((uint64)PGSIZE - 1))

#endif
