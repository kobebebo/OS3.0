#ifndef _PMM_H_
#define _PMM_H_

#include "types.h"

// 初始化物理内存管理器（建立空闲页链表）
void pmm_init(void);

// 分配/释放一个物理页（4KB），返回物理地址（恒等映射下也可当作虚拟地址用）
void *alloc_page(void);
void free_page(void *pa);

#endif
