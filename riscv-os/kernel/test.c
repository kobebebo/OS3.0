// kernel/test.c
#include "console.h"
#include "printf.h"
#include "types.h"
#include "memlayout.h"
#include "pmm.h"
#include "vm.h"
#include "trap.h"
#include "riscv.h"
#include "test.h"
#include "proc.h"
#include "syscall.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "stat.h"
#include "fs_debug.h"
#include "klog.h"


// -------- 通用断言宏 --------

#define KASSERT(cond) \
    do { \
        if (!(cond)) { \
            panic("assertion failed: " #cond); \
        } \
    } while (0)

// -------- 实验1：最小启动 & 输出 --------

static void
test_experiment1(void)
{
    printf("==== Experiment 1: boot & bare-metal output ====\n");
    printf("Hello OS (exp1)\n");
}

// -------- 实验2：printf & console 测试 --------

static void
test_experiment2(void)
{
    printf("\n==== Experiment 2: printf & console test ====\n");
    printf("decimal: %d, negative: %d\n", 12345, -6789);
    printf("unsigned: %u\n", 3000000000u);
    printf("hex: 0x%x\n", 0xdeadbeef);
    printf("char: %c\n", 'A');
    printf("string: %s\n", "riscv-os");
    printf("percent: %%\n");

    int x = 42;
    printf("pointer: %p\n", (uint64)&x);
}

// -------- 实验3：物理内存 & 页表测试 --------

static void
test_physical_memory_basic(void)
{
    printf("\n[exp3] testing physical page allocator...\n");

    void *p1 = alloc_page();
    void *p2 = alloc_page();
    KASSERT(p1 != 0);
    KASSERT(p2 != 0);
    KASSERT(p1 != p2);
    KASSERT(((uint64)p1 & (PGSIZE - 1)) == 0);
    KASSERT(((uint64)p2 & (PGSIZE - 1)) == 0);

    printf("[exp3] alloc_page: p1=%p, p2=%p\n", (uint64)p1, (uint64)p2);

    free_page(p1);
    free_page(p2);

    printf("[exp3] physical allocator basic test OK.\n");
}

static void
test_virtual_memory_basic(void)
{
    printf("\n[exp3] building kernel page table and enabling paging...\n");
    kvminit();
    kvminithart();
    printf("[exp3] paging is now enabled, still printing via UART.\n");
}

static void
test_experiment3(void)
{
    printf("\n==== Experiment 3: memory management & paging ====\n");

    // 1. 初始化物理内存分配器
    pmm_init();
    test_physical_memory_basic();

    // 2. 构建内核页表并开启分页
    test_virtual_memory_basic();
}

// -------- 实验4：中断 & 时钟测试 --------

// 时钟 tick 计数在 trap.c 中定义
extern volatile uint64 ticks;

// 手册里的 get_time，这里直接用 time CSR 实现（cycle 计数）
static uint64
get_time(void)
{
    return r_time();
}

// 简单的时钟中断测试：看 ticks 是否在增长
static void
test_timer_interrupt(void)
{
    printf("[exp4] Testing timer interrupt...\n");

    uint64 start_ticks = ticks;
    uint64 start_time  = get_time();

    // 等待一段时间，期间时钟中断会不断增加 ticks
    while (ticks - start_ticks < 20) {
        // busy wait
    }

    uint64 end_time = get_time();
    printf("[exp4] ticks from %d -> %d, delta=%d\n",
           (int)start_ticks, (int)ticks, (int)(ticks - start_ticks));
    printf("[exp4] time delta (cycles) = %d\n", end_time - start_time);
}

// 粗略测量一下中断开关的开销
static void
test_interrupt_overhead(void)
{
    printf("[exp4] Measuring interrupt on/off overhead...\n");

    const int N = 100000;

    uint64 t0 = get_time();
    for (int i = 0; i < N; i++) {
        asm volatile("nop");
    }
    uint64 t1 = get_time();

    uint64 t2 = get_time();
    for (int i = 0; i < N; i++) {
        intr_on();
        intr_off();
    }
    uint64 t3 = get_time();

    printf("[exp4] baseline loop cycles: %d\n", t1 - t0);
    printf("[exp4] with intr_on/off cycles: %d\n", t3 - t2);
}

// 触发一次 S 模式 ecall，用于测试 kerneltrap 中的异常处理
static void
do_smode_ecall(void)
{
    asm volatile("ecall");
}

void
test_exception_handling(void)
{
    printf("[exp4] Testing exception handling (S-mode ecall)...\n");

    uint64 t0 = r_time();
    do_smode_ecall();   // 这里会陷入 kerneltrap -> handle_kernel_exception
    uint64 t1 = r_time();

    printf("[exp4] Exception test finished, delta=%lu cycles\n", (unsigned long)(t1 - t0));
    printf("[exp4] Exception tests completed.\n");
}

static void
test_experiment4(void)
{
    printf("\n==== Experiment 4: interrupts & timer ====\n");

    // 1. 初始化 trap 系统并设置 stvec
    trapinit();
    trapinithart();

    // 2. 打开 S 模式中断总开关
    printf("[exp4] enabling S-mode interrupts...\n");
    intr_on();

    // 3. 按手册要求依次执行中断/性能
    test_timer_interrupt();
    test_interrupt_overhead();

    printf("[exp4] interrupt & timer tests finished.\n");
}

// ================= 实验5：进程管理与调度 =================

// --- 工具：打印进程状态 ---

static const char *
proc_state_name(procstate_t st)
{
    switch (st) {
    case PROC_UNUSED:   return "UNUSED";
    case PROC_RUNNABLE: return "RUNNABLE";
    case PROC_RUNNING:  return "RUNNING";
    case PROC_ZOMBIE:   return "ZOMBIE";
    default:            return "UNKNOWN";
    }
}

static void
debug_proc_table(const char *tag)
{
    printf("[exp5] === Process Table (%s) ===\n", tag ? tag : "");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        if (p->state != PROC_UNUSED || p->pid != 0 || p->name[0] != 0) {
            printf("[exp5] slot=%d pid=%d state=%d(%s) name=%s kstack=%p\n",
                   i,
                   p->pid,
                   (int)p->state,
                   proc_state_name(p->state),
                   p->name,
                   (uint64)p->kstack);
        }
    }
}

// -------- 5.1 进程创建测试：test_process_creation --------

// 简单任务：打印几次然后退出
static void
simple_task(void)
{
    int pid = current_proc ? current_proc->pid : -1;
    printf("[exp5] simple_task: pid=%d start\n", pid);

    for (int i = 0; i < 3; i++) {
        printf("[exp5] simple_task: pid=%d step=%d\n", pid, i);
        yield();
    }

    printf("[exp5] simple_task: pid=%d exit\n", pid);
    kproc_exit();
}

static void
test_process_creation(void)
{
    printf("[exp5] Testing process creation...\n");

    // 每个子测试前都重新初始化进程表
    proc_init();

    // 1. 基本的进程创建
    struct proc *first = kproc_create(simple_task, "simple0");
    if (first == 0) {
        panic("[exp5] test_process_creation: first create failed");
    }
    printf("[exp5] first simple_task pid=%d\n", first->pid);

    // 2. 测试进程表限制：循环创建，直到没有空槽
    int created = 1; // 已经创建了一个
    for (int i = 0; i < NPROC + 5; i++) {
        char name[16];
        int idx = i;
        int pos = 0;
        name[pos++] = 't';
        name[pos++] = 'h';
        name[pos++] = 'r';
        if (idx >= 10) {
            name[pos++] = '0' + (idx / 10);
            name[pos++] = '0' + (idx % 10);
        } else {
            name[pos++] = '0' + idx;
        }
        name[pos] = 0;

        struct proc *p = kproc_create(simple_task, name);
        if (p) {
            created++;
        } else {
            printf("[exp5] process table full after extra %d creates\n", i);
            break;
        }
    }

    printf("[exp5] total created kernel threads: %d (NPROC=%d)\n",
           created, NPROC);

    debug_proc_table("after creation");

    // 3. 进入调度器，直到所有线程都 exit
    scheduler_run();

    debug_proc_table("after scheduler_run");
    printf("[exp5] process creation test done.\n");
}

// -------- 5.2 调度器测试：test_scheduler --------

static void
cpu_intensive_task(void)
{
    int pid = current_proc ? current_proc->pid : -1;
    printf("[exp5] cpu_task: pid=%d start\n", pid);

    volatile uint64 sum = 0;
    const int MAX = 500000;

    for (int i = 0; i < MAX; i++) {
        sum += i;
        if (i % 100000 == 0) {
            printf("[exp5] cpu_task: pid=%d iter=%d/%d\n", pid, i, MAX);
            // 主动让出 CPU，验证轮转调度
            yield();
        }
    }

    printf("[exp5] cpu_task: pid=%d done, sum=%d\n", pid, (uint64)sum);
    kproc_exit();
}

static void
test_scheduler(void)
{
    printf("[exp5] Testing scheduler...\n");

    proc_init();

    // 创建多个“计算密集型”线程
    for (int i = 0; i < 3; i++) {
        struct proc *p = kproc_create(cpu_intensive_task, "cpu_task");
        if (p == 0) {
            panic("[exp5] test_scheduler: create cpu_task failed");
        }
    }

    debug_proc_table("before scheduler (scheduler test)");

    uint64 start_time = get_time();
    scheduler_run();
    uint64 end_time = get_time();

    printf("[exp5] scheduler_run() completed in %d cycles\n",
           end_time - start_time);

    debug_proc_table("after scheduler (scheduler test)");
}

// -------- 5.3 同步机制测试：test_synchronization --------

// 一个极简的“共享缓冲区”，用于生产者-消费者测试
struct shared_buffer {
    int  total_items;   // 要生产/消费的总数
    int  produced;      // 已生产数量
    int  consumed;      // 已消费数量
    int  count;         // 当前缓冲区中“可用项目”数量
    int  lock;          // 简单自旋锁（基于协作式调度，无需原子指令）
};

static struct shared_buffer sbuf;

// 简单的“自旋锁”，在当前协作式调度模型下是安全的：
// 只有在 yield()/kproc_exit 时才会切换线程。
static void
spin_lock(int *lk)
{
    while (*lk) {
        // 另一线程持有锁，主动让出 CPU
        yield();
    }
    *lk = 1;
}

static void
spin_unlock(int *lk)
{
    *lk = 0;
}

static void
shared_buffer_init(void)
{
    sbuf.total_items = 10;
    sbuf.produced    = 0;
    sbuf.consumed    = 0;
    sbuf.count       = 0;
    sbuf.lock        = 0;
}

static void
producer_task(void)
{
    int pid = current_proc ? current_proc->pid : -1;
    printf("[exp5] producer(pid=%d) start\n", pid);

    for (;;) {
        spin_lock(&sbuf.lock);
        if (sbuf.produced >= sbuf.total_items) {
            spin_unlock(&sbuf.lock);
            break;
        }

        int item = sbuf.produced;
        sbuf.produced++;
        sbuf.count++;
        printf("[exp5] producer: produced item %d, count=%d\n",
               item, sbuf.count);
        spin_unlock(&sbuf.lock);

        // 给消费者机会运行
        yield();
    }

    printf("[exp5] producer(pid=%d) finished, produced=%d\n",
           pid, sbuf.produced);
    kproc_exit();
}

static void
consumer_task(void)
{
    int pid = current_proc ? current_proc->pid : -1;
    printf("[exp5] consumer(pid=%d) start\n", pid);

    for (;;) {
        spin_lock(&sbuf.lock);

        // 所有项目都已消费且缓冲区为空，退出
        if (sbuf.consumed >= sbuf.total_items && sbuf.count == 0) {
            spin_unlock(&sbuf.lock);
            break;
        }

        if (sbuf.count > 0) {
            int item = sbuf.consumed;
            sbuf.consumed++;
            sbuf.count--;
            printf("[exp5] consumer: consumed item %d, count=%d\n",
                   item, sbuf.count);
            spin_unlock(&sbuf.lock);
        } else {
            // 当前没有可消费的项目
            spin_unlock(&sbuf.lock);
        }

        // 让出 CPU，等待生产者生产
        yield();
    }

    printf("[exp5] consumer(pid=%d) finished, consumed=%d\n",
           pid, sbuf.consumed);
    kproc_exit();
}

static void
test_synchronization(void)
{
    printf("[exp5] Testing synchronization (producer-consumer)...\n");

    proc_init();
    shared_buffer_init();

    struct proc *prod = kproc_create(producer_task, "producer");
    struct proc *cons = kproc_create(consumer_task, "consumer");
    if (prod == 0 || cons == 0) {
        panic("[exp5] test_synchronization: create producer/consumer failed");
    }

    debug_proc_table("before scheduler (sync test)");

    scheduler_run();

    printf("[exp5] after scheduler: produced=%d consumed=%d count=%d\n",
           sbuf.produced, sbuf.consumed, sbuf.count);

    if (sbuf.produced == sbuf.total_items &&
        sbuf.consumed == sbuf.total_items &&
        sbuf.count == 0) {
        printf("[exp5] synchronization test PASSED.\n");
    } else {
        printf("[exp5] synchronization test FAILED!\n");
    }

    debug_proc_table("after scheduler (sync test)");
}

static void
test_experiment5(void)
{
    printf("\n==== Experiment 5: process management & scheduler ====\n");

    // 对应手册中的三个测试
    test_process_creation();
    test_scheduler();
    test_synchronization();

    printf("[exp5] all Experiment 5 tests finished.\n");
}

// -------- 小工具：字符串长度（实验6 也用到） --------

static int
kstrlen(const char *s)
{
    int n = 0;
    if (s == 0)
        return 0;
    while (s[n])
        n++;
    return n;
}

// 新增：内存比较（实验 7 用）
static int
kmemcmp(const void *a, const void *b, int n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (int i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}


// -------- 实验6：系统调用框架测试 --------
//
// 在内核中“伪造”一个 syscall 调用环境：
//   - 构造一个假的 current_proc，用来测试 SYS_getpid
//   - 构造 syscall_frame，设置 a0~a7，然后调用 syscall()
//   - 测试：getpid / uptime / pause / test_add / test_str


// -------- Experiment 6: syscall 测试辅助 --------

extern struct proc *current_proc;
extern volatile uint64 ticks;   // 在 trap.c 中定义，用于时间/性能测试

// 用一个假的 proc 作为 current_proc，方便 sys_getpid 使用
static struct proc fake_proc;

static void
set_fake_current_proc(int pid)
{
    fake_proc.pid = pid;
    fake_proc.state = PROC_RUNNING;
    fake_proc.kstack = 0;
    fake_proc.context.ra = 0;
    fake_proc.context.sp = 0;
    for (int i = 0; i < (int)sizeof(fake_proc.name); i++) {
        fake_proc.name[i] = 0;
    }
    current_proc = &fake_proc;
}

// 封装一次 syscall 调用，方便下面的测试代码
static void
do_syscall(struct syscall_frame *f,
           uint64 num, uint64 a0, uint64 a1, uint64 a2)
{
    f->a0 = a0;
    f->a1 = a1;
    f->a2 = a2;
    f->a3 = 0;
    f->a4 = 0;
    f->a5 = 0;
    f->a6 = 0;
    f->a7 = num;   // 系统调用号
    syscall(f);
}

// ==================== 1) 基础功能测试 ====================
// 对应手册里的 test_basic_syscalls：测试基础 syscall 功能，
// 我们用 getpid / uptime / pause / 未知 syscall 来做基础测试。
static void
test_basic_syscalls(void)
{
    printf("[exp6] Testing basic system calls...\n");

    struct syscall_frame f;

    // 为本小节伪造一个“当前进程”，pid=100
    // 这样 sys_getpid() 才能返回 100
    set_fake_current_proc(100);

    // (1) 测试 getpid
    do_syscall(&f, SYS_getpid, 0, 0, 0);
    printf("[exp6] getpid() => %d (expected %d)\n",
           (int)f.a0, 100);

    // (2) 测试 uptime + pause（类似手册里用时间测试）
    do_syscall(&f, SYS_uptime, 0, 0, 0);
    uint64 t0 = f.a0;
    printf("[exp6] uptime before pause: %d\n", (int)t0);

    // 暂停约 5 个 tick
    do_syscall(&f, SYS_pause, 5, 0, 0);

    do_syscall(&f, SYS_uptime, 0, 0, 0);
    uint64 t1 = f.a0;
    printf("[exp6] uptime after pause: %d, delta=%d (should be >= 5)\n",
           (int)t1, (int)(t1 - t0));

    // (3) 测试未知 syscall 号，应该返回 -1
    do_syscall(&f, 999, 0, 0, 0);
    printf("[exp6] unknown syscall 999 => %d (expected -1)\n",
           (long)f.a0);
}


// ==================== 2) 参数传递测试 ====================
// 对应手册里的 test_parameter_passing：
// 它用 open/write/read 测不同类型参数和边界情况。:contentReference[oaicite:2]{index=2}
// 我们用 test_add (两个 int) 和 test_str (字符串) 来验证参数传递。
static void
test_parameter_passing(void)
{
    printf("[exp6] Testing parameter passing...\n");

    struct syscall_frame f;
    set_fake_current_proc(101);

    // (1) 测试整型参数传递：a0 + a1
    do_syscall(&f, SYS_test_add, 10, 32, 0);
    printf("[exp6] test_add(10, 32) => %d (expected 42)\n", (int)f.a0);

    // (2) 测试字符串参数传递
    const char *msg = "Hello, syscall!";
    do_syscall(&f, SYS_test_str, (uint64)msg, 0, 0);
    int expected_len = kstrlen(msg);
    printf("[exp6] test_str(\"%s\") => len=%d (expected %d)\n",
           msg, (int)f.a0, expected_len);

    // (3) 测试“长一点”的字符串（对应手册里的边界情况思路）
    const char *long_msg = "This is a very very long string used to test argstr";
    do_syscall(&f, SYS_test_str, (uint64)long_msg, 0, 0);
    printf("[exp6] test_str(long_msg) => len=%d (should be <= internal buffer size)\n",
           (int)f.a0);
}

// ==================== 3) 安全性测试 ====================
// 手册里的 test_security 用无效指针、大长度读写等测试安全检查。:contentReference[oaicite:3]{index=3}
// 在我们目前没有用户地址空间和真正 file 的情况下，
// 用 NULL 指针 + 负数参数这种“明显非法”的情况来测试 sys_* 的鲁棒性。
static void
test_security(void)
{
    printf("[exp6] Testing syscall 'security' cases (simplified)...\n");

    struct syscall_frame f;
    set_fake_current_proc(102);

    // (1) test_str(NULL)：argstr里会把 src==0 视为非法并返回 -1
    do_syscall(&f, SYS_test_str, 0, 0, 0);
    printf("[exp6] test_str(NULL) => %d (expected -1)\n", (long)f.a0);

    // (2) pause(-10)：sys_pause 里把负数 n 修正为 0，看是否正常返回
    do_syscall(&f, SYS_pause, (uint64)(-10), 0, 0);
    printf("[exp6] pause(-10) => %d (expected 0, argument clamped)\n",
           (long)f.a0);

    // 如果后续你在 argint/argstr 里增加更多检查，可以在这里继续加 case。
}

// ==================== 4) 性能测试 ====================
// 手册里的 test_syscall_performance 测 10000 次 getpid 的耗时。:contentReference[oaicite:4]{index=4}
// 我们同样做 10000 次 SYS_getpid 调用，利用 ticks 粗略估计时间。
static void
test_syscall_performance(void)
{
    printf("[exp6] Testing syscall performance: 10000 getpid()...\n");

    struct syscall_frame f;
    set_fake_current_proc(103);

    uint64 start_ticks = ticks;
    for (int i = 0; i < 10000; i++) {
        do_syscall(&f, SYS_getpid, 0, 0, 0);
    }
    uint64 end_ticks = ticks;

    printf("[exp6] 10000 getpid() took %d ticks (from %d to %d)\n",
           (int)(end_ticks - start_ticks),
           (int)start_ticks, (int)end_ticks);
}

// ======= 实验六总入口：在 run_all_tests() 里调用它 =======
static void
test_experiment6(void)
{
    printf("\n==== Experiment 6: syscall tests ====\n");
    intr_on();
    test_basic_syscalls();
    test_parameter_passing();
    test_security();
    test_syscall_performance();

    printf("[exp6] all syscall sub-tests finished.\n");
}

// ======= 文件系统测试初始化：调用 binit/fileinit/fsinit 一次 =======

static void
fs_test_init_once(void)
{
    static int initialized = 0;
    if (initialized)
        return;
    initialized = 1;

    printf("[exp7] initializing file system layer...\n");

    // 这三个函数分别在 bio.c / file.c / fs.c 中实现，
    // 名字按我们之前的设计与 xv6 保持一致。
    // binit();
    // fileinit();
    fs_init(1);   // 假定根设备号为 1（ROOTDEV=1）
}

// ======= Experiment 7: filesystem tests =======

// 利用 syscall() 做一层包装，方便在内核里直接调用 open/read/write 等。

static int
fs_sys_open(const char *path, int omode)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_open, (uint64)path, (uint64)omode, 0);
    return (int)f.a0;
}

static int
fs_sys_close(int fd)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_close, (uint64)fd, 0, 0);
    return (int)f.a0;
}

static int
fs_sys_write(int fd, const void *buf, int n)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_write, (uint64)fd, (uint64)buf, (uint64)n);
    return (int)f.a0;
}

static int
fs_sys_read(int fd, void *buf, int n)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_read, (uint64)fd, (uint64)buf, (uint64)n);
    return (int)f.a0;
}

static int
fs_sys_fstat(int fd, struct stat *st)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_fstat, (uint64)fd, (uint64)st, 0);
    return (int)f.a0;
}

static int
fs_sys_dup(int fd)
{
    struct syscall_frame f;
    do_syscall(&f, SYS_dup, (uint64)fd, 0, 0);
    return (int)f.a0;
}

// ==================== 1) 基本读写完整性测试 ====================
// 创建一个文件 -> 写入一段字符串 -> 重新打开读出 -> 比较内容 + fstat 检查 size

static void
test_fs_basic(void)
{
    printf("[exp7] test_fs_basic: create/write/read one file...\n");

    fs_test_init_once();
    set_fake_current_proc(201);

    const char *name = "fs_basic.txt";
    const char *msg  = "Hello, filesystem!";
    int len = kstrlen(msg);

    int fd = fs_sys_open(name, O_CREATE | O_RDWR);
    KASSERT(fd >= 0);

    int n = fs_sys_write(fd, msg, len);
    printf("[exp7]   write %d bytes to %s\n", n, name);
    KASSERT(n == len);

    struct stat st;
    int r = fs_sys_fstat(fd, &st);
    KASSERT(r == 0);
    printf("[exp7]   fstat: size=%d\n", (int)st.size);
    KASSERT((int)st.size == len);

    KASSERT(fs_sys_close(fd) == 0);

    // 重新以只读方式打开，读回验证
    fd = fs_sys_open(name, O_RDONLY);
    KASSERT(fd >= 0);

    char buf[64];
    n = fs_sys_read(fd, buf, sizeof(buf));
    KASSERT(n == len);
    buf[n] = 0;

    printf("[exp7]   read back: \"%s\"\n", buf);
    KASSERT(kmemcmp(buf, msg, len) == 0);

    KASSERT(fs_sys_close(fd) == 0);
    printf("[exp7] test_fs_basic OK.\n");
}

// ==================== 2) 大文件 / 跨 direct & indirect 块测试 ====================
// 写入 NDIRECT+2 个块，确保覆盖直接块 + 间接块，随后逐块读回校验。

static void
test_fs_large_file(void)
{
    printf("[exp7] test_fs_large_file: write & read multi-block file...\n");

    fs_test_init_once();
    set_fake_current_proc(202);

    const char *name = "fs_large.bin";
    int fd = fs_sys_open(name, O_CREATE | O_RDWR | O_TRUNC);
    KASSERT(fd >= 0);

    char wbuf[BSIZE];
    char rbuf[BSIZE];
    int blocks = NDIRECT + 2;   // 覆盖 direct + 一点 indirect，具体值看 fs.h

    for (int i = 0; i < blocks; i++) {
        // 每个块用不同的字节填充，方便验证
        for (int j = 0; j < BSIZE; j++) {
            wbuf[j] = (char)(i & 0xff);
        }
        int n = fs_sys_write(fd, wbuf, BSIZE);
        KASSERT(n == BSIZE);
    }

    KASSERT(fs_sys_close(fd) == 0);

    // 再次打开并逐块读回验证
    fd = fs_sys_open(name, O_RDONLY);
    KASSERT(fd >= 0);

    for (int i = 0; i < blocks; i++) {
        int n = fs_sys_read(fd, rbuf, BSIZE);
        KASSERT(n == BSIZE);
        for (int j = 0; j < BSIZE; j++) {
            if (rbuf[j] != (char)(i & 0xff)) {
                printf("[exp7]   large file mismatch at block %d, byte %d\n", i, j);
                panic("test_fs_large_file: data mismatch");
            }
        }
    }

    KASSERT(fs_sys_close(fd) == 0);
    printf("[exp7] test_fs_large_file OK (blocks=%d, blocksize=%d).\n",
           blocks, BSIZE);
}

// ==================== 3) dup / 文件偏移共享 测试 ====================
// 打开同一文件，调用 dup() 复制 fd，验证两个 fd 共享同一个文件偏移。

static void
test_fs_dup(void)
{
    printf("[exp7] test_fs_dup: check dup/offset behavior...\n");

    fs_test_init_once();
    set_fake_current_proc(203);

    const char *name = "fs_dup.txt";
    const char *msg  = "Hello";

    int fd = fs_sys_open(name, O_CREATE | O_RDWR | O_TRUNC);
    KASSERT(fd >= 0);
    int len = kstrlen(msg);
    KASSERT(fs_sys_write(fd, msg, len) == len);
    KASSERT(fs_sys_close(fd) == 0);

    fd = fs_sys_open(name, O_RDONLY);
    KASSERT(fd >= 0);
    int fd2 = fs_sys_dup(fd);
    KASSERT(fd2 >= 0);

    char a[3], b[3];
    int n;

    // 从 fd 读 2 字节 => "He"
    n = fs_sys_read(fd, a, 2);
    KASSERT(n == 2);
    a[2] = 0;

    // 从 fd2 再读 2 字节，如果偏移共享，则应该是 "ll"
    n = fs_sys_read(fd2, b, 2);
    KASSERT(n == 2);
    b[2] = 0;

    printf("[exp7]   dup result: first=\"%s\", second=\"%s\"\n", a, b);
    KASSERT(a[0] == 'H' && a[1] == 'e');
    KASSERT(b[0] == 'l' && b[1] == 'l');

    KASSERT(fs_sys_close(fd) == 0);
    KASSERT(fs_sys_close(fd2) == 0);

    printf("[exp7] test_fs_dup OK.\n");
}

// ==================== 4) 简单性能测试 ====================
// 创建若干小文件 + 一个适度较大的文件，粗略统计时间（cycle 数）

static void
test_fs_performance(void)
{
    printf("[exp7] test_fs_performance: small files + one large file...\n");

    fs_test_init_once();
    set_fake_current_proc(204);

    uint64 t0 = get_time();

    // 多个小文件：每个写少量数据
    for (int i = 0; i < 20; i++) {
        char name[32];
        int pos = 0;
        name[pos++] = 's';
        name[pos++] = 'f';
        name[pos++] = '_';
        if (i >= 10) {
            name[pos++] = '0' + (i / 10);
            name[pos++] = '0' + (i % 10);
        } else {
            name[pos++] = '0' + i;
        }
        name[pos] = 0;

        int fd = fs_sys_open(name, O_CREATE | O_RDWR | O_TRUNC);
        if (fd < 0)
            panic("test_fs_performance: open small file failed");
        char buf[32];
        for (int j = 0; j < 32; j++)
            buf[j] = (char)('A' + (i % 26));
        if (fs_sys_write(fd, buf, 32) != 32)
            panic("test_fs_performance: small write failed");
        fs_sys_close(fd);
    }

    uint64 t1 = get_time();

    // 一个稍大文件：写入若干个块
    const char *lname = "fs_perf_large";
    int fd = fs_sys_open(lname, O_CREATE | O_RDWR | O_TRUNC);
    if (fd < 0)
        panic("test_fs_performance: open large file failed");
    char lbuf[BSIZE];
    for (int j = 0; j < BSIZE; j++)
        lbuf[j] = 'P';
    for (int i = 0; i < 16; i++) {   // 16 * BSIZE 字节
        if (fs_sys_write(fd, lbuf, BSIZE) != BSIZE)
            panic("test_fs_performance: large write failed");
    }
    fs_sys_close(fd);

    uint64 t2 = get_time();

    printf("[exp7]   20 small files took %d cycles; large file took %d cycles\n",
           (int)(t1 - t0), (int)(t2 - t1));
}

// ======= 实验七总入口 =======

static void
test_experiment7(void)
{
    printf("\n==== Experiment 7: file system ====\n");

    test_fs_basic();
    test_fs_large_file();
    test_fs_dup();
    test_fs_performance();

    printf("[exp7] all file system tests finished.\n");
}


static void
test_experiment8_fs_diagnostics(void)
{

    // 这里默认你已经跑过 exp7，fs_init 已完成，superblock 已就绪
    debug_filesystem_state();
    debug_inode_usage();

    int r = fsck_lite();
    if (r == 0) {
    } else {
    }
}

// -------- 实验8（project4）：内核日志系统 klog 测试 --------
static void
test_klog_system(void)
{
    printf("\n==== Experiment 8 (project4): kernel logging system ====\n");

    // 1) 初始化并关闭同步控制台输出（避免刷屏），只用 dump 展示
    klog_init();
    klog_enable_console(0);
    klog_set_level(KLOG_DEBUG);

    // 2) 基本写入 + 格式化
    KLOGI("klog", "hello %d", 123);
    KLOGW("klog", "warn hex=%x ptr=%p", 0xdeadbeef, (uint64)&test_klog_system);
    KLOGE("klog", "error msg=%s", "something happened");

    struct klog_stats st = klog_get_stats();
    KASSERT(st.count == 3);
    KASSERT(st.filtered == 0);

    // 3) 级别过滤：把阈值提高到 WARN
    klog_set_level(KLOG_WARN);
    KLOGI("klog", "this info should be filtered");
    KLOGW("klog", "this warn should be kept");

    st = klog_get_stats();
    KASSERT(st.filtered >= 1);   // 至少过滤掉 1 条
    KASSERT(st.count == 4);      // 之前 3 条 + 本次 warn 1 条

    // 4) 环形缓冲区覆盖测试：清空 count，但保留统计（便于观察）
    klog_clear();
    st = klog_get_stats();
    KASSERT(st.count == 0);

    klog_set_level(KLOG_DEBUG);

    // 写入超过 KLOG_NENTRY，触发覆盖
    for (int i = 0; i < KLOG_NENTRY + 10; i++) {
        KLOGD("ovf", "i=%d", i);
    }

    st = klog_get_stats();
    KASSERT(st.count == KLOG_NENTRY);
    KASSERT(st.overwritten >= 10);

    // 5) 最后 dump 最近 8 条日志，作为验收展示
    printf("[exp8-klog] dump last 8 lines:\n");
    klog_dump(8);

    printf("[exp8-klog] klog tests OK.\n");
}


// 实验2：printf 测试与调试策略（单函数，详细输出）
void
test_printf_strategy_verbose(void)
{
  printf("\n==== Experiment 2: printf test & debug strategy (verbose) ====\n");

  // ------------------------------------------------------------
  // 1) 底层验证：先确保单字符输出正常
  // ------------------------------------------------------------
  printf("\n[1/4] Low-level validation: single-character output\n");
  printf("  Goal   : verify 'one char at a time' path is stable (printf->console->uart)\n");
  printf("  Expect : characters should appear in-order, no missing chars, newline works\n");

  printf("  Output : BEGIN\n");
  printf("    (1) raw chars: ");
  printf("A"); printf("B"); printf("C"); printf("D"); printf("E");
  printf("  <- should be ABCDE\n");
  printf("    (2) with spaces: ");
  printf("X"); printf(" "); printf("Y"); printf(" "); printf("Z");
  printf("  <- should be X Y Z\n");
  printf("    (3) newline test: line1\n");
  printf("                     line2\n");
  printf("  Output : END\n");

  printf("  Debug hints:\n");
  printf("    - If output is incomplete: check uart_putc waits for TX-ready; check string termination.\n");

  // ------------------------------------------------------------
  // 2) 数字转换：单独测试各种数字格式
  // ------------------------------------------------------------
  printf("\n[2/4] Number conversion: %d %u %x %p\n");
  printf("  Goal   : verify printint/printptr conversion, sign handling, base-10/base-16\n");
  printf("  Expect : decimal correct; INT_MIN printed correctly; hex lower-case; pointer has 0x prefix\n");

  // 2.1 signed decimal
  printf("  Output : signed decimal\n");
  printf("    (a) positive  42        => %d\n", 42);
  printf("    (b) negative  -123      => %d\n", -123);
  printf("    (c) zero      0         => %d\n", 0);
  printf("    (d) INT_MAX   2147483647=> %d\n", 2147483647);
  printf("    (e) INT_MIN  -2147483648=> %d\n", -2147483648);

  // 2.2 unsigned decimal
  printf("  Output : unsigned decimal\n");
  printf("    (f) 0u                => %u\n", 0u);
  printf("    (g) 42u               => %u\n", 42u);
  printf("    (h) 3000000000u       => %u\n", 3000000000u);

  // 2.3 hex
  printf("  Output : hex (lower-case expected)\n");
  printf("    (i) 0x0               => 0x%x\n", 0x0);
  printf("    (j) 0xABC             => 0x%x\n", 0xABC);
  printf("    (k) 0xdeadbeef        => 0x%x\n", 0xdeadbeef);

  // 2.4 pointer
  printf("  Output : pointer\n");
  int local = 123;
  printf("    (l) &local            => %p (should look like 0x...)\n", (uint64)&local);
  printf("    (m) (void*)0          => %p (NULL pointer formatting)\n", (uint64)0);

  printf("  Debug hints:\n");
  printf("    - If numbers are wrong: re-check base conversion, negative handling, INT_MIN handling.\n");
  printf("    - If %p is wrong: confirm fixed-width hex and 0x prefix policy.\n");

  // ------------------------------------------------------------
  // 3) 字符串处理：测试各种字符串边界情况
  // ------------------------------------------------------------
  printf("\n[3/4] String handling: %s %c %%\n");
  printf("  Goal   : verify string printing does not crash and respects terminator\n");
  printf("  Expect : normal string prints; empty string prints nothing between quotes; NULL prints (null) or equivalent\n");

  printf("  Output : string\n");
  printf("    (a) normal:   \"%s\"\n", "Hello");
  printf("    (b) empty :   \"%s\"  (should be just quotes)\n", "");
  printf("    (c) NULL  :   \"%s\"  (should NOT crash)\n", (char*)0);

  printf("  Output : char/percent\n");
  printf("    (d) char  :   '%c'\n", 'X');
  printf("    (e) percent:  %%\n");

  printf("  Debug hints:\n");
  printf("    - If crash on %%s: add NULL guard, print (null) or empty.\n");
  printf("    - If output keeps going: string might be missing NUL terminator.\n");

  // ------------------------------------------------------------
  // 4) 综合测试：复杂格式字符串测试（覆盖解析与参数匹配）
  // ------------------------------------------------------------
  printf("\n[4/4] Integrated tests: mixed format / parser robustness\n");
  printf("  Goal   : verify parser state machine, va_arg type matching, and recovery on unknown specifier\n");
  printf("  Expect : values appear in correct order; unknown spec handled gracefully (no crash)\n");

  printf("  Output : mixed format A\n");
  printf("    fmt : \"pid=%d state=%s addr=%p flag=%c percent=%% hex=0x%x\" \n");
  printf("    out : pid=%d state=%s addr=%p flag=%c percent=%% hex=0x%x\n",
         1, "RUNNING", (uint64)&local, 'Y', 0x2f);

  printf("  Output : mixed format B (neg + unsigned + string)\n");
  printf("    out : neg=%d u=%u s=%s\n", -10, 3000000000u, "mix-test");

  printf("  Output : unknown specifier recovery\n");
  printf("    fmt : \"unknown=%%q then d=%%d\" (implementation-defined recovery)\n");
  // 如果你的 printf 实现遇到未知格式符是“打印 % 和该字符”，这里会很直观。
  printf("    out : unknown=%q then d=%d\n", 7);

  printf("  Debug hints:\n");
  printf("    - If parser breaks: print intermediate state around '%%' handling.\n");
  printf("    - If values mismatch: ensure va_arg types match format (e.g., %%p takes uint64).\n");

  printf("\n==== Experiment 2: printf test & debug strategy (verbose) DONE ====\n\n");
}


void
run_all_tests(void)
{
    test_experiment1();
    test_experiment2();
    test_experiment3();
    test_experiment4();
    test_experiment5();
    test_experiment6();
    test_experiment7();
    test_experiment8_fs_diagnostics();
    test_klog_system();
    intr_off();
    test_printf_strategy_verbose();
}
