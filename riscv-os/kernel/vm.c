#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "pmm.h"
#include "vm.h"

// -------- Sv39 相关宏 --------

#define PXMASK         0x1FFUL
#define PXSHIFT(level) (12 + 9 * (level))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// PTE 标志位
#define PTE_V (1L << 0)  // 有效
#define PTE_R (1L << 1)  // 可读
#define PTE_W (1L << 2)  // 可写
#define PTE_X (1L << 3)  // 可执行
#define PTE_U (1L << 4)  // 用户态访问
#define PTE_G (1L << 5)
#define PTE_A (1L << 6)
#define PTE_D (1L << 7)

#define PTE_FLAGS(pte) ((pte) & 0x3FFUL)
#define PTE_PA(pte)    (((pte) >> 10) << 12)
#define PA2PTE(pa)     ((((uint64)(pa)) >> 12) << 10)

static inline void
w_satp(uint64 x)
{
    asm volatile("csrw satp, %0" : : "r"(x));
}

static inline void
sfence_vma(void)
{
    asm volatile("sfence.vma zero, zero");
}

// MODE=8 表示 Sv39
#define MAKE_SATP(pagetable) \
    (((uint64)8 << 60) | ((((uint64)(pagetable)) >> 12) & ((1L << 44) - 1)))

// 简单本地 memset，避免依赖 libc
static void *
memset_local(void *dst, int c, uint64 n)
{
    uint8 *p = (uint8 *)dst;
    for (uint64 i = 0; i < n; i++)
        p[i] = (uint8)c;
    return dst;
}

// -------- 内核页表全局变量 --------

pagetable_t kernel_pagetable;

// -------- 基本页表操作 --------

// 从 pagetable 中查找 va 对应的 PTE，alloc!=0 时需要则分配中间页表
static pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk: va >= MAXVA");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE_PA(*pte);
        } else {
            if (!alloc)
                return 0;
            pagetable_t newpage = (pagetable_t)alloc_page();
            if (newpage == 0)
                return 0;
            memset_local(newpage, 0, PGSIZE);
            *pte = PA2PTE(newpage) | PTE_V;
            pagetable = newpage;
        }
    }

    return &pagetable[PX(0, va)];
}

// 映射单个页：va -> pa，要求两者都 4KB 对齐
static int
mappage(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
    if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0)
        panic("mappage: not aligned");

    pte_t *pte = walk(pagetable, va, 1);
    if (pte == 0)
        return -1;
    if (*pte & PTE_V)
        panic("mappage: remap");

    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0;
}

// 映射一段区域：[va, va+sz) -> [pa, pa+sz)
static void
kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
    uint64 a    = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(va + sz - 1);

    // 这里要求调用者保证 va/pa/size 都是页对齐的，
    // 否则就会在 mappage 中 panic。
    for (;;) {
        if (mappage(pagetable, a, pa, perm) != 0)
            panic("kvmmap: mappage failed");
        if (a == last)
            break;
        a  += PGSIZE;
        pa += PGSIZE;
    }
}

// 对外接口：创建空页表
pagetable_t
create_pagetable(void)
{
    pagetable_t pt = (pagetable_t)alloc_page();
    if (pt == 0)
        return 0;
    memset_local(pt, 0, PGSIZE);
    return pt;
}

// 对外接口：映射单个页（封装一下）
int
map_page(pagetable_t pt, uint64 va, uint64 pa, int perm)
{
    return mappage(pt, va, pa, perm);
}

// 链接脚本里导出的符号：代码段结束
extern char etext[];

// -------- 构建并启用内核页表 --------

void
kvminit(void)
{
    printf("kvminit: building kernel page table...\n");

    kernel_pagetable = create_pagetable();
    if (kernel_pagetable == 0)
        panic("kvminit: no memory for kernel_pagetable");

    // 1. 恒等映射整个内核物理内存 [KERNBASE, PHYSTOP)，R/W/X 都打开
    //    这样起始地址 KERNBASE 和大小 PHYSTOP-KERNBASE 都是页面对齐的。
    kvmmap(kernel_pagetable,
           KERNBASE,
           KERNBASE,
           PHYSTOP - KERNBASE,
           PTE_R | PTE_W | PTE_X);

    // 2. 设备内存：UART0（同样是恒等映射）
    kvmmap(kernel_pagetable,
           UART0,
           UART0,
           PGSIZE,
           PTE_R | PTE_W);

    printf("kvminit: kernel page table built.\n");
}

void
kvminithart(void)
{
    uint64 satp = MAKE_SATP(kernel_pagetable);

    printf("kvminithart: enabling Sv39 paging, satp=%p\n", (uint64)satp);

    w_satp(satp);
    sfence_vma();

    printf("kvminithart: paging enabled.\n");
}
