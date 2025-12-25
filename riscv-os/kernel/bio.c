// kernel/bio.c
#include "types.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "printf.h"
#include "fs_debug.h"

uint64 buffer_cache_hits = 0;
uint64 buffer_cache_misses = 0;


// 底层 virtio 磁盘接口（由实验框架提供）
extern void virtio_disk_rw(struct buf *b, int write);
extern void virtio_disk_init(void);

// 块缓存全局状态：一个双向环形链表 + 固定数组
static struct {
    struct spinlock lock;      // 保护整个缓存结构
    struct buf      buf[NBUF]; // 实际的缓存块数组
    struct buf      head;      // 伪头结点（不存数据，只用于链表）
} bcache;

// 内部辅助：获取一个指定 (dev, blockno) 的 buf
static struct buf *
bget(uint32 dev, uint32 blockno)
{
    struct buf *b;

    acquire(&bcache.lock);

    // 1. 在缓存中查找是否已经存在
    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 2. 没找到，则从 LRU 尾部找一个 refcnt == 0 的 buf 复用
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev     = dev;
            b->blockno = blockno;
            b->valid   = 0;
            b->disk    = 0;
            b->refcnt  = 1;

            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    release(&bcache.lock);
    panic("bget: no free buffer");
    return 0;
}

// 初始化块缓存：在内核启动时调用一次
void
binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // 初始化双向环形链表 head
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    // 把所有 buf 挂到 head 后面（MRU 位置）
    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->valid  = 0;
        b->disk   = 0;
        b->dev    = 0;
        b->blockno = 0;
        b->refcnt = 0;
        initsleeplock(&b->lock, "buffer");

        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    // 初始化底层 virtio 磁盘
    virtio_disk_init();
}

// 读取 (dev, blockno) 对应的磁盘块
struct buf *
bread(uint32 dev, uint32 blockno)
{
    struct buf *b = bget(dev, blockno);

if (!b->valid) {
    buffer_cache_misses++;
    virtio_disk_rw(b, 0);   // 读盘
    b->valid = 1;
} else {
    buffer_cache_hits++;
}
return b;

}

// 标记 buf 需要写入磁盘，并交给日志系统记录
void
bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock)) {
        panic("bwrite: buf not locked");
    }

    b->disk = 1;      // 标记该 buf 数据已经被修改
    log_write(b);     // 记录到日志中（写前日志，只记录“哪个块会被写回”）
}

// 释放对 buf 的持有
void
brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock)) {
        panic("brelse: buf not locked");
    }

    releasesleep(&b->lock);

    acquire(&bcache.lock);

    if (b->refcnt < 1) {
        panic("brelse: refcnt < 1");
    }

    b->refcnt--;

    // 没有使用者时，把该 buf 移动到 LRU 头部（MRU）
    if (b->refcnt == 0) {
        // 从当前位置摘链
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // 插入到 head 后面
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    release(&bcache.lock);
}
