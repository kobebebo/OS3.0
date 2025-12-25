// include/klog.h
#pragma once

#include "types.h"

// ================================
// 内核日志系统（klog）
// - 支持日志级别（DEBUG/INFO/WARN/ERROR/FATAL）
// - 支持环形缓冲区：保存最近 N 条日志（覆盖最旧的）
// - 支持格式化写入：klogf(level, tag, fmt, ...)
// - 支持按级别过滤、输出开关、dump/clear、统计信息
// ================================

// 日志级别：数值越大越“严重”
enum klog_level {
    KLOG_DEBUG = 0,
    KLOG_INFO  = 1,
    KLOG_WARN  = 2,
    KLOG_ERROR = 3,
    KLOG_FATAL = 4,
};

// 配置：日志条目数/单条消息长度/Tag 长度
#ifndef KLOG_NENTRY
#define KLOG_NENTRY 256
#endif

#ifndef KLOG_MAXMSG
#define KLOG_MAXMSG 160
#endif

#ifndef KLOG_MAXTAG
#define KLOG_MAXTAG 16
#endif

// 单条日志条目（结构化信息）
struct klog_entry {
    uint64 ts;                 // 时间戳（当前实现用 r_time() 读取 time CSR）
    uint8  level;              // enum klog_level
    char   tag[KLOG_MAXTAG];   // 模块/子系统标签（如 "fs","bio","sys"）
    char   msg[KLOG_MAXMSG];   // 格式化后的消息正文
};

// 统计信息：用于测试/诊断
struct klog_stats {
    uint64 total_written;      // 调用写入接口成功记录的总次数（含覆盖）
    uint64 overwritten;        // 环形缓冲区满后覆盖最旧条目的次数
    uint64 filtered;           // 因 level 过滤而被丢弃的次数
    uint32 count;              // 当前缓冲区内有效条目数（<=KLOG_NENTRY）
};

// 初始化/配置
void  klog_init(void);
void  klog_set_level(int level);          // 设置“最小记录级别”，低于该级别的日志会被过滤
int   klog_get_level(void);
void  klog_enable_console(int on);        // on=1：写入时同步打印到控制台；on=0：只进缓冲区

// 写日志（核心接口）
void  klogf(int level, const char *tag, const char *fmt, ...);

// 辅助接口：dump/clear/stats
void  klog_dump(int max_lines);           // max_lines<=0 表示全部输出
void  klog_clear(void);
struct klog_stats klog_get_stats(void);

// 便捷宏：固定 level
#define KLOGD(tag, fmt, ...) klogf(KLOG_DEBUG, (tag), (fmt), ##__VA_ARGS__)
#define KLOGI(tag, fmt, ...) klogf(KLOG_INFO,  (tag), (fmt), ##__VA_ARGS__)
#define KLOGW(tag, fmt, ...) klogf(KLOG_WARN,  (tag), (fmt), ##__VA_ARGS__)
#define KLOGE(tag, fmt, ...) klogf(KLOG_ERROR, (tag), (fmt), ##__VA_ARGS__)
#define KLOGF(tag, fmt, ...) klogf(KLOG_FATAL, (tag), (fmt), ##__VA_ARGS__)
