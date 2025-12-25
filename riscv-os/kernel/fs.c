// kernel/fs.c
// 简化版 xv6 风格文件系统实现：
//  - 物理块分配/释放（balloc/bfree）
//  - inode 分配/缓存/读写（ialloc/iget/ilock/...）
//  - 文件读写（readi/writei）
//  - 目录与路径解析（dirlookup/namei/...）
//  - 文件系统初始化 fs_init()：会在 RAM 磁盘上创建一个全新的根目录

#include "types.h"
#include "printf.h"
#include "fs.h"
#include "stat.h"
#include "file.h"   // 为了调用 fileinit()

// 超级块全局变量（内存中的 copy）
struct superblock sb;

// inode 缓存
struct inode_cache icache;

// ----------- 小工具函数 -----------

static void *
memset_local(void *dst, int c, uint32 n)
{
    unsigned char *p = (unsigned char *)dst;
    for (uint32 i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return dst;
}

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

static int
strncmp_local(const char *s, const char *t, int n)
{
    while (n > 0 && *s && *t && *s == *t) {
        s++;
        t++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s - (unsigned char)*t;
}

// 读取超级块（约定在块号 1）
static void
readsb(uint32 dev, struct superblock *sbp)
{
    struct buf *b = bread(dev, 1);
    memmove_local(sbp, b->data, sizeof(*sbp));
    brelse(b);
}

// 把一个数据块清零
static void
bzero(uint32 dev, uint32 bno)
{
    struct buf *b = bread(dev, bno);
    memset_local(b->data, 0, BSIZE);
    bwrite(b);
    brelse(b);
}

// 在位图中分配一个空闲块，返回块号
static uint32
balloc(uint32 dev)
{
    struct buf *b;
    char *bits;

    for (uint32 bno = 0; bno < sb.size; bno += BPB) {
        int bitmap_blockno = BBLOCK(bno, sb);
        b = bread(dev, bitmap_blockno);
        bits = (char *)b->data;

        for (uint32 bi = 0; bi < BPB && (bno + bi) < sb.size; bi++) {
            int byte_index = bi / 8;
            int bit_index  = bi % 8;
            unsigned char mask = 1 << bit_index;

            if ((bits[byte_index] & mask) == 0) {
                // 找到空闲块，标记为已用
                bits[byte_index] |= mask;
                bwrite(b);
                brelse(b);

                uint32 allocated = bno + bi;
                bzero(dev, allocated);  // 把新块内容清零
                return allocated;
            }
        }

        brelse(b);
    }

    panic("balloc: out of blocks");
    return 0;
}

// 把块 bno 标记为空闲
static void
bfree(uint32 dev, uint32 bno)
{
    struct buf *b = bread(dev, BBLOCK(bno, sb));
    char *bits = (char *)b->data;

    uint32 bi = bno % BPB;
    int byte_index = bi / 8;
    int bit_index  = bi % 8;
    unsigned char mask = 1 << bit_index;

    if ((bits[byte_index] & mask) == 0) {
        panic("bfree: block not allocated");
    }

    bits[byte_index] &= ~mask;
    bwrite(b);
    brelse(b);
}

// bmap：逻辑块号 bn -> 物理块号（alloc=1 时需要则分配）
static uint32
bmap(struct inode *ip, uint32 bn, int alloc)
{
    uint32 addr;
    struct buf *b;
    uint32 *a;

    if (bn < NDIRECT) {
        addr = ip->addrs[bn];
        if (addr == 0 && alloc) {
            addr = balloc(ip->dev);
            ip->addrs[bn] = addr;
        }
        return addr;
    }

    bn -= NDIRECT;
    if (bn >= NINDIRECT) {
        panic("bmap: out of range");
    }

    // 一级间接块
    addr = ip->addrs[NDIRECT];
    if (addr == 0) {
        if (!alloc) {
            return 0;
        }
        addr = balloc(ip->dev);
        ip->addrs[NDIRECT] = addr;
    }

    b = bread(ip->dev, addr);
    a = (uint32 *)b->data;
    if (a[bn] == 0 && alloc) {
        a[bn] = balloc(ip->dev);
        bwrite(b);
    }
    uint32 result = a[bn];
    brelse(b);

    return result;
}

// ------------ inode 缓存 & 初始化 ------------

// 初始化 inode 缓存
void
iinit(void)
{
    initlock(&icache.lock, "icache");
    for (int i = 0; i < NINODE; i++) {
        icache.inode[i].ref   = 0;
        icache.inode[i].valid = 0;
        initsleeplock(&icache.inode[i].lock, "inode");
    }
}

// 从缓存中获取一个指定 (dev, inum) 的 inode
struct inode *
iget(uint32 dev, uint32 inum)
{
    struct inode *ip, *empty = 0;

    acquire(&icache.lock);

    // 1. 在缓存中查找
    for (ip = icache.inode; ip < icache.inode + NINODE; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            release(&icache.lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0) {
            empty = ip;
        }
    }

    // 2. 没有命中缓存，则找一个空闲项
    if (empty == 0) {
        release(&icache.lock);
        panic("iget: no free inode");
    }

    ip = empty;
    ip->dev   = dev;
    ip->inum  = inum;
    ip->ref   = 1;
    ip->valid = 0;

    release(&icache.lock);
    return ip;
}

// 在磁盘上分配一个新的 inode，类型为 type
struct inode *
ialloc(uint32 dev, short type)
{
    struct buf *b;
    struct dinode *dip;

    for (uint32 inum = 1; inum < sb.ninodes; inum++) {
        b = bread(dev, IBLOCK(inum, sb));
        dip = (struct dinode *)b->data + (inum % IPB);

        if (dip->type == T_UNUSED) {
            // 找到空闲 inode
            memset_local(dip, 0, sizeof(*dip));
            dip->type  = type;
            dip->nlink = 1;
            bwrite(b);
            brelse(b);

            struct inode *ip = iget(dev, inum);
            ip->type  = type;
            ip->nlink = 1;
            return ip;
        }

        brelse(b);
    }

    panic("ialloc: no inodes");
    return 0;
}

// 加锁并确保 inode 内容已从磁盘读入
void
ilock(struct inode *ip)
{
    if (ip == 0 || ip->ref < 1) {
        panic("ilock: bad ip");
    }

    acquiresleep(&ip->lock);

    if (!ip->valid) {
        struct buf *b = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode *)b->data + (ip->inum % IPB);

        ip->type  = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size  = dip->size;
        for (int i = 0; i < NDIRECT + 1; i++) {
            ip->addrs[i] = dip->addrs[i];
        }

        brelse(b);
        ip->valid = 1;

        if (ip->type == T_UNUSED) {
            panic("ilock: no type");
        }
    }
}

// 解锁 inode
void
iunlock(struct inode *ip)
{
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1) {
        panic("iunlock");
    }
    releasesleep(&ip->lock);
}

// 把内存中的 inode 内容写回磁盘
void
iupdate(struct inode *ip)
{
    struct buf *b = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode *)b->data + (ip->inum % IPB);

    dip->type  = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size  = ip->size;
    for (int i = 0; i < NDIRECT + 1; i++) {
        dip->addrs[i] = ip->addrs[i];
    }

    bwrite(b);
    brelse(b);
}

// 释放 inode 的引用，当 ref==1 且 nlink==0 时会删除文件内容
void
iput(struct inode *ip)
{
    acquire(&icache.lock);

    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // 准备删除：释放磁盘上所有数据块并清空 inode
        release(&icache.lock);

        ilock(ip);
        itrunc(ip);
        ip->type = T_UNUSED;
        iupdate(ip);
        iunlock(ip);

        acquire(&icache.lock);
        ip->valid = 0;
    }

    ip->ref--;
    release(&icache.lock);
}

// 释放一个 inode 占用的所有数据块
void
itrunc(struct inode *ip)
{
    // 直接块
    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    // 一级间接块
    if (ip->addrs[NDIRECT]) {
        struct buf *b = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32 *a = (uint32 *)b->data;
        for (int i = 0; i < NINDIRECT; i++) {
            if (a[i]) {
                bfree(ip->dev, a[i]);
            }
        }
        brelse(b);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

// ------------ 文件数据读写 ------------

int
readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n)
{
    (void)user_dst; // 当前没有真正的用户地址空间，直接把 dst 当成内核指针

    if (off > ip->size || off + n < off) {
        return 0;
    }
    if (off + n > ip->size) {
        n = ip->size - off;
    }

    uint32 tot = 0;
    while (tot < n) {
        uint32 bn   = off / BSIZE;
        uint32 boff = off % BSIZE;
        uint32 m    = BSIZE - boff;
        if (m > (n - tot)) {
            m = n - tot;
        }

        uint32 addr = bmap(ip, bn, 0);
        if (addr == 0) {
            panic("readi: addr == 0");
        }

        struct buf *b = bread(ip->dev, addr);
        memmove_local((void *)(dst + tot), b->data + boff, m);
        brelse(b);

        tot += m;
        off += m;
    }

    return n;
}

int
writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n)
{
    (void)user_src;

    if (off > ip->size || off + n < off) {
        return -1;
    }
    if (off + n > MAXFILE * BSIZE) {
        return -1;
    }

    uint32 tot = 0;
    while (tot < n) {
        uint32 bn   = off / BSIZE;
        uint32 boff = off % BSIZE;
        uint32 m    = BSIZE - boff;
        if (m > (n - tot)) {
            m = n - tot;
        }

        uint32 addr = bmap(ip, bn, 1);
        if (addr == 0) {
            panic("writei: addr == 0");
        }

        struct buf *b = bread(ip->dev, addr);
        memmove_local(b->data + boff, (void *)(src + tot), m);
        bwrite(b);
        brelse(b);

        tot += m;
        off += m;
    }

    if (off > ip->size) {
        ip->size = off;
    }
    iupdate(ip);
    return n;
}

// ------------ stat 信息 ------------

void
stati(struct inode *ip, struct stat *st)
{
    st->dev   = ip->dev;
    st->ino   = ip->inum;
    st->type  = ip->type;
    st->nlink = ip->nlink;
    st->size  = ip->size;
}

// ------------ 目录 & 路径解析 ------------

static int
namecmp(const char *s, const char *t)
{
    return strncmp_local(s, t, DIRSIZ);
}

// 在目录 dp 中查找名为 name 的项，找到则返回对应 inode
struct inode *
dirlookup(struct inode *dp, char *name, uint32 *poff)
{
    struct dirent de;

    if (dp->type != T_DIR) {
        panic("dirlookup: not DIR");
    }

    for (uint32 off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlookup: read");
        }
        if (de.inum == 0) {
            continue;
        }
        if (namecmp(name, de.name) == 0) {
            // 找到
            if (poff) {
                *poff = off;
            }
            return iget(dp->dev, de.inum);
        }
    }

    return 0;
}

// 在目录 dp 中创建名为 name、inode 号为 inum 的目录项
int
dirlink(struct inode *dp, char *name, uint32 inum)
{
    struct dirent de;
    uint32 off;

    // 已存在同名目录项则失败
    if (dirlookup(dp, name, 0) != 0) {
        return -1;
    }

    // 查找一个空闲目录项
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlink: read");
        }
        if (de.inum == 0) {
            break;
        }
    }

    memset_local(&de, 0, sizeof(de));
    de.inum = inum;
    int i;
    for (i = 0; i < DIRSIZ && name[i]; i++) {
        de.name[i] = name[i];
    }
    if (i < DIRSIZ) {
        de.name[i] = 0;
    }

    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("dirlink: write");
    }

    return 0;
}

// 把 path 的下一个路径分量拷贝到 name 中，并返回余下路径
static char *
skipelem(char *path, char *name)
{
    // 跳过前导 '/'
    while (*path == '/') path++;
    if (*path == 0) {
        return 0;
    }

    // 复制一个路径分量
    char *s = path;
    int len = 0;
    while (*path != '/' && *path != 0) {
        if (len < DIRSIZ) {
            name[len++] = *path;
        }
        path++;
    }
    name[len] = 0;

    // 跳过多余的 '/'
    while (*path == '/') path++;

    return path;
}

// 核心路径解析函数：
// nameiparent=0 返回最终 inode
// nameiparent=1 返回父目录 inode，同时把最后一段名字写入 name
static struct inode *
namex(char *path, int nameiparent, char *name)
{
    struct inode *ip;
    struct inode *next;
    char elem[DIRSIZ];

    if (*path == '/') {
        // 绝对路径：从根目录开始
        ip = iget(ROOTDEV, ROOTINO);
    } else {
        // 本实验暂无 per-process cwd，统一从根目录开始
        ip = iget(ROOTDEV, ROOTINO);
    }

    if (*path == 0) {
        return 0;
    }

    while ((path = skipelem(path, elem)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlock(ip);
            iput(ip);
            return 0;
        }

        if (nameiparent && *path == 0) {
            // 需要父目录，当前 ip 正好是父目录
            if (name) {
                for (int i = 0; i < DIRSIZ; i++) {
                    name[i] = elem[i];
                }
            }
            iunlock(ip);
            return ip;
        }

        next = dirlookup(ip, elem, 0);
        iunlock(ip);
        if (next == 0) {
            iput(ip);
            return 0;
        }

        iput(ip);
        ip = next;
    }

    if (nameiparent) {
        iput(ip);
        return 0;
    }
    if (name) {
        name[0] = 0;
    }
    return ip;
}

struct inode *
namei(char *path)
{
    return namex(path, 0, 0);
}

struct inode *
nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}

// ------------ 文件系统总初始化入口 ------------
//
// 在实验七中，我们在 test.c 里调用 fs_init(ROOTDEV)，
// 这个函数会：
//   - binit()：初始化块缓存 + 内存中的“RAM 磁盘”（virtio_disk_init）
//   - fileinit()：初始化全局文件表
//   - readsb()：读取超级块
//   - iinit()：初始化 inode 缓存
//   - initlog()：初始化日志系统
//   - 如果是新文件系统，则创建根目录 inode (#1) 并写入 "." 和 ".."

// ------------ 文件系统总初始化入口 ------------
//
// fs_init(dev) 在内存 RAM 磁盘上挂载一个简单文件系统：
//   - binit()       : 初始化块缓存 + 调用 virtio_disk_init() 建 RAM 磁盘
//   - fileinit()    : 初始化全局文件表
//   - readsb()      : 读取超级块
//   - iinit()       : 初始化 inode 缓存
//   - initlog()     : 初始化日志系统
//   - 如果根 inode(1) 还是 T_UNUSED，则在磁盘上创建根目录和 . / ..
//
// 注意：这里创建根目录时，直接操作“磁盘上的 dinode + 数据块”
//       不会调用 ilock()/iget()，因此不会触发 "ilock: no type"。

void
fs_init(int dev)
{
    // 1. 初始化块缓存（内部会 virtio_disk_init 创建 RAM 磁盘 + superblock）
    binit();

    // 2. 初始化全局文件表
    fileinit();

    // 3. 读取超级块
    readsb(dev, &sb);
    if (sb.magic != FSMAGIC) {
        panic("fs_init: bad superblock magic");
    }

    printf("fs_init: size=%d nblocks=%d ninodes=%d nlog=%d\n",
           sb.size, sb.nblocks, sb.ninodes, sb.nlog);

    // 4. 初始化 inode 缓存和日志系统
    iinit();
    initlog(dev, &sb);

    // 5. 检查根 inode(#1) 是否已经存在；如果是新文件系统，则创建之
    struct buf *b = bread(dev, IBLOCK(ROOTINO, sb));
    struct dinode *dip = (struct dinode *)b->data + (ROOTINO % IPB);

    if (dip->type == T_UNUSED) {
        // 这是一个“刚格式化好的空文件系统”，还没有根目录

        printf("fs_init: creating root directory (inum=%d)\n", ROOTINO);

        // (1) 初始化根目录 dinode 元数据
        memset_local(dip, 0, sizeof(*dip));
        dip->type  = T_DIR;
        dip->major = 0;
        dip->minor = 0;
        dip->nlink = 1;                       // 至少有 "." 这个链接
        dip->size  = 2 * sizeof(struct dirent); // 先放 "." 和 ".."

        // 分配一个数据块，用来存放 "." 和 ".."
        uint32 bno = balloc(dev);
        dip->addrs[0] = bno;                 // 根目录的第 0 个直接块
        // 其它 addrs[] 保持为 0

        bwrite(b);   // 把更新后的 dinode 写回磁盘
        brelse(b);

        // (2) 在这个数据块里写入 "." 和 ".." 两个目录项
        struct buf *bdata = bread(dev, bno);
        struct dirent *de = (struct dirent *)bdata->data;

        // "." -> 指向自己
        memset_local(&de[0], 0, sizeof(struct dirent));
        de[0].inum     = ROOTINO;
        de[0].name[0]  = '.';
        de[0].name[1]  = 0;

        // ".." -> 根目录的父亲还是自己
        memset_local(&de[1], 0, sizeof(struct dirent));
        de[1].inum     = ROOTINO;
        de[1].name[0]  = '.';
        de[1].name[1]  = '.';
        de[1].name[2]  = 0;

        // 其余目录项清零
        int nent = BSIZE / sizeof(struct dirent);
        for (int i = 2; i < nent; i++) {
            memset_local(&de[i], 0, sizeof(struct dirent));
        }

        bwrite(bdata);
        brelse(bdata);
    } else {
        // 已经有根 inode 了（例如之前运行时写过文件），直接释放 buf
        brelse(b);
    }
}
