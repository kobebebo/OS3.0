#include "console.h"
#include "printf.h"
#include "test.h"
#include "fs.h"    // fs_init, ROOTDEV
#include "file.h"  // fileinit

int
main(void)
{
    // 初始化 UART + 控制台
    console_init();

    // 初始化 printf
    printf_init();

    // 初始化文件系统（块缓存 / 超级块 / inode 缓存 / 日志）
    fs_init(ROOTDEV);

    // 初始化全局打开文件表
    fileinit();

    // 运行所有实验的测试代码（包括实验七）
    run_all_tests();

    // 最后保持死循环
    while (1) {
        // idle...
    }

    return 0;
}
