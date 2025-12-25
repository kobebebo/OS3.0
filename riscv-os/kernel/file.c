// kernel/file.c
#include "types.h"
#include "printf.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "spinlock.h"

// 全局打开文件表
static struct {
    struct spinlock lock;
    struct file     file[NFILE];
} ftable;

// 初始化文件表
void
fileinit(void)
{
    initlock(&ftable.lock, "ftable");
}

// 分配一个新的 struct file
struct file *
filealloc(void)
{
    acquire(&ftable.lock);
    for (int i = 0; i < NFILE; i++) {
        if (ftable.file[i].ref == 0) {
            ftable.file[i].ref = 1;
            release(&ftable.lock);
            return &ftable.file[i];
        }
    }
    release(&ftable.lock);
    return 0;
}

// 增加引用计数
struct file *
filedup(struct file *f)
{
    acquire(&ftable.lock);
    if (f->ref < 1) {
        release(&ftable.lock);
        panic("filedup");
    }
    f->ref++;
    release(&ftable.lock);
    return f;
}

// 关闭一个打开文件
void
fileclose(struct file *f)
{
    struct file ff;

    acquire(&ftable.lock);
    if (f->ref < 1) {
        release(&ftable.lock);
        panic("fileclose");
    }

    f->ref--;
    if (f->ref > 0) {
        // 还有其它引用者，不真正关闭
        release(&ftable.lock);
        return;
    }

    // 这是最后一个引用：复制一份，清空表项再释放锁
    ff = *f;
    f->type     = FD_NONE;
    f->readable = 0;
    f->writable = 0;
    f->ip       = 0;
    f->off      = 0;
    release(&ftable.lock);

    // 真正释放底层资源
    if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
        begin_op();
        iput(ff.ip);    // iput 内部会根据 nlink/ref 决定是否释放 inode
        end_op();
    }
}

// 把文件的 stat 信息拷贝到“用户缓冲区”
int
filestat(struct file *f, uint64 addr)
{
    if (f->type != FD_INODE && f->type != FD_DEVICE) {
        return -1;
    }

    struct stat st;
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);

    // 当前实验没有真正的用户态地址空间，直接认为 addr 是内核地址
    struct stat *dst = (struct stat *)addr;
    *dst = st;
    return 0;
}

// 读取文件内容
int
fileread(struct file *f, uint64 addr, int n)
{
    if (!f->readable) {
        return -1;
    }

    if (f->type == FD_INODE || f->type == FD_DEVICE) {
        ilock(f->ip);
        int r = readi(f->ip, 1 /* user_dst */, addr, f->off, n);
        if (r > 0) {
            f->off += r;
        }
        iunlock(f->ip);
        return r;
    }

    return -1;
}

// 写入文件内容：自动按事务大小分段
int
filewrite(struct file *f, uint64 addr, int n)
{
    if (!f->writable) {
        return -1;
    }
    if (f->type != FD_INODE && f->type != FD_DEVICE) {
        return -1;
    }

    int tot = 0;

    while (tot < n) {
        int n1 = n - tot;
        if (n1 > MAXOPBLOCKS * BSIZE) {
            n1 = MAXOPBLOCKS * BSIZE;
        }

        begin_op();
        ilock(f->ip);
        int r = writei(f->ip, 1 /* user_src */, addr + tot, f->off, n1);
        if (r > 0) {
            f->off += r;
        }
        iunlock(f->ip);
        end_op();

        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;
        }
        tot += r;
    }

    return (tot == n) ? n : -1;
}
