#ifndef _TRAP_H_
#define _TRAP_H_

// 初始化简单 trap 系统（主要是 ticks）
void trapinit(void);

// 为当前 hart 设置 stvec -> kernelvec
void trapinithart(void);

#endif
