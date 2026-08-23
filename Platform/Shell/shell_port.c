/**
 ******************************************************************************
 * @file    shell_port.c
 * @brief   Letter Shell 3.1 移植层：LPUART1 + FreeRTOS（静态任务，零堆分配）
 *
 * 说明：
 *  - TX：HAL 阻塞发送（shell 任务上下文，100ms 超时）
 *  - RX：LPUART1 中断接收 -> 环形缓冲 -> shell 任务轮询取字节
 *  - shell 任务使用静态栈/静态TCB（FreeRTOS 堆仅 3072 字节，避免堆分配）
 ******************************************************************************
 */
#include "shell.h"
#include "shell_port.h"

#include "stm32wlxx_hal_uart.h"
#include "stm32wlxx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* UART 句柄（定义于 main.c） */
extern UART_HandleTypeDef hlpuart1;

/* ------------------------- RX 环形缓冲 ------------------------- */
#define SHELL_RX_BUF_SIZE   256U
#define SHELL_RX_BUF_MASK   (SHELL_RX_BUF_SIZE - 1U)

static volatile uint8_t  shellRxByte;         /* 当前接收字节（ISR 与回调共享） */
static volatile uint16_t shellRxHead;         /* 写入头（中断上下文） */
static volatile uint16_t shellRxTail;         /* 读取尾（shell 任务） */
static uint8_t           shellRxBuf[SHELL_RX_BUF_SIZE];

/* ------------------------- shell 对象 ------------------------- */
static Shell shell;
static char  shellBuffer[512];

/* ------------------------- 静态任务资源 ------------------------- */
#define SHELL_TASK_STACK_SIZE  4096U /* 字节 */
static StaticTask_t shellTaskTCB;
static StackType_t  shellTaskStack[SHELL_TASK_STACK_SIZE / sizeof(StackType_t)];

/**
 * @brief UART 接收完成回调（中断上下文）
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)
  {
    uint16_t next = (uint16_t)((shellRxHead + 1U) & SHELL_RX_BUF_MASK);
    if (next != shellRxTail)
    {
      shellRxBuf[shellRxHead] = shellRxByte;
      shellRxHead = next;
    }
    /* 重新武装，接收下一个字节 */
    HAL_UART_Receive_IT(huart, (uint8_t *)&shellRxByte, 1);
  }
}

/**
 * @brief shell 读函数（由 shell 任务调用，无数据时让出 CPU）
 */
short shellPortRead(char *data, unsigned short len)
{
  unsigned short cnt = 0;
  while (cnt < len)
  {
    if (shellRxHead != shellRxTail)
    {
      data[cnt++] = shellRxBuf[shellRxTail];
      shellRxTail = (uint16_t)((shellRxTail + 1U) & SHELL_RX_BUF_MASK);
    }
    else if (cnt > 0)
    {
      break;
    }
    else
    {
      /* 暂无数据：延时 1ms 等待，避免空转 */
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  return (short)cnt;
}

/**
 * @brief shell 写函数（阻塞发送）
 */
short shellPortWrite(char *data, unsigned short len)
{
  if (HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, len, 100) == HAL_OK)
  {
    return (short)len;
  }
  return 0;
}

/**
 * @brief shell 任务入口
 */
static void ShellTask(void *argument)
{
  (void)argument;

  shell.write = shellPortWrite;
  shell.read  = shellPortRead;
  shellInit(&shell, shellBuffer, sizeof(shellBuffer));

  /* SHELL_TASK_WHILE == 1：内部死循环，永不返回 */
  shellTask(&shell);

  /* 理论不可达 */
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
 * @brief 初始化 Letter Shell
 */
void ShellInit(void)
{
  /* 启动 UART 中断接收（先于调度器启动，字节会先入缓冲） */
  HAL_UART_Receive_IT(&hlpuart1, (uint8_t *)&shellRxByte, 1);

  /* 创建 shell 任务：静态栈 + 静态 TCB，优先级低于 defaultTask */
  xTaskCreateStatic(ShellTask,                       /* 任务函数 */
                    "shellTask",                     /* 任务名 */
                    (uint32_t)(sizeof(shellTaskStack) / sizeof(StackType_t)), /* 栈(字) */
                    NULL,                            /* 参数 */
                    (tskIDLE_PRIORITY + 1),          /* 优先级 */
                    shellTaskStack,                  /* 栈内存 */
                    &shellTaskTCB);                  /* TCB */
}

/* ------------------------- ROM Bootloader 跳转 ------------------------- */

/** @brief bootloader 请求标志：复位后保留（链接脚本 .bootflag 段，非 .bss） */
__attribute__((section(".bootflag"))) volatile uint32_t shellBootFlag = 0U;

/** @brief STM32WL 系统存储器（ROM bootloader）基址 */
#define SYSTEM_FLASH_BASE_ADDR   0x1FFF0000UL

/**
 * @brief 跳转到系统 ROM bootloader（在系统复位后从 main() 调用）
 *
 * STM32WL ROM bootloader 的 USART2 位于 PA2/PA3，与板载 VCP 引脚一致，
 * 可用 STM32CubeProgrammer 通过 UART 下载固件。
 */
void SystemBootloaderJump(void)
{
  const volatile uint32_t *romVt = (const volatile uint32_t *)SYSTEM_FLASH_BASE_ADDR;
  uint32_t bootMsp     = romVt[0];
  uint32_t bootAddr    = romVt[1];
  void (*bootJump)(void) = (void (*)(void))bootAddr;

  /* ROM 向量有效性检查：复位向量应落在 System Memory 区间 (0x1FFF0000 - 0x1FFF6FFF) */
  if ((bootAddr < SYSTEM_FLASH_BASE_ADDR) || (bootAddr > (SYSTEM_FLASH_BASE_ADDR + 0x6FFFU)))
  {
    /* ROM 无效：清除标志，回退正常启动 */
    shellBootFlag = 0U;
    return;
  }

  __disable_irq();
  __set_MSP(bootMsp);
  bootJump();

  /* 理论不可达 */
  for (;;)
  {
  }
}
