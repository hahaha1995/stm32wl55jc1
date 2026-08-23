/**
 ******************************************************************************
 * @file    shell_cmd_user.c
 * @brief   Letter Shell 用户命令示例
 *
 * 新增命令：复制本文件里的 SHELL_EXPORT_CMD 模式即可。
 * 注意：命令函数是 int (*)(int argc, char *argv[]) 形式。
 ******************************************************************************
 */
#include "shell.h"
#include "main.h"
#include "stm32wlxx_nucleo.h"
#include "shell_port.h"

/**
 * @brief hello：打招呼
 */
int shellCmdHello(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  shellPrint(shellGetCurrent(), "Hello, this is STM32WL55 CM4!\r\n");
  return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 hello, shellCmdHello, say hello);

/**
 * @brief led：翻转蓝色 LED
 */
int shellCmdLed(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  BSP_LED_Toggle(LED_BLUE);
  shellPrint(shellGetCurrent(), "LED toggled\r\n");
  return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 led, shellCmdLed, toggle blue led);

/**
 * @brief tick：显示系统运行时间
 */
int shellCmdTick(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  shellPrint(shellGetCurrent(), "HAL tick: %lu ms\r\n", (unsigned long)HAL_GetTick());
  return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 tick, shellCmdTick, show uptime ms);

/**
 * @brief download：进入系统 ROM bootloader，停留在烧录阶段
 *
 * 流程：
 *  1. shell 打印提示，设置复位后保留的 bootflag（.bootflag 段）
 *  2. 系统复位（同时停止 IWDG，避免 bootloader 期间被看门狗复位）
 *  3. main() 检测到 bootflag 后跳转到 ROM bootloader（USART2 @ PA2/PA3）
 *  4. 主机侧执行：make download COM=COMx 通过 STM32CubeProgrammer 下载
 *     build/ 下的 hex 文件，-rst 复位后自动运行新固件
 */
int shellCmdDownload(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  shellPrint(shellGetCurrent(),
             "\r\nEntering ROM bootloader... "
             "On host run: make download COM=<COMx>\r\n");
  shellBootFlag = SHELL_BOOTFLAG_MAGIC;
  NVIC_SystemReset();
  /* 理论不可达 */
  return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 download, shellCmdDownload, enter ROM bootloader for UART download);
