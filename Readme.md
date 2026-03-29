# STM32WL55JC1 架构说明

本文档给出一个面向嵌入式传感器项目的目录分层方案，目标是满足以下几点：

- 结构清晰，便于长期维护
- 与 STM32CubeMX 生成工程兼容，减少冲突
- 应用代码不直接依赖 HAL 和具体传感器芯片
- 后续新增传感器、显示屏、Flash、LoRa 业务时改动范围可控

该方案强调“够用即可”，避免为了抽象而引入过重的设备管理框架。

---

## 设计目标

本工程建议围绕以下原则组织代码：

1. `CM4` 仅负责启动、初始化和调度，不承载业务逻辑
2. `App` 只写任务编排和业务流程，不直接调用 `HAL_*`
3. 具体器件驱动统一收敛到 `Device`
4. 总线、GPIO、时钟、板级资源等底层能力统一收敛到 `Platform`
5. STM32Cube 原生目录尽量保持不动，避免后续重新生成代码时冲突

---

## 推荐目录结构

```text
stm32wl55jc1/
├── App/                       应用层：任务、状态机、业务流程
│   ├── Inc/
│   └── Src/
│
├── Services/                  服务层：采集、缓存、打包、告警、上报
│   ├── Inc/
│   └── Src/
│
├── Device/                    设备层：具体器件适配
│   ├── Inc/
│   └── Src/
│
├── Platform/                  平台层：板级资源、总线封装、基础设施
│   ├── Board/
│   │   ├── Inc/
│   │   └── Src/
│   ├── Bus/
│   │   ├── Inc/
│   │   └── Src/
│   └── System/
│       ├── Inc/
│       └── Src/
│
├── Drivers/                   STM32Cube HAL / BSP / 官方组件驱动
├── Middlewares/               FreeRTOS / LoRaWAN 等协议栈
├── CM4/                       Cortex-M4 启动、外设初始化、中断入口
├── CM0PLUS/                   Cortex-M0+ 无线核相关代码
├── Common/                    双核共享与芯片系统文件
├── Makefile/
├── newlib_lock_glue.c
├── stm32_lock.h
└── Readme.md
```

---

## 分层职责

### 1. App

`App` 是应用层，只负责业务任务和系统流程控制。

建议放置：

- `app_main.c`
- `app_task_sensor.c`
- `app_task_comm.c`
- `app_task_monitor.c`
- `app_state_machine.c`

这一层应该做的事情：

- 创建和组织任务
- 管理系统状态机
- 调用 `Services` 提供的业务接口
- 协调采集、上报、监控等顶层流程

这一层不应该做的事情：

- 直接调用 `HAL_SPI_Transmit()`、`HAL_I2C_Mem_Read()` 等底层接口
- 直接操作 GPIO 或外设句柄
- 解析具体传感器寄存器

---

### 2. Services

`Services` 是业务服务层，用于承载比任务更稳定、可复用的项目逻辑。

建议放置：

- `sensor_service.c`：统一采集多个传感器
- `telemetry_service.c`：组织上报数据包
- `health_service.c`：系统健康监控、看门狗喂狗
- `storage_service.c`：本地缓存或掉电存储策略
- `lora_service.c`：LoRa 业务调度

这一层适合承载：

- 采样时序管理
- 数据滤波和校准
- 多传感器融合
- 告警阈值判断
- 上报帧打包
- 失败重试和缓存策略

这一层存在的意义是避免把所有逻辑都堆到 `task_sensor.c` 这类任务文件中。

---

### 3. Device

`Device` 是设备层，用于适配具体器件，把不同芯片统一包装成项目可直接使用的接口。

建议放置：

- `stts22h_device.c`
- `iis2mdc_device.c`
- `ism330dhcx_device.c`
- `mx25l4006_device.c`
- `ssd1306_device.c`

这一层应该做的事情：

- 封装具体芯片初始化流程
- 基于 `Platform/Bus` 访问 I2C、SPI、GPIO
- 调用 ST 官方组件驱动并向上提供统一接口
- 屏蔽具体芯片寄存器细节

建议按“设备类型能力”设计接口，而不是把所有设备硬塞进统一的 `read/write/ioctl` 模型。例如：

- 温度传感器：`read_temperature()`
- IMU：`read_accel()`、`read_gyro()`
- Flash：`read()`、`write()`、`erase()`
- OLED：`draw_text()`、`refresh()`

如果确实需要统一基础接口，建议只保留轻量公共接口：

- `init()`
- `deinit()`
- `self_test()`
- `get_status()`

然后针对不同设备类型扩展专用接口，而不是过度依赖 `ioctl`。

---

### 4. Platform

`Platform` 是平台层，负责底层通用能力和板级资源管理。

建议拆成三个子目录。

#### Platform/Board

用于管理板级资源和硬件连线关系。

建议放置：

- `board.h`
- `board_pins.h`
- `board.c`
- `board_power.c`

负责内容：

- GPIO 引脚定义
- 片选引脚定义
- 中断引脚定义
- 电源使能脚定义
- 板上设备实例和接线关系
- 板级初始化与上电控制

这是传感器项目里非常关键的一层。所有“这个传感器挂在哪个总线、哪个引脚”这类信息，都应该集中放在这里，而不是散落到 `main.c`、`HAL MSP` 或具体设备文件里。

#### Platform/Bus

用于对底层总线和基础外设进行薄封装。

建议放置：

- `platform_spi.c`
- `platform_i2c.c`
- `platform_uart.c`
- `platform_gpio.c`
- `platform_time.c`
- `platform_lock.c`

负责内容：

- SPI、I2C、UART、GPIO 操作封装
- 延时、时间戳、超时管理
- 锁、临界区、线程安全辅助
- 对 HAL 句柄和错误码做统一包装

注意这里强调“薄封装”，只封装真正需要统一和复用的部分，不建议再造一套复杂 HAL。

#### Platform/System

用于存放跨模块但不属于具体设备的公共基础设施。

建议放置：

- `platform_assert.c`
- `platform_log.c`
- `platform_error.c`
- `ring_buffer.c`
- `event_helper.c`

如果项目规模较小，这部分可以暂时精简，甚至并入 `Platform/Bus` 或 `App` 的公共模块中。

---

### 5. Drivers

`Drivers` 保持 STM32Cube 目录风格，尽量不要修改其源码。

主要包括：

- `STM32WLxx_HAL_Driver`
- `BSP`
- `BSP/Components`
- `CMSIS`

这一层建议遵循的原则：

- 可以调用，不建议直接修改
- 第三方或官方组件驱动尽量通过 `Device` 包一层再对上层暴露
- 不要让 `App` 直接依赖 `Drivers/BSP/Components` 的头文件

---

### 6. Middlewares

`Middlewares` 放置通用中间件和协议栈。

例如：

- `FreeRTOS`
- `LoRaWAN`

建议做法：

- 协议栈源码保留在 `Middlewares`
- 针对项目的业务封装放在 `Services`

也就是说，LoRaWAN 协议本体属于 `Middlewares`，而“什么时候发、发什么、重试策略如何处理”属于 `Services`。

---

### 7. CM4 / CM0PLUS / Common

这三个目录继续保持当前 STM32WL 双核工程的风格即可。

职责建议如下：

- `CM4`：M4 启动、RTOS 初始化、外设初始化、中断入口
- `CM0PLUS`：无线核相关启动和控制逻辑
- `Common`：系统时钟、双核共享资源、芯片系统文件

其中 `CM4/Core/Src/main.c` 建议只做最小启动流程：

1. `HAL_Init()`
2. `SystemClock_Config()`
3. 初始化必要外设
4. 初始化 RTOS
5. 调用 `App` 的入口函数或创建应用任务

不要在 `main.c` 中堆积业务逻辑。

---

## 推荐依赖关系

各层依赖方向建议保持单向：

```text
App
	↓
Services
	↓
Device
	↓
Platform
	↓
Drivers / Middlewares
```

约束原则：

- `App` 不直接调用 `HAL_*`
- `Services` 不直接操作 GPIO、I2C、SPI
- `Device` 不反向依赖 `App`
- `Drivers` 不依赖项目业务层

单向依赖可以显著降低后续替换传感器、调整业务流程时的改动范围。

---

## 推荐调用链

以“采集温度并通过 LoRa 上报”为例，典型调用路径如下：

```text
App/app_task_sensor.c
	└─ sensor_service_collect()
			 └─ stts22h_device_read_temperature()
						└─ platform_i2c_mem_read()
								 └─ HAL_I2C_Mem_Read()
```

再例如“上报业务”：

```text
App/app_task_comm.c
	└─ telemetry_service_send()
			 └─ lora_service_uplink()
						└─ LoRaWAN stack
```

---

## 新增模块时的放置原则

### 新增一个传感器

如果是 ST 官方已提供组件驱动的器件：

1. 官方组件源码放到 `Drivers/BSP/Components/<chip>/`
2. 在 `Device/` 中新增适配层文件
3. 在 `Platform/Board/` 中补充连线和实例配置
4. 在 `Services/` 中决定如何被采集和上报

### 新增一个 SPI 或 I2C 外设

例如 OLED、EEPROM、自定义 ADC 扩展芯片：

1. 总线访问通过 `Platform/Bus/` 完成
2. 器件驱动写在 `Device/`
3. 不要把具体外设逻辑直接写进 `App`

### 新增板级资源

例如新增电源开关脚、中断脚、复位脚：

- 放到 `Platform/Board/`
- 不要分散在 `main.c`、设备驱动和业务层中

---

## 文件命名建议

为了避免同类文件越来越多后难以区分，建议使用统一前缀：

- `app_*`：应用任务和应用入口
- `*_service`：业务服务层
- `*_device`：器件适配层
- `platform_*`：总线与基础设施层
- `board_*`：板级资源与板级控制

示例：

- `app_task_sensor.c`
- `sensor_service.c`
- `stts22h_device.c`
- `platform_i2c.c`
- `board_power.c`

---

## 不建议的做法

以下做法不适合中小型嵌入式传感器项目：

- 为所有设备强行设计一套复杂的 `open/read/write/ioctl` 框架
- 在 `App` 中直接包含具体芯片驱动头文件
- 在多个目录中重复封装同一层抽象
- 把板级引脚和器件接线关系分散到各处
- 大量修改 `Drivers` 和 STM32Cube 自动生成文件

这些做法会让项目在初期看起来“通用”，但在后续迭代中往往会增加维护成本。

---

## 迁移建议

结合当前工程状态，推荐按以下顺序逐步演进，而不是一次性大改：

1. 保持 `Drivers`、`Middlewares`、`CM4`、`CM0PLUS`、`Common` 不动
2. 在工程中新增 `Services`、`Device`、`Platform`
3. 先把一个真实器件迁入 `Device`，验证接口边界
4. 把总线操作收敛到 `Platform/Bus`
5. 把板级脚位和设备实例收敛到 `Platform/Board`
6. 最后再把 `App` 中的业务流程逐步迁移为“调用服务层”模式

这样改动风险更低，也更容易和 CubeMX 生成代码共存。

---

## 总结

对于 STM32WL 传感器项目，推荐采用以下核心结构：

- `App`：任务和业务流程
- `Services`：采集、告警、缓存、上报等业务服务
- `Device`：具体器件适配
- `Platform`：板级资源、总线封装、公共基础设施
- `Drivers / Middlewares`：STM32Cube HAL、BSP、协议栈

这套结构相比“设备管理器 + 驱动框架”的方案更直接，也更适合中小型嵌入式传感器工程落地。

---

## 以 OLED 驱动和显示业务为例的一句话归类

下面以“新增一个 SPI 接口的 SSD1306 OLED 显示器，并在屏幕上显示传感器数据”为例，用一句话说明当前项目每个目录应该放什么代码。

- `App/`：放应用任务和业务流程，例如“每 1 秒刷新一次 OLED 页面”“按键切换显示页面”“异常时显示告警信息”。
- `Services/`：放可复用业务服务，例如“整理温湿度、电量、LoRa 状态后生成 OLED 页面数据”或“统一管理显示内容刷新策略”。
- `Device/`：放项目里的 OLED 设备适配代码，例如 `oled_ssd1306_device.c`，负责把 SSD1306 包装成 `oled_show_text()`、`oled_clear()`、`oled_refresh()` 这类项目接口。
- `Platform/Board/`：放 OLED 在本板上的连接关系，例如 CS、DC、RST 分别接哪个 GPIO、是否有电源使能脚、上电和复位顺序是什么。
- `Platform/Bus/`：放 OLED 依赖的通用底层访问代码，例如 SPI 发送、GPIO 拉高拉低、毫秒延时、互斥锁等。
- `Platform/System/`：放 OLED 和其他模块都可能复用的基础设施，例如日志、错误码、断言、环形缓冲区、事件辅助函数。
- `Drivers/`：放底层原始驱动或第三方组件代码，例如 `ssd1306.c/.h` 这类只关心芯片命令、显存和刷屏协议的原始驱动，或 ST 官方/第三方提供的组件源码。
- `Middlewares/`：放通用中间件和协议栈，例如 FreeRTOS、LoRaWAN，本身不直接承载 OLED 业务页面逻辑。
- `CM4/`：放 M4 启动、RTOS 初始化和外设初始化入口，例如在 `main.c` 里完成 SPI、GPIO、RTOS 初始化后启动应用任务，但不写具体 OLED 页面逻辑。
- `CM0PLUS/`：放无线核相关代码，通常与 OLED 驱动无直接关系，除非你做了双核协同显示控制。
- `Common/`：放系统时钟、芯片公共启动文件、双核共享系统代码，不放具体 OLED 逻辑。
- `Makefile/`：放构建脚本，需要把新增的 `Device`、`Platform`、`Services` 或 `Drivers/Components/ssd1306` 源文件加入编译列表。

如果只记一条最实用的判断规则：

- 芯片本身怎么驱动，放 `Drivers`
- 这个芯片在本项目里怎么被调用，放 `Device`
- 这个芯片在本板上怎么连接、怎么通信，放 `Platform`
- 这个芯片在业务上显示什么内容，放 `Services` 或 `App`


TODO 传感器相关的先把接口放Device里，后面统一抽象出sensor的设备对象