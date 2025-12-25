#ifndef _VM_H_
#define _VM_H_

#include "types.h"

typedef uint64  pte_t;
typedef uint64* pagetable_t;

// 全局内核页表指针
extern pagetable_t kernel_pagetable;

// 构建内核页表（恒等映射）但尚未写入 satp
void kvminit(void);

// 把 kernel_pagetable 写入 satp，执行 sfence.vma，正式开启 Sv39 分页
void kvminithart(void);

// 附带导出两个通用接口（和实验手册中的接口一致）
pagetable_t create_pagetable(void);
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm);

#endif
