// kernel/log.c

#include "types.h"
#include "printf.h"
#include "fs.h"      // struct logheader / struct log / LOGSIZE 等

// 低层磁盘读写接口（在 virtio_disk.c 中实现）
extern void virtio_disk_rw(struct buf *b, int write);

// 在 fs.h 里有 extern struct log log; 这里只做定义
struct log log;

// 简单 memmove，避免依赖 libc
static void *
memmove_local(void *dst, const void *src, uint32 n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        for (uint32 i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (uint32 i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

// 从磁盘读取日志头到内存 log.lh
static void
read_head(void)
{
    struct buf *b = bread(log.dev, log.start);
    struct logheader *hd = (struct logheader *)(b->data);

    log.lh.n = hd->n;
    if (log.lh.n < 0 || log.lh.n > LOGSIZE) {
        panic("read_head: bad n");
    }
    for (int i = 0; i < log.lh.n; i++) {
        log.lh.block[i] = hd->block[i];
    }

    brelse(b);
}

// 把内存 log.lh 写回磁盘上的日志头块
static void
write_head(void)
{
    struct buf *b = bread(log.dev, log.start);
    struct logheader *hd = (struct logheader *)(b->data);

    hd->n = log.lh.n;
    for (int i = 0; i < log.lh.n; i++) {
        hd->block[i] = log.lh.block[i];
    }

    // 注意：这里不能再调用 bwrite()，否则会触发 log_write 再回到这里，递归死循环
    virtio_disk_rw(b, 1);
    brelse(b);
}

// 把日志区域中的数据块“安装”到它们真正对应的位置上
static void
install_trans_from_log(void)
{
    for (int i = 0; i < log.lh.n; i++) {
        uint32 bno = log.lh.block[i];

        // 日志中的第 i 个数据块在 log.start+1+i
        struct buf *lb = bread(log.dev, log.start + 1 + i);
        struct buf *db = bread(log.dev, bno);

        memmove_local(db->data, lb->data, BSIZE);

        // 同样直接用底层驱动写盘，不能走 bwrite()
        virtio_disk_rw(db, 1);

        brelse(lb);
        brelse(db);
    }
}

// 开机或挂载时的恢复流程
static void
recover_from_log(void)
{
    read_head();

    if (log.lh.n > 0) {
        printf("log: recovering %d blocks from log...\n", log.lh.n);

        install_trans_from_log();

        // 清空日志头
        log.lh.n = 0;
        write_head();
    }
}

// 初始化日志系统：由 fs_init() 调用。
void
initlog(int dev, struct superblock *sb)
{
    log.dev   = dev;
    log.start = sb->logstart;
    log.size  = sb->nlog;

    log.outstanding = 0;
    log.committing  = 0;
    log.lh.n        = 0;

    printf("log: init: start=%d, size=%d\n", log.start, log.size);

    recover_from_log();
}

// 开始一个文件系统操作
void
begin_op(void)
{
    if (log.committing) {
        panic("begin_op: committing");
    }
    log.outstanding++;
}

// 真正执行一次提交
static void
commit(void)
{
    if (log.lh.n > 0) {
        // 1) 把日志头写到磁盘，标记“日志有效”
        write_head();

        // 2) 真正把日志中的块安装到文件系统的位置
        install_trans_from_log();

        // 3) 清空日志头并写回磁盘，表示“事务已经完成”
        log.lh.n = 0;
        write_head();
    }
}

// 结束一次文件系统操作
void
end_op(void)
{
    if (log.outstanding < 1) {
        panic("end_op: no outstanding");
    }

    log.outstanding--;
    if (log.outstanding == 0) {
        // 当前没有别的文件系统操作在进行，可以安全提交
        log.committing = 1;
        commit();
        log.committing = 0;
    }
}

// 把一个即将被修改的缓冲区 b 纳入日志系统
void
log_write(struct buf *b)
{
    // 如果不在事务内，直接“裸写”到磁盘，不占用日志空间。
    // 这里一定不能调用 bwrite()，否则会回到 log_write 形成递归。
    if (log.outstanding < 1) {
        virtio_disk_rw(b, 1);
        return;
    }

    // 查找该块是否已经在当前事务的日志列表中
    int i;
    for (i = 0; i < log.lh.n; i++) {
        if ((uint32)log.lh.block[i] == b->blockno) {
            break;
        }
    }

    if (i == log.lh.n) {
        // 这是一个新的块记录
        if (log.lh.n >= log.size || log.lh.n >= LOGSIZE) {
            panic("log_write: log full");
        }
        log.lh.block[i] = b->blockno;
        log.lh.n++;
    }

    // 把数据写入对应的日志数据块（log.start+1+i），
    // 注意这里同样直接调用底层磁盘驱动，不能调用 bwrite()
    struct buf *lb = bread(log.dev, log.start + 1 + i);
    memmove_local(lb->data, b->data, BSIZE);
    virtio_disk_rw(lb, 1);
    brelse(lb);
}
