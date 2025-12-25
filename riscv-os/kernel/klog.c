// kernel/klog.c
//
// 结构化内核日志系统：
//  - 环形缓冲区保存最近 KLOG_NENTRY 条日志
//  - 每条日志包含：时间戳、级别、tag、格式化消息
//  - 支持：级别过滤、同步控制台输出、dump/clear、统计
//

#include <stdarg.h>

#include "types.h"
#include "printf.h"
#include "riscv.h"
#include "klog.h"

// -------------------- 小工具：本地字符串/内存工具（避免依赖 libc） --------------------

static void *memset_local(void *dst, int c, uint32 n)
{
    uint8 *p = (uint8 *)dst;
    for (uint32 i = 0; i < n; i++) p[i] = (uint8)c;
    return dst;
}

static void *memmove_local(void *dst, const void *src, uint32 n)
{
    uint8 *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        for (uint32 i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (uint32 i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

static uint32 strnlen_local(const char *s, uint32 max)
{
    uint32 n = 0;
    while (n < max && s && s[n]) n++;
    return n;
}

static void strncpy0(char *dst, const char *src, uint32 max)
{
    if (max == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    uint32 i = 0;
    for (; i + 1 < max && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

// -------------------- 小工具：把数字写入字符串缓冲区（kvsnprintf 用） --------------------

static char digits[] = "0123456789abcdef";

struct outbuf {
    char  *buf;
    int    cap;   // 总容量
    int    len;   // 当前长度（不含结尾 0）
};

static void ob_putc(struct outbuf *ob, char c)
{
    if (ob->cap <= 0) return;
    if (ob->len + 1 >= ob->cap) return; // 预留 '\0'
    ob->buf[ob->len++] = c;
    ob->buf[ob->len] = 0;
}

static void ob_puts(struct outbuf *ob, const char *s)
{
    if (!s) s = "(null)";
    while (*s) ob_putc(ob, *s++);
}

static void ob_put_uint(struct outbuf *ob, uint64 x, int base)
{
    char tmp[32];
    int i = 0;

    if (base < 2) base = 10;

    do {
        tmp[i++] = digits[x % (uint64)base];
        x /= (uint64)base;
    } while (x != 0 && i < (int)sizeof(tmp));

    while (i > 0) ob_putc(ob, tmp[--i]);
}

static void ob_put_int(struct outbuf *ob, int64 x)
{
    if (x < 0) {
        ob_putc(ob, '-');
        ob_put_uint(ob, (uint64)(-x), 10);
    } else {
        ob_put_uint(ob, (uint64)x, 10);
    }
}

static void kvsnprintf_local(char *buf, int cap, const char *fmt, va_list ap)
{
    struct outbuf ob;
    ob.buf = buf;
    ob.cap = cap;
    ob.len = 0;
    if (cap > 0) buf[0] = 0;

    for (const char *p = fmt; p && *p; p++) {
        if (*p != '%') {
            ob_putc(&ob, *p);
            continue;
        }

        // 解析：支持 %d %u %x %p %s %c %%
        p++;
        if (*p == 0) break;

        // 简化支持：处理 'l' 或 'll'（把数字按 64bit 取）
        int longflag = 0;
        int longlongflag = 0;
        if (*p == 'l') {
            longflag = 1;
            p++;
            if (*p == 'l') {
                longlongflag = 1;
                p++;
            }
            if (*p == 0) break;
        }

        char c = *p;
        switch (c) {
        case 'd':
            if (longflag || longlongflag) ob_put_int(&ob, (int64)va_arg(ap, int64));
            else ob_put_int(&ob, (int64)va_arg(ap, int));
            break;
        case 'u':
            if (longflag || longlongflag) ob_put_uint(&ob, (uint64)va_arg(ap, uint64), 10);
            else ob_put_uint(&ob, (uint64)va_arg(ap, uint32), 10);
            break;
        case 'x':
            if (longflag || longlongflag) ob_put_uint(&ob, (uint64)va_arg(ap, uint64), 16);
            else ob_put_uint(&ob, (uint64)va_arg(ap, uint32), 16);
            break;
        case 'p': {
            uint64 v = va_arg(ap, uint64);
            ob_puts(&ob, "0x");
            // 打印 16 个十六进制半字节
            for (int i = 0; i < 16; i++) {
                int shift = (15 - i) * 4;
                ob_putc(&ob, digits[(v >> shift) & 0xF]);
            }
            break;
        }
        case 's':
            ob_puts(&ob, va_arg(ap, const char *));
            break;
        case 'c':
            ob_putc(&ob, (char)va_arg(ap, int));
            break;
        case '%':
            ob_putc(&ob, '%');
            break;
        default:
            // 不认识就原样输出
            ob_putc(&ob, '%');
            ob_putc(&ob, c);
            break;
        }
    }
}

// -------------------- klog 内部状态 --------------------

static struct {
    int inited;

    // 运行时配置
    int level_threshold;   // 最小记录级别（低于它的会被过滤）
    int console_on;        // 写入时是否同步输出到 console

    // 环形缓冲区
    struct klog_entry ring[KLOG_NENTRY];
    uint32 head;           // 下一次写入的位置
    uint32 count;          // 当前有效条目数（<= KLOG_NENTRY）

    // 统计
    uint64 total_written;
    uint64 overwritten;
    uint64 filtered;
} ks;

// 级别名（用于 dump/console 输出）
static const char *level_name(int level)
{
    switch (level) {
    case KLOG_DEBUG: return "DEBUG";
    case KLOG_INFO:  return "INFO";
    case KLOG_WARN:  return "WARN";
    case KLOG_ERROR: return "ERROR";
    case KLOG_FATAL: return "FATAL";
    default:         return "UNK";
    }
}

void klog_init(void)
{
    memset_local(&ks, 0, sizeof(ks));
    ks.inited = 1;
    ks.level_threshold = KLOG_DEBUG; // 默认记录全部级别
    ks.console_on = 1;               // 默认同步打印，便于调试
}

void klog_set_level(int level)
{
    if (!ks.inited) klog_init();
    if (level < KLOG_DEBUG) level = KLOG_DEBUG;
    if (level > KLOG_FATAL) level = KLOG_FATAL;
    ks.level_threshold = level;
}

int klog_get_level(void)
{
    if (!ks.inited) klog_init();
    return ks.level_threshold;
}

void klog_enable_console(int on)
{
    if (!ks.inited) klog_init();
    ks.console_on = (on != 0);
}

static void klog_emit_console(const struct klog_entry *e)
{
    // 注意：你当前 printf 支持 %p %s，ts 用 %p 打印成 0x... 的 64 位数，足够验收展示
    printf("[klog %p] [%s] %s: %s\n",
           (uint64)e->ts, level_name((int)e->level), e->tag, e->msg);
}

void klogf(int level, const char *tag, const char *fmt, ...)
{
    if (!ks.inited) klog_init();

    // level 过滤：低于阈值直接丢弃并计数
    if (level < ks.level_threshold) {
        ks.filtered++;
        return;
    }

    // 选定写入槽位
    uint32 idx = ks.head;

    // 环满：覆盖最旧记录（覆盖次数 +1）
    if (ks.count == KLOG_NENTRY) {
        ks.overwritten++;
    } else {
        ks.count++;
    }

    ks.head = (ks.head + 1) % KLOG_NENTRY;

    // 填充 entry
    struct klog_entry *e = &ks.ring[idx];
    e->ts = r_time();               // time CSR（你 exp4/exp7 也在用）
    e->level = (uint8)level;
    strncpy0(e->tag, tag ? tag : "-", KLOG_MAXTAG);

    // 格式化 msg
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf_local(e->msg, KLOG_MAXMSG, fmt ? fmt : "", ap);
    va_end(ap);

    ks.total_written++;

    // 同步输出到控制台（可关闭）
    if (ks.console_on) {
        klog_emit_console(e);
    }
}

void klog_dump(int max_lines)
{
    if (!ks.inited) klog_init();

    printf("=== klog dump: count=%d total=%p overwritten=%p filtered=%p ===\n",
           (int)ks.count, (uint64)ks.total_written, (uint64)ks.overwritten, (uint64)ks.filtered);

    if (ks.count == 0) return;

    // 计算最旧条目的索引：tail = (head - count + N) % N
    uint32 tail = (ks.head + KLOG_NENTRY - ks.count) % KLOG_NENTRY;

    int to_print = (max_lines <= 0 || max_lines > (int)ks.count) ? (int)ks.count : max_lines;

    // 如果 max_lines 限制，只打印最近的 to_print 条
    // recent_tail = (head - to_print + N) % N
    if (to_print < (int)ks.count) {
        tail = (ks.head + KLOG_NENTRY - (uint32)to_print) % KLOG_NENTRY;
    }

    for (int i = 0; i < to_print; i++) {
        uint32 idx = (tail + (uint32)i) % KLOG_NENTRY;
        klog_emit_console(&ks.ring[idx]);
    }
}

void klog_clear(void)
{
    if (!ks.inited) klog_init();
    ks.head = 0;
    ks.count = 0;
    // 不清统计，让你能看到历史写入/覆盖/过滤次数
}

struct klog_stats klog_get_stats(void)
{
    if (!ks.inited) klog_init();
    struct klog_stats st;
    st.total_written = ks.total_written;
    st.overwritten   = ks.overwritten;
    st.filtered      = ks.filtered;
    st.count         = ks.count;
    return st;
}
