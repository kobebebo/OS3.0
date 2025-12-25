// kernel/sysfile.c
// 文件系统相关的系统调用实现（简化版）
// 假设当前还没有真正的用户态和管道/exec 功能，只做：
//   - open
//   - read
//   - write
//   - close
//   - fstat
//   - dup
//
// 注意：
// 1. 这里不依赖 struct proc 里的 ofile[]/cwd/pagetable，
//    而是用一个“全局文件描述符表” g_ofile[] 来简化实现。
// 2. 后续如果你要做多进程/多线程 + 用户态，再把 fd 表挪到 proc 里即可。

#include "types.h"
#include "printf.h"
#include "riscv.h"

#include "syscall.h"  // argint/argaddr/argstr
#include "fs.h"       // inode / namei / nameiparent / dirlink / T_FILE 等
#include "file.h"     // struct file / filealloc / fileread / filewrite 等
#include "fcntl.h"    // O_RDONLY/O_WRONLY/O_RDWR/O_CREATE/O_TRUNC
#include "stat.h"     // struct stat

// ---------- 简化版：全局文件描述符表 ----------

// 每个“进程”最多同时打开的文件数量（简化版，全局共享）
#define NOFILE   16
// 路径最大长度
#define MAXPATH  128

// g_ofile[fd] = 对应的 struct file*
// 这里用一个全局数组，而不是放在 struct proc 里，
// 对于当前“单内核线程测试”的场景已经够用。
static struct file *g_ofile[NOFILE];

// 从第 n 个系统调用参数中取 fd，返回对应的 struct file*
// n: 第几个 syscall 参数（0 开始）
// pfd: 如非空，写回 fd 数值
// pf:  如非空，写回 struct file*
static int
argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    // 从 syscall 的第 n 个参数中取一个 int
    argint(n, &fd);

    if (fd < 0 || fd >= NOFILE || (f = g_ofile[fd]) == 0) {
        return -1;
    }

    if (pfd) {
        *pfd = fd;
    }
    if (pf) {
        *pf = f;
    }
    return 0;
}

// 在全局 fd 表中分配一个空闲 fd，并绑定到 f
static int
fdalloc(struct file *f)
{
    for (int fd = 0; fd < NOFILE; fd++) {
        if (g_ofile[fd] == 0) {
            g_ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

// ---------- 内部辅助：创建文件（或复用已有文件） ----------

// 在路径 path 处创建一个新的 inode：type = T_FILE/T_DIR，major/minor 给设备用。
// 如果同名文件已存在且类型兼容，会直接返回已有文件。
// 返回时，ip 仍然“上锁”（ilock 持有），调用者需要负责 iunlock/iput。
static struct inode *
create(char *path, short type, short major, short minor)
{
    struct inode *ip;
    struct inode *dp;
    char name[DIRSIZ];

    // 找到父目录 dp，以及最后一段名字 name
    dp = nameiparent(path, name);
    if (dp == 0) {
        return 0;
    }

    ilock(dp);

    // 看看父目录里是否已经有同名目录项
    ip = dirlookup(dp, name, 0);
    if (ip != 0) {
        // 已存在：检查类型是否兼容
        iunlock(dp);
        iput(dp);

        ilock(ip);
        if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEV)) {
            // 对普通文件：允许“打开已存在文件”的语义
            return ip;   // ip 仍然保持加锁状态
        }
        // 其它类型不兼容，失败
        iunlock(ip);
        iput(ip);
        return 0;
    }

    // 不存在，则分配一个新的 inode
    ip = ialloc(dp->dev, type);
    if (ip == 0) {
        iunlock(dp);
        iput(dp);
        return 0;
    }

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;      // 先假设即将会被链接到目录中
    iupdate(ip);        // 把元数据写回磁盘

    if (type == T_DIR) {
        // 目录需要额外处理 "." 和 ".." 两个目录项
        // 父目录 dp 的链接数也要 +1（对应子目录的 ".."）
        dp->nlink++;
        iupdate(dp);

        // 在新目录里添加 "." 和 ".."
        if (dirlink(ip, ".", ip->inum) < 0 ||
            dirlink(ip, "..", dp->inum) < 0) {
            // 非常罕见的失败路径，简单回滚
            iunlock(ip);
            ip->nlink = 0;
            iupdate(ip);
            iunlock(dp);
            iput(ip);
            iput(dp);
            return 0;
        }
    }

    // 把新 inode 加入父目录
    if (dirlink(dp, name, ip->inum) < 0) {
        // 再次极少发生的失败路径，简单回滚处理
        iunlock(ip);
        ip->nlink = 0;
        iupdate(ip);
        iunlock(dp);
        iput(ip);
        iput(dp);
        return 0;
    }

    // 父目录已经不再需要持锁
    iunlock(dp);
    iput(dp);

    // 返回时，ip 仍然是“加锁状态”，方便调用者继续对其操作
    return ip;
}

// ---------- 具体系统调用实现 ----------

// dup(oldfd) -> newfd
// 只是多一个引用，不会复制文件内容。
uint64
sys_dup(void)
{
    struct file *f;
    int fd;

    if (argfd(0, 0, &f) < 0) {
        return (uint64)-1;
    }
    if ((fd = fdalloc(f)) < 0) {
        return (uint64)-1;
    }
    filedup(f);
    return (uint64)fd;
}

// read(fd, buf, n)
// 当前实验中，buf 被认为是“内核中的有效地址”。
uint64
sys_read(void)
{
    struct file *f;
    uint64 p;
    int n;

    if (argfd(0, 0, &f) < 0) {
        return (uint64)-1;
    }
    argaddr(1, &p);
    argint(2, &n);

    return (uint64)fileread(f, p, n);
}

// write(fd, buf, n)
uint64
sys_write(void)
{
    struct file *f;
    uint64 p;
    int n;

    if (argfd(0, 0, &f) < 0) {
        return (uint64)-1;
    }
    argaddr(1, &p);
    argint(2, &n);

    return (uint64)filewrite(f, p, n);
}

// close(fd)
uint64
sys_close(void)
{
    int fd;
    struct file *f;

    if (argfd(0, &fd, &f) < 0) {
        return (uint64)-1;
    }

    // 从全局 fd 表中清除，再真正关闭 file 对象
    g_ofile[fd] = 0;
    fileclose(f);

    return 0;
}

// fstat(fd, struct stat *st)
// 把文件的元信息写入 st。
uint64
sys_fstat(void)
{
    struct file *f;
    uint64 addr;

    if (argfd(0, 0, &f) < 0) {
        return (uint64)-1;
    }
    argaddr(1, &addr);

    return (uint64)filestat(f, addr);
}

// open(path, omode)
// 支持标志：O_RDONLY/O_WRONLY/O_RDWR/O_CREATE/O_TRUNC
uint64
sys_open(void)
{
    char path[MAXPATH];
    int omode;
    int n;
    struct file *f;
    struct inode *ip;
    int fd;

    // 取 path 和 omode 参数
    n = argstr(0, path, MAXPATH);
    if (n < 0) {
        return (uint64)-1;
    }
    argint(1, &omode);

    begin_op();

    if (omode & 0x200) {
        // 创建新文件（普通文件）
        ip = create(path, T_FILE, 0, 0);
        if (ip == 0) {
            end_op();
            return (uint64)-1;
        }
    } else {
        // 打开已有文件
        ip = namei(path);
        if (ip == 0) {
            end_op();
            return (uint64)-1;
        }
        ilock(ip);

        // 简化处理：不允许以“非只读方式”打开目录
        if (ip->type == T_DIR && omode != O_RDONLY) {
            iunlock(ip);
            iput(ip);
            end_op();
            return (uint64)-1;
        }
    }

    // 此时 ip 已经被 ilock() 上锁

    // 为该 inode 分配一个 struct file 和一个 fd
    f = filealloc();
    if (f == 0) {
        iunlock(ip);
        iput(ip);
        end_op();
        return (uint64)-1;
    }

    fd = fdalloc(f);
    if (fd < 0) {
        fileclose(f);
        iunlock(ip);
        iput(ip);
        end_op();
        return (uint64)-1;
    }

    // 初始化 struct file
    f->type     = FD_INODE;
    f->ip       = ip;
    f->off      = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

    // 如果请求截断，则把文件内容截断为 0
    if ((omode & O_TRUNC) && ip->type == T_FILE) {
        itrunc(ip);
    }

    iunlock(ip);
    end_op();

    return (uint64)fd;
}
