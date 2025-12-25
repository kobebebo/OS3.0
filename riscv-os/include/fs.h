// include/fs.h
#ifndef _FS_H_
#define _FS_H_

#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"

// ------------ 常量定义 ------------

// 块大小：与内核页大小一致，都是 4096 字节
#define BSIZE 4096

// 文件系统魔数，用于识别磁盘上是否是我们的文件系统
#define FSMAGIC 0x10203040

// inode 类型
#define T_UNUSED 0
#define T_DIR    1   // 目录
#define T_FILE   2   // 普通文件
#define T_DEV    3   // 设备文件（保留）

// 超级块：记录整个文件系统的元数据（磁盘上只占用 1 个块）
struct superblock {
    uint32 magic;      // 魔数，必须是 FSMAGIC 才认为是合法文件系统
    uint32 size;       // 总块数（包括 super/log/inode/bitmap/data 等所有区域）
    uint32 nblocks;    // 数据块数量
    uint32 ninodes;    // inode 总数量
    uint32 nlog;       // 日志区的块数
    uint32 logstart;   // 日志区起始块号
    uint32 inodestart; // inode 区起始块号
    uint32 bmapstart;  // 位图区起始块号
};

// ------------ 磁盘上的 inode 结构 ------------

// 磁盘上真正存放的 inode
struct dinode {
    short  type;               // T_DIR/T_FILE/T_DEV/...
    short  major;              // 设备号（T_DEV 时使用）
    short  minor;
    short  nlink;              // 指向该 inode 的硬链接数量
    uint32 size;               // 文件大小（字节）
    uint32 addrs[13];          // 12 个直接块 + 1 个一级间接块
};

#define NDIRECT   12
#define NINDIRECT (BSIZE / sizeof(uint32))
#define MAXFILE   (NDIRECT + NINDIRECT)

// 每个块里可以容纳多少个 dinode
#define IPB (BSIZE / sizeof(struct dinode))

// 第 inum 个 inode 所在的磁盘块号
#define IBLOCK(inum, sb) ((inum) / IPB + (sb).inodestart)

// 位图相关：一个块有多少 bit
#define BPB (BSIZE * 8)

// 第 b 个数据块对应的位图块块号
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

// ------------ 内存中的 inode 结构 ------------

struct inode {
    uint32 dev;                 // 所在设备号
    uint32 inum;                // inode 号
    int    ref;                 // 引用计数（在 icache 中被多少地方引用）

    struct sleeplock lock;      // 保护下方字段
    int    valid;               // 是否已经从磁盘加载了元数据

    // 从 dinode 拷贝过来的元数据
    short  type;                // T_DIR/T_FILE/T_DEV
    short  major;
    short  minor;
    short  nlink;
    uint32 size;
    uint32 addrs[NDIRECT + 1];  // 数据块地址（最后一个为一级间接块）
};

// inode 缓存：固定大小的数组 + 自旋锁
#define NINODE 50

struct inode_cache {
    struct spinlock lock;
    struct inode    inode[NINODE];
};

extern struct inode_cache icache;

// ------------ 目录项结构 ------------

#define DIRSIZ 14

struct dirent {
    uint16 inum;                // inode 号，0 表示该目录项为空
    char   name[DIRSIZ];        // 不一定以 '\0' 结尾
};

// ------------ 块缓存 buf 结构 ------------

#define NBUF 30                 // 块缓存中 buf 的数量（可根据需要调整）

struct buf {
    int valid;                  // 数据是否有效
    int disk;                   // 是否被修改过，需要写回磁盘
    uint32 dev;                 // 设备号
    uint32 blockno;             // 磁盘块号

    struct sleeplock lock;      // 保护 data 区
    uint32 refcnt;              // 引用计数
    struct buf *prev;           // LRU 链表（前驱）
    struct buf *next;           // LRU 链表（后继）

    unsigned char data[BSIZE];  // 实际缓存的数据
};

// ------------ 日志结构 ------------

// 日志中最多能记录多少个块（事务总和）
#define LOGSIZE      30

// 单个文件系统操作最多会修改多少个块
#define MAXOPBLOCKS  10

struct logheader {
    int n;                      // 当前事务中涉及的块数
    int block[LOGSIZE];         // 每个块在文件系统中的块号
};

struct log {
    struct spinlock lock;
    int start;                  // 日志区起始块号
    int size;                   // 日志区大小
    int outstanding;            // 正在进行的 FS 操作数量
    int committing;             // 是否正在提交（commit）
    int dev;                    // 日志所在设备号
    struct logheader lh;        // 内存中的日志头
};

extern struct superblock sb;    // 在 fs.c 中定义
extern struct log        log;   // 在 log.c 中定义

// ------------ bio.c 接口 ------------

void        binit(void);
struct buf* bread(uint32 dev, uint32 blockno);
void        bwrite(struct buf *b);
void        brelse(struct buf *b);

// ------------ log.c 接口 ------------

void initlog(int dev, struct superblock *sb);
void begin_op(void);
void end_op(void);
void log_write(struct buf *b);

// ------------ fs.c 接口 ------------

// 初始化文件系统（main.c 中调用一次）
void fs_init(int dev);

// inode 管理
void         iinit(void);
struct inode* ialloc(uint32 dev, short type);
struct inode* iget(uint32 dev, uint32 inum);
void         ilock(struct inode *ip);
void         iunlock(struct inode *ip);
void         iput(struct inode *ip);
void         iupdate(struct inode *ip);
void         itrunc(struct inode *ip);

// 数据块读写
int  readi(struct inode *ip, int user_dst, uint64 dst, uint32 off, uint32 n);
int  writei(struct inode *ip, int user_src, uint64 src, uint32 off, uint32 n);

// stat 信息（给 file.c / stat 系统调用使用）
struct stat;
void stati(struct inode *ip, struct stat *st);

// 目录 & 路径解析
struct inode* dirlookup(struct inode *dp, char *name, uint32 *poff);
int           dirlink(struct inode *dp, char *name, uint32 inum);
struct inode* namei(char *path);
struct inode* nameiparent(char *path, char *name);

// 根文件系统设备与根目录 inode 号
#define ROOTDEV 1
#define ROOTINO 1

#endif // _FS_H_
