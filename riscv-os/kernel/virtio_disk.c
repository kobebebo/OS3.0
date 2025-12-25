// kernel/virtio_disk.c
// 简化版“磁盘驱动”：不真正使用 QEMU 的 virtio 设备，
// 而是用一块内存数组来模拟磁盘。
// 这样就能让文件系统的其它层（bio/fs/log）正常工作，
// 方便在教学/实验环境中调试。

#include "types.h"
#include "fs.h"
#include "printf.h"
#include "fs_debug.h"

uint64 disk_read_count = 0;
uint64 disk_write_count = 0;


// ----------- 配置：模拟磁盘大小 -----------

// 总块数（包括 super/log/inode/bitmap/data 等）
// 可以根据需要调整，只要不超过内存即可。
#define FSSIZE 1024

// 用一块全局 BSS 数组模拟磁盘：FSSIZE 个块，每块 BSIZE 字节
static unsigned char ramdisk[FSSIZE][BSIZE];

// 简单的 memmove 实现，避免依赖 libc
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

// 在“内存磁盘”的 blockno 位置读写一个块
// write=0：把磁盘内容读入 b->data
// write!=0：把 b->data 写入磁盘
void
virtio_disk_rw(struct buf *b, int write)
{
    if (write)
    disk_write_count++;
else
    disk_read_count++;

    if (b->blockno >= FSSIZE) {
        panic("virtio_disk_rw: blockno out of range");
    }

    if (write) {
        // 写：把 buf->data 写入 ramdisk
        memmove_local(ramdisk[b->blockno], b->data, BSIZE);
    } else {
        // 读：把 ramdisk 的内容读到 buf->data
        memmove_local(b->data, ramdisk[b->blockno], BSIZE);
    }
}

// 初始化“磁盘”：
// 这里同时负责在 block 1 写入超级块，建立一个全新的空文件系统。
// 布局与 fs.h 中的 superblock 定义相匹配。
void
virtio_disk_init(void)
{
    // 把整个 ramdisk 清零
    for (uint32 i = 0; i < FSSIZE; i++) {
        for (uint32 j = 0; j < BSIZE; j++) {
            ramdisk[i][j] = 0;
        }
    }

    // 计算一个合理的文件系统布局
    struct superblock sb;

    sb.magic = FSMAGIC;
    sb.size  = FSSIZE;   // 总块数

    sb.nlog      = LOGSIZE;  // 使用 fs.h 中定义的 LOGSIZE
    sb.logstart  = 2;        // 约定：块 0 保留，块 1 superblock，块 2..logstart+nlog-1 为日志

    sb.ninodes   = 200;      // 可根据需要调整 inode 总数
    uint32 ninodeblocks = (sb.ninodes + IPB - 1) / IPB;

    sb.inodestart = sb.logstart + sb.nlog;             // inode 表起始块
    sb.bmapstart  = sb.inodestart + ninodeblocks;      // 位图块起始块

    // 位图块数量（当前 FSSIZE 不大，一般只需要 1 块）
    uint32 nbitmapblocks = (sb.size + BPB - 1) / BPB;

    uint32 data_start = sb.bmapstart + nbitmapblocks;  // 数据块起始编号
    sb.nblocks = sb.size - data_start;                 // 数据块数量

    // 把 superblock 写入 block 1
    memmove_local(ramdisk[1], &sb, sizeof(sb));

    // 初始化位图：把 [0, data_start) 这些“元数据块”标记为已占用，
    // 防止 balloc() 把它们分配给文件数据。
    // 位图块本身位于 sb.bmapstart（这里只有 1 块）。
    unsigned char *bits = ramdisk[sb.bmapstart];

    for (uint32 bno = 0; bno < data_start; bno++) {
        uint32 bi = bno % BPB;
        uint32 byte_index = bi / 8;
        uint32 bit_index  = bi % 8;
        bits[byte_index] |= (1 << bit_index);
    }

    printf("virtio_disk_init: RAM disk fs created: size=%d nblocks=%d ninodes=%d\n",
           sb.size, sb.nblocks, sb.ninodes);
}
