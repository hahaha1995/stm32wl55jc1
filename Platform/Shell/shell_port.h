/**
 ******************************************************************************
 * @file    shell_port.h
 * @brief   Letter Shell 3.1 port interface (STM32WL55JC1 CM4)
 ******************************************************************************
 */
#ifndef __SHELL_PORT_H__
#define __SHELL_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Letter Shell（创建 shell 任务、启动 UART RX 中断接收）
 * @note  必须在 osKernelInitialize() 之后、osKernelStart() 之前调用
 */
void ShellInit(void);

/* ------------------------- ROM Bootloader 跳转 ------------------------- */

/** @brief bootloader 请求魔数（位于 .bootflag 段，系统复位后保留） */
#define SHELL_BOOTFLAG_MAGIC   0x4C53424CUL  /* "LSBL" */

/** @brief bootloader 请求标志（复位后保留，见链接脚本 .bootflag 段） */
extern volatile uint32_t shellBootFlag;

/**
 * @brief 跳转到系统 ROM bootloader（STM32WL system memory @ 0x1FFF0000）
 * @note  - 应在系统复位后、外设初始化前调用（由 main() 检测 shellBootFlag 触发）
 *        - ROM bootloader 的 USART2 位于 PA2/PA3，与板载 VCP 引脚一致
 *        - 跳转后 MCU 停留在 bootloader 等待 UART 下载，完成后 -rst 复位运行新固件
 */
void SystemBootloaderJump(void);

#ifdef __cplusplus
}
#endif

#endif /* __SHELL_PORT_H__ */
