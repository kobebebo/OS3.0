// include/sleeplock.h
#ifndef _SLEEPLOCK_H_
#define _SLEEPLOCK_H_

#include "types.h"
#include "spinlock.h"

// 睡眠锁（简化版）：
// - 正式的睡眠锁会在锁被占用时 sleep/wakeup；
// - 本实验单核 + 简化调度，这里用“内部自旋 + 标志位”来模拟语义。
struct sleeplock {
    struct spinlock lk;   // 内部自旋锁，保护下面的状态
    char *name;           // 名字，方便调试
    int  locked;          // 0=未持有，1=已持有
};

// 初始化睡眠锁
static inline void
initsleeplock(struct sleeplock *slk, char *name)
{
    initlock(&slk->lk, "sleeplock");
    slk->name   = name;
    slk->locked = 0;
}

// 加锁：自旋等待 locked 变为 0，再设置为 1
static inline void
acquiresleep(struct sleeplock *slk)
{
    for (;;) {
        acquire(&slk->lk);
        if (!slk->locked) {
            slk->locked = 1;
            release(&slk->lk);
            break;
        }
        // 已被占用：释放内部自旋锁，稍后重试（简单自旋，不真正 sleep）
        release(&slk->lk);
    }
}

// 解锁
static inline void
releasesleep(struct sleeplock *slk)
{
    acquire(&slk->lk);
    slk->locked = 0;
    release(&slk->lk);
}

// 是否持有该睡眠锁
static inline int
holdingsleep(struct sleeplock *slk)
{
    int r;

    acquire(&slk->lk);
    r = slk->locked;
    release(&slk->lk);

    return r;
}

#endif // _SLEEPLOCK_H_
