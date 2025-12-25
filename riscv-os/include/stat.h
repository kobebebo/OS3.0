// include/stat.h
#ifndef _STAT_H_
#define _STAT_H_

#include "types.h"

// stat 结构：描述一个文件的元数据
struct stat {
    short  type;   // T_FILE / T_DIR / T_DEV
    short  nlink;  // 硬链接数
    uint32 dev;    // 设备号
    uint32 ino;    // inode 号
    uint64 size;   // 文件大小（字节）
};

#endif // _STAT_H_
