// include/fcntl.h
#ifndef _FCNTL_H_
#define _FCNTL_H_

// 打开文件标志（与 xv6 兼容）
#define O_RDONLY  0x000   // 只读
#define O_WRONLY  0x001   // 只写
#define O_RDWR    0x002   // 读写
#define O_CREATE  0x200   // 不存在则创建
#define O_TRUNC   0x400   // 截断为 0

#endif // _FCNTL_H_
