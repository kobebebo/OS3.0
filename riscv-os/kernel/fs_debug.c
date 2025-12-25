// kernel/fs_debug.c
// 实验八：文件系统诊断与一致性检查（fsck-lite + 统计信息）

#include "types.h"
#include "printf.h"
#include "fs.h"
#include "fs_debug.h"

// fs.c 里定义的全局超级块与 inode cache
extern struct superblock sb;
extern struct inode_cache icache;

// ---------------- 小工具：避免依赖 libc ----------------

static void *
memset_local(void *dst, int c, uint32 n)
{
    unsigned char *p = (unsigned char *)dst;
    for (uint32 i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return dst;
}

// 读位图：bno 这个块是否在 bitmap 里被标记为 “已分配”
static int
bitmap_isset(uint32 dev, uint32 bno)
{
    struct buf *b = bread(dev, BBLOCK(bno, sb));
    uint32 bi = bno % BPB;                 // 在该 bitmap block 内的 bit 索引
    uint32 byte_index = bi / 8;
    uint32 bit_index  = bi % 8;
    unsigned char mask = (unsigned char)(1 << bit_index);

    int ret = ((unsigned char)b->data[byte_index] & mask) ? 1 : 0;
    brelse(b);
    return ret;
}

// 计算 bitmap 有多少块
static uint32
calc_nbitmap(void)
{
    return (sb.size + BPB - 1) / BPB;
}

// 计算 data 区起始块号：bitmap 之后就是 data blocks
static uint32
calc_datastart(void)
{
    return sb.bmapstart + calc_nbitmap();
}

// 扫描 bitmap，统计 data 区空闲块数量
static int
count_free_blocks(void)
{
    uint32 dev = ROOTDEV;
    uint32 datastart = calc_datastart();
    int free = 0;

    for (uint32 bno = datastart; bno < sb.size; bno++) {
        if (bitmap_isset(dev, bno) == 0) {
            free++;
        }
    }
    return free;
}

// 扫描 dinode，统计空闲 inode 数量（type==T_UNUSED）
static int
count_free_inodes(void)
{
    uint32 dev = ROOTDEV;
    int free = 0;

    for (uint32 inum = 1; inum < sb.ninodes; inum++) {
        struct buf *b = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode *)b->data + (inum % IPB);
        if (dip->type == T_UNUSED) {
            free++;
        }
        brelse(b);
    }
    return free;
}

// ---------------- 对外调试接口 ----------------

void
debug_disk_io(void)
{
    printf("=== Disk I/O Statistics ===\n");
    printf("Disk reads : %u\n", disk_read_count);
    printf("Disk writes: %u\n", disk_write_count);
}

void
debug_inode_usage(void)
{
    printf("=== Inode Cache Usage (ref>0) ===\n");
    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &icache.inode[i];
        if (ip->ref > 0) {
            printf("icache[%d]: dev=%u inum=%u ref=%d valid=%d type=%d size=%u\n",
                   i, ip->dev, ip->inum, ip->ref, ip->valid, ip->type, ip->size);
        }
    }
}

void
debug_filesystem_state(void)
{
    printf("=== Filesystem Debug Info ===\n");
    printf("Superblock: size=%u nblocks=%u ninodes=%u nlog=%u\n",
           sb.size, sb.nblocks, sb.ninodes, sb.nlog);
    printf("Layout: logstart=%u inodestart=%u bmapstart=%u datastart=%u\n",
           sb.logstart, sb.inodestart, sb.bmapstart, calc_datastart());

    int freeb = count_free_blocks();
    int freei = count_free_inodes();

    printf("Free blocks (data region): %d\n", freeb);
    printf("Free inodes             : %d\n", freei);

    printf("Buffer cache hits  : %u\n", buffer_cache_hits);
    printf("Buffer cache misses: %u\n", buffer_cache_misses);

    debug_disk_io();
}

// ---------------- fsck-lite：一致性检查 ----------------
//
// 检查点：
// 1) inode 引用的数据块是否越界 / 是否落入 metadata 区（非法）
// 2) 是否出现 “同一个块被多个 inode 引用”（重复引用）
// 3) inode 引用的块，在 bitmap 中必须是 allocated
//
// 返回 0 表示 OK；返回 -1 表示发现问题（会打印具体错误）
int
fsck_lite(void)
{
    printf("=== fsck_lite: start ===\n");

    uint32 dev = ROOTDEV;
    uint32 datastart = calc_datastart();

    // 为了简单：假设 fs size 不会超过 4096 blocks（你现在是 1024）
    // 如果未来扩展，可改成动态分配或增大常量。
    enum { FSCK_MAXBLOCKS = 4096 };
    static unsigned char used[FSCK_MAXBLOCKS];

    if (sb.size > FSCK_MAXBLOCKS) {
        printf("fsck_lite: WARNING: sb.size=%u > %u, check range truncated.\n",
               sb.size, (uint32)FSCK_MAXBLOCKS);
    }

    uint32 limit = (sb.size < FSCK_MAXBLOCKS) ? sb.size : (uint32)FSCK_MAXBLOCKS;
    memset_local(used, 0, limit);

    int errors = 0;

    // 扫描所有磁盘 inode
    for (uint32 inum = 1; inum < sb.ninodes; inum++) {
        struct buf *b = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode *)b->data + (inum % IPB);

        if (dip->type == T_UNUSED) {
            brelse(b);
            continue;
        }

        // 检查一个块号 addr
        auto void check_addr(uint32 addr, const char *what) {
            if (addr == 0) return;

            if (addr >= limit) {
                printf("fsck_lite ERROR: inode %u %s block out of range: %u\n",
                       inum, what, addr);
                errors++;
                return;
            }
            if (addr < datastart) {
                printf("fsck_lite ERROR: inode %u %s block in metadata region: %u (datastart=%u)\n",
                       inum, what, addr, datastart);
                errors++;
                return;
            }
            if (used[addr]) {
                printf("fsck_lite ERROR: duplicate block %u referenced again (inode %u %s)\n",
                       addr, inum, what);
                errors++;
                return;
            }
            used[addr] = 1;

            // 位图一致性：引用的块必须在 bitmap 中是 1
            if (bitmap_isset(dev, addr) == 0) {
                printf("fsck_lite ERROR: inode %u references block %u but bitmap says FREE\n",
                       inum, addr);
                errors++;
            }
        }

        // 直接块
        for (int i = 0; i < NDIRECT; i++) {
            check_addr(dip->addrs[i], "direct");
        }

        // 一级间接块：dip->addrs[NDIRECT] 指向间接块本身
        uint32 indirect = dip->addrs[NDIRECT];
        if (indirect != 0) {
            check_addr(indirect, "indirect(block)");
            struct buf *ib = bread(dev, indirect);
            uint32 *a = (uint32 *)ib->data;
            for (int j = 0; j < NINDIRECT; j++) {
                if (a[j]) {
                    check_addr(a[j], "indirect(data)");
                }
            }
            brelse(ib);
        }

        brelse(b);
    }

    if (errors == 0) {
        printf("=== fsck_lite: OK ===\n");
        return 0;
    } else {
        printf("=== fsck_lite: FAILED, errors=%d ===\n", errors);
        return -1;
    }
}
