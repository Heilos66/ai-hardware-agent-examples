/**
 * @file goldie_osal_win_compat.c
 * @brief libwinvm.a 缺失的 OSAL destroy 函数补齐 (仅 Windows 模拟器)。
 *
 * libwinvm.a 提供 goldie_sem_init/wait/post 与 goldie_mutex_init/lock/unlock,
 * 但未导出对应的 destroy: 原 WIN 工程从不销毁信号量/互斥锁, 库中因此没有
 * 这两个符号。sdk_integration 音频线程与 WIN 平台适配层 (与 ws63 对齐)
 * 需要调用它们, 在此补齐。
 *
 * winvm 内部句柄结构不公开, 无法安全释放底层资源, 故提供空实现:
 * 不销毁底层句柄。模拟器场景下泄漏有界 (桥接每次启停/重连仅个位数句柄),
 * 进程退出时由操作系统统一回收, 可接受。
 *
 * 注意: 若未来 libwinvm.a 更新并导出这两个符号, 需删除本文件
 * (whole-archive 链接下强符号会冲突)。
 */
#include "goldie_osal.h"

void goldie_sem_destroy(goldie_sem *sem)
{
    (void)sem;  /* no-op: 见文件头注释 */
}

void goldie_mutex_destroy(goldie_mutex *mutex)
{
    (void)mutex;  /* no-op: 见文件头注释 */
}
