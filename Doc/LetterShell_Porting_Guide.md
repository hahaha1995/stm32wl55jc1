# Letter Shell 移植操作指南（STM32WL55JC1 + FreeRTOS）

> 适用工程：`stm32wl55jc1`（CM4 核，STM32CubeMX 生成 + Makefile 构建）
> Shell 版本：Letter Shell 3.x（`letter-shell/src`，`shell_cfg.h` 标注 3.0.0）
> 硬件：NUCLEO-WL55JC1，板载 ST-LINK VCP 接 **LPUART1 @ PA2/PA3（AF8）**

本文档基于本工程**已验证可编译**的移植实现编写，记录从零接入 Letter Shell 的全部步骤，
以及本工程中踩过的坑（串口引脚映射、IWDG 与 bootloader 下载、链接脚本命令段等）。

---

## 1. 概述

Letter Shell 是一个轻量级嵌入式命令行工具，支持：

- 命令导出宏（`SHELL_EXPORT_CMD`），无需手写命令表
- Tab 补全、历史命令、帮助信息（`help`）
- 支持用户/权限/锁（本工程未启用权限体系）
- 纯静态内存（配合静态 TCB/静态栈，可实现零堆分配）

本工程采用 **LPUART1 中断接收 + FreeRTOS 静态任务** 的移植方式：

- TX：HAL 阻塞发送（shell 任务上下文，100 ms 超时）
- RX：LPUART1 中断接收 → 环形缓冲（256 B）→ shell 任务轮询取字节
- shell 任务：静态栈（4 KB）+ 静态 TCB，FreeRTOS 堆仅 3072 B，避免资源不足

```
┌──────────┐   USART IRQ    ┌────────────┐   poll    ┌─────────────┐
│ LPUART1  ├───────────────►│ 环形缓冲    │──────────►│ shell 任务    │
│ RX ISR   │   (256B)       │            │           │ shellTask()  │
└──────────┘                └────────────┘           └──────┬──────┘
                                                     write  │ HAL_UART_Transmit
                                                            ▼
                                                     LPUART1 TX (PA2) → VCP
```

---

## 2. 文件架构

| 路径 | 作用 |
|---|---|
| `Middlewares/Third_Party/letter-shell/src/shell.c` | 核心：解析、执行、补全、历史 |
| `Middlewares/Third_Party/letter-shell/src/shell.h` | 核心头文件：`Shell` 结构体、导出宏 |
| `Middlewares/Third_Party/letter-shell/src/shell_cfg.h` | **配置头**：功能开关（后续修改都在这里） |
| `Middlewares/Third_Party/letter-shell/src/shell_ext.c/.h` | 扩展组件（伴生对象等，本工程仅包含未使用） |
| `Platform/Shell/shell_port.c` | **移植层**：读写函数、shell 任务、bootloader 跳转 |
| `Platform/Shell/shell_port.h` | 移植层接口：`ShellInit()`、`SystemBootloaderJump()` |
| `Platform/Shell/shell_cmd_user.c` | 用户命令（`hello` / `led` / `tick` / `download`） |
| `CM4/Core/Src/main.c` | 调用 `ShellInit()`；启动时检测 bootloader 标志 |
| `CM4/Core/Src/stm32wlxx_it.c` | `LPUART1_IRQHandler` → `HAL_UART_IRQHandler` |
| `CM4/Core/Src/stm32wlxx_hal_msp.c` | LPUART1 GPIO（PA2/PA3 AF8）与 NVIC 配置 |
| `Makefile/CM4/Makefile` | 源文件/头文件路径、`download` 下载目标 |
| `Makefile/CM4/STM32WL55XX_FLASH_CM4.ld` | `.shellCommand` 命令段、`.bootflag` 标志段 |

> `shell_port.c/h` 与 `shell_cmd_user.c` 属于"移植层"和"应用命令"，位于 `Platform/` 下，
> 与 CubeMX 生成目录（`CM4/`、`Drivers/`）解耦，重新生成代码不会被覆盖。

---

## 3. 移植步骤

### 3.1 集成源码

1. 将 letter-shell 源码放入 `Middlewares/Third_Party/letter-shell/src/`：
   - `shell.c`、`shell.h`、`shell_cfg.h`、`shell_ext.c`、`shell_ext.h`
2. 创建 `Platform/Shell/` 目录，放置：
   - `shell_port.c`、`shell_port.h`、`shell_cmd_user.c`

### 3.2 配置 `shell_cfg.h`

关键配置项（本工程取值）：

```c
#define SHELL_TASK_WHILE            1      // shellTask() 内部死循环，配合 FreeRTOS 任务
#define SHELL_USING_CMD_EXPORT      1      // 使用 SHELL_EXPORT_CMD 导出命令
#define SHELL_USING_COMPANION       0      // 不使用伴生对象
#define SHELL_PRINT_BUFFER          128    // shellPrint 格式化缓冲（栈上）
#define SHELL_GET_TICK()            HAL_GetTick()   // 提供系统时间（双击 Tab 帮助、超时）
#define SHELL_SHOW_INFO             1      // 登录/复位时显示 ASCII banner
#define SHELL_CLS_WHEN_LOGIN        1      // 登录后清屏
#define SHELL_DEFAULT_USER          "letter"  // 默认用户名（shell.c 中自动导出该用户）
#define SHELL_DEFAULT_USER_PASSWORD ""      // 空密码 = 免登录
#define SHELL_LOCK_TIMEOUT          0      // 关闭自动锁定
```

> ⚠️ `SHELL_DEFAULT_USER` 必须能通过 `shellSeekCommand()` 找到名为 `letter` 的用户条目。
> `shell.c` 内部已通过 `SHELL_SECTION("shellCommand")` 自动导出该用户；若你另写命令表模式
> （`SHELL_USING_CMD_EXPORT=0`），必须**手动添加该用户**，否则 `shellWritePrompt()` 会对
> `shell->info.user` 解引用空指针 → HardFault。

### 3.3 实现移植层（`shell_port.c`）

#### (1) 写函数（TX）

```c
short shellPortWrite(char *data, unsigned short len)
{
  if (HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, len, 100) == HAL_OK)
  {
    return (short)len;
  }
  return 0;
}
```

- 在**任务上下文**中调用，允许阻塞；100 ms 超时防止 UART 异常时卡死任务。

#### (2) 读函数（RX）

```c
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
      vTaskDelay(pdMS_TO_TICKS(1));   // 无数据让出 CPU，避免空转
    }
  }
  return (short)cnt;
}
```

#### (3) UART 接收中断回调

```c
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
    HAL_UART_Receive_IT(huart, (uint8_t *)&shellRxByte, 1);  // 重新武装
  }
}
```

要点：
- 使用**单字节**接收（`HAL_UART_Receive_IT(&hlpuart1, &shellRxByte, 1)`），每字节一次中断，实现简单可靠。
- 环形缓冲 256 B 必须为 2 的幂，头尾指针用掩码回绕。
- 中断回调内**禁止**调用任何 FreeRTOS API（此处仅纯 C 操作）。

#### (4) Shell 任务（静态资源）

```c
static StaticTask_t shellTaskTCB;
static StackType_t  shellTaskStack[SHELL_TASK_STACK_SIZE / sizeof(StackType_t)]; // 4096 B

static void ShellTask(void *argument)
{
  (void)argument;
  shell.write = shellPortWrite;
  shell.read  = shellPortRead;
  shellInit(&shell, shellBuffer, sizeof(shellBuffer));
  shellTask(&shell);   // SHELL_TASK_WHILE=1，内部死循环
}
```

- 必须**先注册 `write/read` 再调用 `shellInit()`**，否则 `shellInit` 末尾的
  `shellWritePrompt()` 输出失败。
- `shellBuffer[512]` 会被 shell 按 `SHELL_HISTORY_MAX_NUMBER+1` 等分为编辑缓冲 + 历史记录，
  512 B 提供约 85 字符编辑区（`512/(5+1)`），够常规调试使用。

#### (5) 初始化入口（`ShellInit`）

```c
void ShellInit(void)
{
  HAL_UART_Receive_IT(&hlpuart1, (uint8_t *)&shellRxByte, 1);  // 启动 RX
  xTaskCreateStatic(ShellTask, "shellTask",
                    sizeof(shellTaskStack) / sizeof(StackType_t),
                    NULL, tskIDLE_PRIORITY + 1,
                    shellTaskStack, &shellTaskTCB);
}
```

调用时机：**`osKernelInitialize()` 之后、`osKernelStart()` 之前**。
（`xTaskCreateStatic` 允许在调度器启动前调用；先于调度器启动的 RX 中断收到的字节会先进入缓冲。）

### 3.4 接入 FreeRTOS（`main.c`）

```c
/* USER CODE BEGIN RTOS_THREADS */
ShellInit();   /* Letter Shell on LPUART1 */
/* USER CODE END RTOS_THREADS */
```

优先级建议：shell 任务使用较低优先级（`tskIDLE_PRIORITY+1`），避免抢占业务任务。

### 3.5 中断处理（`stm32wlxx_it.c`）

```c
void LPUART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&hlpuart1);
}
```

并在 `stm32wlxx_hal_msp.c` 中使能：

```c
HAL_NVIC_SetPriority(LPUART1_IRQn, 5, 0);
HAL_NVIC_EnableIRQ(LPUART1_IRQn);
```

> 若在 CubeMX 中重新生成，务必确认 `LPUART1 global interrupt` 已勾选，
> 否则 RX 永远收不到数据（TX 仍可正常输出 prompt）。

### 3.6 UART 引脚与时钟（⚠️ 本工程的重要坑）

**NUCLEO-WL55JC1 板载 ST-LINK VCP 连接的是 `LPUART1` 的 PA2/PA3（AF8）**，
而不是 PC0/PC1。若按手册把 LPUART1 配到 PC0/PC1，串口将**完全无输出**（不是乱码）。

`stm32wlxx_hal_msp.c` 正确配置：

```c
PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LPUART1;
PeriphClkInitStruct.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;

__HAL_RCC_GPIOA_CLK_ENABLE();                     // 注意是 GPIOA 不是 GPIOC
GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;    // PA2=TX, PA3=RX
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;     // AF8
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

`main.c` 波特率：`hlpuart1.Init.BaudRate = 115200;`（8N1，无流控）。

> 不同开发板 VCP 映射不同（`B-WL5M-SUBG` 为 USART2@PA2/PA3）。移植到其他板时，
> 先查 BSP 头文件中 `COM1_UART*` 宏确认引脚与外设！

### 3.7 定义用户命令（`shell_cmd_user.c`）

```c
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
```

- 命令函数签名固定：`int (*)(int argc, char *argv[])`
- `SHELL_EXPORT_CMD(权限|类型, 名字, 函数, 描述)`，描述显示在 `help` 中
- 输出统一用 `shellPrint(shellGetCurrent(), ...)`，支持 `%d/%s/%lu` 等格式化

### 3.8 Makefile 配置

`C_SOURCES` 增加：

```makefile
../../Middlewares/Third_Party/letter-shell/src/shell.c \
../../Middlewares/Third_Party/letter-shell/src/shell_ext.c \
../../Platform/Shell/shell_port.c \
../../Platform/Shell/shell_cmd_user.c
```

`C_INCLUDES` 增加：

```makefile
-I../../Middlewares/Third_Party/letter-shell/src \
-I../../Platform/Shell
```

### 3.9 链接脚本（关键！GCC 命令导出依赖）

`SHELL_USING_CMD_EXPORT=1` 时，所有 `SHELL_EXPORT_CMD` 数据被放入 `shellCommand` section，
`shellInit()` 通过 `_shell_command_start/_shell_command_end` 定位命令表。
**链接脚本必须定义这两个符号**，否则链接报 undefined reference 或命令为空：

```ld
.shellCommand :
{
  . = ALIGN(8);
  _shell_command_start = .;
  KEEP(*(shellCommand))       /* KEEP 防止 --gc-sections 裁掉命令 */
  _shell_command_end = .;
  . = ALIGN(8);
} >ROM
```

---

## 4. 使用说明

- 串口参数：**115200 / 8N1 / 无流控**（板载 VCP 忽略波特率，但串口助手建议设为 115200）
- 上电后应看到 ASCII 欢迎信息与提示符 `letter:/$ `

常用命令：

| 命令 | 说明 |
|---|---|
| `help` | 列出所有命令 |
| `hello` | 打招呼 |
| `led` | 翻转蓝色 LED |
| `tick` | 显示 HAL tick（运行时间 ms） |
| `download` | 进入 ROM bootloader，等待 UART 下载固件（见第 5 节） |
| `Tab` | 命令补全；双击 Tab 显示帮助 |
| `↑/↓` | 历史命令 |
| `Ctrl+C` | 取消当前输入 |

---

## 5. 固件下载（download 命令 + make download）

### 5.1 原理

```
shell: download
  → 写 .bootflag 标志（0x4C53424C，位于链接脚本 .bootflag 段，系统复位后保留）
  → NVIC_SystemReset()     // 顺便停止 IWDG（STM32WL 的 IWDG 启动后无法软件停止）
main(): 检测到标志
  → 清除标志
  → SystemBootloaderJump() // 跳转 ROM bootloader @ 0x1FFF0000
  → 停留在 bootloader（USART2 @ PA2/PA3，与板载 VCP 同引脚）
PC: make download COMX=5
  → STM32CubeProgrammer 通过 COM5 下载 build/*.hex → -v 校验 → -rst 复位运行新固件
```

### 5.2 链接脚本 `.bootflag` 段

```ld
.bootflag (NOLOAD) :
{
  . = ALIGN(4);
  KEEP(*(.bootflag))
  . = ALIGN(4);
} >RAM1
```

- `NOLOAD` 且位于 `.bss` 之后，startup 的 `.data/.bss` 初始化**不会**清掉它，复位后值保留。
- `FLASH_OPTR` 的 `SRAM_RST` 选项需保持默认（0），否则系统复位会擦除 SRAM 导致标志丢失。

### 5.3 使用流程

```bash
# 1. 首次用 ST-LINK 烧录含 download 命令的固件（或直接 make download 前先在 shell 中执行 download）
# 2. 串口输入:
download
# 3. 主机执行:
cd Makefile/CM4
make download COMX=5        # 或 make download COMX=COM5
```

- `make download` 要求**必须**传 `COMX`（`5` 或 `COM5` 皆可），不传会提示用法并报错退出。
- 工具路径默认 `C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin`，
  可通过 `make download COMX=5 STM32CUBEPROGRAMMER_DIR="你的路径"` 覆盖。

---

## 6. 常见问题（FAQ）

| 现象 | 原因与解决 |
|---|---|
| 上电串口**完全无输出** | 引脚映射错误。核对 VCP 实际连接的外设/引脚（NUCLEO-WL55JC1 = LPUART1@PA2/PA3 AF8）；检查 GPIOA 时钟使能 |
| 有输出但**乱码** | 波特率不匹配（本工程 115200）；或主时钟/分频配置被改动 |
| 能输出 prompt，**输入无回显** | `LPUART1 global interrupt` 未使能 / `LPUART1_IRQHandler` 缺失 / `HAL_UART_Receive_IT` 未启动 |
| 输入 `help` 命令列表为空 | `.shellCommand` 段未 KEEP 或未定义 `_shell_command_start/end`；或被 `--gc-sections` 裁掉 |
| 上电 HardFault / 无任何输出且 LED 不闪 | `SHELL_DEFAULT_USER` 找不到对应用户时 `shellSetUser(NULL)` 空指针解引用（检查是否改成了命令表模式，或误删默认用户） |
| 移植到 CM0+ 或其他 RTOS | 本例为 CM4 + FreeRTOS。无 RTOS 时去掉任务、改为 `SHELL_TASK_WHILE=0` 并在主循环调用 `shellTask(&shell)`，`shellPortRead` 改为非阻塞轮询 |
| 编辑器报"未定义标识符" | IntelliSense 未配置 include 路径（`Middlewares/.../letter-shell/src`、`Platform/Shell`），与编译无关，忽略或更新 `c_cpp_properties.json` |
| `download` 后 STM32CubeProgrammer 连不上 | 确认串口是 COM 正确；确认 VCP 能识别；ROM bootloader 的 USART2=PA2/PA3 必须与线一致；若 IWDG 为硬件模式会周期复位，需通过选项字节关闭 |

---

## 7. 关键经验总结

1. **先确认板载 VCP 的引脚/外设映射**（BSP 头文件的 `COM1_*` 宏），再决定串口外设，这是最常见的坑。
2. **移植层职责单一**：`shell_port.c` 只做 HAL/RTOS 桥接，命令放在 `shell_cmd_user.c`，不要混。
3. **静态资源避免堆分配**:堆很小时（本工程 3072 B）使用 `xTaskCreateStatic` + 静态缓冲，省去 malloc。
4. **中断回调保持极简**：ISR 内只做"入队列 + 重新武装"，其它都交给任务。
5. **bootloader 跳转用"标志 + 复位"而不是直接跳转**：STM32WL 的 IWDG 无法软件停止，系统复位是清掉看门狗的唯一途径。
