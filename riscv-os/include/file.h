// include/file.h
#ifndef _FILE_H_
#define _FILE_H_

#include "types.h"
#include "fs.h"

// 打开文件类型
#define FD_NONE   0   // 空闲
#define FD_INODE  1   // 普通文件 / 目录
#define FD_DEVICE 2   // 设备文件（可选）

// 内核中的“打开文件”对象
struct file {
    int   type;       // FD_xxx
    int   ref;        // 引用计数
    char  readable;   // 是否可读
    char  writable;   // 是否可写
    struct inode *ip; // 对应的 inode
    uint32 off;       // 读写偏移
};

// 全局打开文件表大小
#define NFILE 100

// file.c 提供的接口
void         fileinit(void);
struct file* filealloc(void);
struct file* filedup(struct file *f);
void         fileclose(struct file *f);
int          filestat(struct file *f, uint64 addr);
int          fileread(struct file *f, uint64 addr, int n);
int          filewrite(struct file *f, uint64 addr, int n);

#endif // _FILE_H_
