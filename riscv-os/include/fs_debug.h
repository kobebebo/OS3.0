#ifndef _FS_DEBUG_H_
#define _FS_DEBUG_H_

#include "types.h"

// ---- 全局统计变量（在对应 .c 中定义）----

// virtio_disk.c 里累加
extern uint64 disk_read_count;
extern uint64 disk_write_count;

// bio.c 里累加
extern uint64 buffer_cache_hits;
extern uint64 buffer_cache_misses;

// ---- 调试/检查接口 ----
void debug_filesystem_state(void);  // 打印 superblock + 空闲统计 + cache 统计
void debug_inode_usage(void);       // 打印 inode cache 的占用情况（ref>0 的项）
void debug_disk_io(void);           // 打印磁盘 I/O 统计
int  fsck_lite(void);               // 轻量一致性检查：0=OK，-1=发现问题

#endif
