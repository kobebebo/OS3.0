#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "pmm.h"

// 空闲页链表的节点，直接放在页本身上
struct run {
    struct run *next;
};

static struct {
    struct run *freelist;
} kmem;

// 由链接脚本提供：内核结束地址（代码+数据+BSS+栈）之后就是可分配物理内存
extern char kernel_end[];

void
free_page(void *pa)
{
    struct run *r;
    uint64 addr = (uint64)pa;

    // 简单的合法性检查
    if (addr % PGSIZE != 0 || addr < KERNBASE || addr >= PHYSTOP) {
        panic("free_page: bad address");
    }

    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
}

void *
alloc_page(void)
{
    struct run *r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
    }
    return (void*)r;   // 返回物理地址（目前是恒等映射，可直接当作虚拟地址用）
}

// 初始化物理内存管理器：
// 从 kernel_end 开始，一直到 PHYSTOP，把每个物理页挂到空闲链表中。
void
pmm_init(void)
{
    uint64 pa_start = PGROUNDUP((uint64)kernel_end);
    uint64 pa_end   = PHYSTOP;

    printf("pmm_init: kernel_end=%p, PHYSTOP=%p\n",
           (uint64)kernel_end, (uint64)PHYSTOP);

    for (uint64 p = pa_start; p + PGSIZE <= pa_end; p += PGSIZE) {
        free_page((void*)p);
    }

    printf("pmm_init: free pages from %p to %p\n",
           (uint64)pa_start, (uint64)pa_end);
}
