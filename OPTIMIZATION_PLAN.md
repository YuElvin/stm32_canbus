# STM32H750 CAN 网关项目优化方案

**制定日期**：2026-05-25
**依据**：`REVIEW_REPORT.md` 审核结论
**目标**：在进入阶段 2（FDCAN）之前，消除 P0/P1 风险项，改善代码可维护性

---

## 总览

优化分为 4 个批次，按优先级递减排列。每批次可独立完成并提交，建议逐批执行、逐批验证。

| 批次 | 内容 | 预计耗时 | 涉及文件 |
|---|---|---|---|
| 1 | 文档修正 + 看门狗 + sprintf 安全 | 2 小时 | 5 个文件 |
| 2 | 数据竞争修复 + 配置集中化 | 2 小时 | 6 个文件（含 1 个新建） |
| 3 | 代码去重 + FreeRTOS 任务优化 | 3 小时 | 4 个文件 |
| 4 | 工程清理 + 代码风格统一 | 3 小时 | 10+ 个文件 |

---

## 批次 1：安全与文档（P0）

### 1.1 修正 CLAUDE.md 中过时的架构约束

**问题**：CLAUDE.md 的"D-Cache 规则"表格和"ETH_PAD_SIZE"描述与当前代码矛盾，按文档操作会引入回归。

**修改内容**：

`CLAUDE.md` 的"关键架构约束"第 1 节，将 D-Cache 规则表替换为：

```markdown
### 1. STM32H7 D-Cache 规则

当前采用 **Non-Cacheable D2 SRAM** 方案（MPU Region 0），ETH DMA 描述符和 Rx 缓冲池
均在 Non-Cacheable 区域，因此 **不需要手动 Clean/Invalidate D-Cache**。

| 区域 | 地址 | MPU 属性 | 用途 |
|---|---|---|---|
| D2 SRAM | 0x30000000, 256KB | Non-Cacheable | DMA 描述符 + Rx Pool + LwIP Heap |
| AXI SRAM | 0x24000000, 512KB | Cacheable | 代码/数据/pbuf payload |

`low_level_output()` 中 pbuf payload 在 Cacheable 的 AXI SRAM，但当前使用
`HAL_ETH_Transmit_IT` + 零拷贝路径，ETH DMA 直接读取 pbuf 数据。
由于 Rx Pool 在 Non-Cacheable 区域，接收路径无需 Cache 维护。

如未来将 Rx Pool 移至 Cacheable 区域，需重新加入 `SCB_InvalidateDCache_by_Addr`。
```

`CLAUDE.md` 的 LwIP 参数表中，将 `ETH_PAD_SIZE` 行替换为：

```markdown
| `ETH_PAD_SIZE` | `0`（不设置） | 已通过覆写 `SMEMCPY` 为字节拷贝解决对齐问题，
  设置 `ETH_PAD_SIZE=2` 反而会导致 LwIP 内部 pbuf 对齐计算错误 |
```

### 1.2 修正 README.md 中过时的描述

**修改 `README.md` 的"关键设计决策"章节**：

```markdown
## 关键设计决策

- **D-Cache 处理**：ETH DMA 描述符和 Rx 缓冲池放在 D2 SRAM（MPU Non-Cacheable），
  不需要手动 Clean/Invalidate D-Cache。见 `lwipopts.h` 注释。
- **ETH_PAD_SIZE=0**：通过覆写 `SMEMCPY` 为逐字节拷贝解决 Cortex-M7 非对齐访问问题，
  而非添加以太网帧 padding。见 `lwipopts.h:124-138`。
- **FreeRTOS 堆 32KB**：LwIP + ETH 需 4 个任务 + 队列/信号量，默认 15KB 不够。
- **PHY BSR 探测**：LAN8720 无 SMR 寄存器（lan8742.c 的 SMR 扫描不兼容），
  改用 BSR 探测地址 0/1。
```

### 1.3 启用 IWDG 看门狗

**涉及文件**：`stm32h7xx_hal_conf.h`、`main.c`、`freertos.c`

**步骤 1** — 启用 HAL IWDG 模块：

`stm32h7xx_hal_conf.h:64`：
```c
// 原：/* #define HAL_IWDG_MODULE_ENABLED   */
#define HAL_IWDG_MODULE_ENABLED
```

**步骤 2** — 添加 IWDG 初始化（CubeMX 未配置，手动添加）：

`main.c` 中，在 `MX_GPIO_Init()` 之前添加：
```c
IWDG_HandleTypeDef hiwdg;

static void MX_IWDG_Init(void) {
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;  /* 32kHz/256 = 125Hz */
  hiwdg.Init.Reload = 1000;                    /* 1000/125 = 8 秒超时 */
  hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
    Error_Handler();
  }
}
```

在 `main()` 的外设初始化序列中调用：
```c
MX_GPIO_Init();
MX_IWDG_Init();    /* 看门狗尽早启动 */
```

**步骤 3** — 在 heartbeatTask 中喂狗：

`freertos.c` 的 `StartHeartbeatTask` 函数中：
```c
void StartHeartbeatTask(void *argument) {
  (void)argument;
  for (;;) {
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);
    HAL_IWDG_Refresh(&hiwdg);  /* 每 500ms 喂狗 */
    osDelay(500);
  }
}
```

需要在 `freertos.c` 顶部添加 `extern IWDG_HandleTypeDef hiwdg;`。

### 1.4 sprintf 替换为 snprintf

**涉及文件**：`main.c`、`stm32h7xx_it.c`、`ethernetif.c`

全局替换规则：
- `sprintf(buf, ...)` → `snprintf(buf, sizeof(buf), ...)`
- `sprintf(msg, ...)` → `snprintf(msg, sizeof(msg), ...)`
- `sprintf(dbg, ...)` → `snprintf(dbg, sizeof(dbg), ...)`
- `sprintf(m, ...)` → `snprintf(m, sizeof(m), ...)`

具体修改点：

| 文件 | 行 | 原 | 改为 |
|---|---|---|---|
| `main.c` | 211 | `sprintf(buf, "\r\n[ASSERT] %s:%lu\r\n", ...)` | `snprintf(buf, sizeof(buf), ...)` |
| `main.c` | 292 | `sprintf(buf, "\r\n[ERROR_HANDLER] LR=0x%08lX\r\n", ...)` | `snprintf(buf, sizeof(buf), ...)` |
| `stm32h7xx_it.c` | 60 | `sprintf(buf, ...)` | `snprintf(buf, sizeof(buf), ...)` |
| `ethernetif.c` | 328 | `sprintf(msg, "[ETH] LAN8720 addr=%lu, link=%s\r\n", ...)` | `snprintf(msg, sizeof(msg), ...)` |
| `ethernetif.c` | 875 | `sprintf(dbg, "[ETH] raw link: %ld -> %ld\r\n", ...)` | `snprintf(dbg, sizeof(dbg), ...)` |
| `ethernetif.c` | 881 | `sprintf(dbg, "[ETH] stat rx=%lu tx=%lu ...\r\n", ...)` | `snprintf(dbg, sizeof(dbg), ...)` |
| `ethernetif.c` | 960 | `sprintf(m, "[ETH] link change: %s %s\r\n", ...)` | `snprintf(m, sizeof(m), ...)` |

**验证**：全量编译通过，确认无 `-Wformat-truncation` 警告。

---

## 批次 2：数据竞争与配置管理（P1）

### 2.1 修复 RxAllocStatus 数据竞争

**涉及文件**：`ethernetif.c`

**步骤 1** — 声明为 volatile：
```c
// 原：static uint8_t RxAllocStatus;
static volatile uint8_t RxAllocStatus;
```

**步骤 2** — 读写处加临界区保护：

`low_level_input()` 中读取处（约 line 506）：
```c
taskENTER_CRITICAL();
uint8_t alloc_status = RxAllocStatus;
RxAllocStatus = RX_ALLOC_OK;
taskEXIT_CRITICAL();
if (alloc_status == RX_ALLOC_ERROR) {
  return NULL;
}
```

`pbuf_free_custom()` 中读取处（约 line 640）：
```c
taskENTER_CRITICAL();
if (RxAllocStatus == RX_ALLOC_ERROR) {
  RxAllocStatus = RX_ALLOC_OK;
  taskEXIT_CRITICAL();
  /* 通知 ETH 输入任务 */
  if (osSemaphoreRelease(RxPktSemaphore) == osOK) {
    SCB_InvalidateDCache_by_Addr(...);
  }
} else {
  taskEXIT_CRITICAL();
}
```

`HAL_ETH_RxAllocateCallback()` 中写入处（约 line 989）在 ISR 上下文，不需要临界区（ISR 优先级高于所有任务），但需确保变量声明为 `volatile`。

### 2.2 创建 app_config.h 集中配置

**新建文件**：`Core/Inc/app_config.h`

```c
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ========== 网络配置 ========== */
#define APP_IP_ADDR_1       192
#define APP_IP_ADDR_2       168
#define APP_IP_ADDR_3       1
#define APP_IP_ADDR_4       88

#define APP_NETMASK_1       255
#define APP_NETMASK_2       255
#define APP_NETMASK_3       255
#define APP_NETMASK_4       0

#define APP_GW_ADDR_1       192
#define APP_GW_ADDR_2       168
#define APP_GW_ADDR_3       1
#define APP_GW_ADDR_4       1

#define APP_MAC_ADDR0       0x00
#define APP_MAC_ADDR1       0x80
#define APP_MAC_ADDR2       0xE1
#define APP_MAC_ADDR3       0x00
#define APP_MAC_ADDR4       0x00
#define APP_MAC_ADDR5       0x00

/* ========== UART 配置 ========== */
#define APP_UART_BAUDRATE   115200
#define APP_UART_TIMEOUT_MS 200

/* ========== ETH 配置 ========== */
#define APP_ETH_RX_BUF_CNT      12
#define APP_ETH_RX_BUF_SIZE     1536
#define APP_ETH_LINK_DEBOUNCE_MS 500  /* 5 * 100ms */

/* ========== FreeRTOS 任务栈（单位：字节） ========== */
#define APP_DEFAULT_TASK_STACK      2048
#define APP_HEARTBEAT_TASK_STACK    512
#define APP_ETHIF_TASK_STACK        2048
#define APP_ETHLINK_TASK_STACK      2048

/* ========== FreeRTOS 任务优先级 ========== */
#define APP_HEARTBEAT_PRIO      osPriorityLow
#define APP_ETHLINK_PRIO        osPriorityBelowNormal
#define APP_ETHIF_PRIO          osPriorityAboveNormal  /* 原 osPriorityRealtime，降级 */
#define APP_DEFAULT_PRIO        osPriorityNormal

#endif /* APP_CONFIG_H */
```

**后续步骤** — 逐步将各文件中的硬编码值替换为 `app_config.h` 中的宏：

| 文件 | 替换内容 |
|---|---|
| `lwip.c:66-77` | IP/Netmask/Gateway 字节赋值 → 使用 `APP_IP_ADDR_*` 等宏 |
| `ethernetif.c:229-234` | MAC 地址 → 使用 `APP_MAC_ADDR*` 宏 |
| `ethernetif.c:105` | `ETH_RX_BUFFER_CNT` → `APP_ETH_RX_BUF_CNT` |
| `ethernetif.c:859` | `STABLE_THRESHOLD` → 计算自 `APP_ETH_LINK_DEBOUNCE_MS` |
| `ethernetif.c:287` | EthIf 优先级 → `APP_ETHIF_PRIO` |
| `freertos.c:55,64` | 栈大小和优先级 → 使用 `APP_*` 宏 |
| 多处 `HAL_UART_Transmit` | timeout → `APP_UART_TIMEOUT_MS` |

### 2.3 修复 MAC 地址冲突

`ethernetif.c:229-234` 和 `stm32h7xx_hal_conf.h:226-231` 定义了不同的 MAC 地址。

**方案**：`ethernetif.c` 中的 `low_level_init()` 使用 `app_config.h` 中的统一宏，
删除 `stm32h7xx_hal_conf.h` 中的 `ETH_MAC_ADDR0..5` 定义（CubeMX 生成的默认值，未被使用）。

---

## 批次 3：代码去重与任务优化（P2）

### 3.1 提取 PHY 链路速率配置函数

**涉及文件**：`ethernetif.c`

`low_level_init()`（line 351-373）和 `ethernet_link_thread()`（line 918-942）中有
完全相同的 switch-case，提取为：

```c
/**
 * 根据 PHY 链路状态配置 ETH MAC 的速率和双工模式
 * @param link_state  LAN8742_GetLinkState() 返回值
 * @param heth        ETH 外设句柄
 * @return 0=配置成功, -1=链路断开或错误
 */
static int phy_apply_link_config(uint32_t link_state, ETH_HandleTypeDef *heth) {
  ETH_MACConfigTypeDef MACConf = {0};
  uint32_t duplex, speed;

  switch (link_state) {
    case LAN8742_STATUS_100MBITS_FULLDUPLEX:
      duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_100M; break;
    case LAN8742_STATUS_100MBITS_HALFDUPLEX:
      duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_100M; break;
    case LAN8742_STATUS_10MBITS_FULLDUPLEX:
      duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_10M; break;
    case LAN8742_STATUS_10MBITS_HALFDUPLEX:
      duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_10M; break;
    default:
      return -1;
  }

  if (HAL_ETH_GetMACConfig(heth, &MACConf) != HAL_OK) return -1;
  MACConf.DuplexMode = duplex;
  MACConf.Speed = speed;
  if (HAL_ETH_SetMACConfig(heth, &MACConf) != HAL_OK) return -1;

  return 0;
}
```

两处调用点改为：
```c
phy_apply_link_config(PHYLinkState, &heth);
```

### 3.2 消除重复 extern 声明

| 文件 | 当前 | 改为 |
|---|---|---|
| `main.c:200` | `extern UART_HandleTypeDef huart2;` | `#include "usart.h"`（已 include 则删除重复行） |
| `stm32h7xx_it.c:54` | `extern UART_HandleTypeDef huart2;` | `#include "usart.h"` |
| `ethernetif.c:38` | `extern UART_HandleTypeDef huart2;` | `#include "usart.h"` |
| `stm32h7xx_it.c:70` | `extern ETH_HandleTypeDef heth;` | `#include "lwip.h"` 或 `#include "ethernetif.h"` |
| `lwip.c:37` | `extern void Error_Handler(void);` | `#include "main.h"` |
| `ethernetif.h:38` | `extern void Error_Handler(void);` | `#include "main.h"` |

### 3.3 降低 EthIf 任务优先级 + 启用栈溢出检测

**步骤 1** — 降低优先级：

`ethernetif.c:287`：
```c
// 原：osThreadNew(ethernetif_input, netif, &attributes);  // attributes 用 osPriorityRealtime
// 改为使用 app_config.h 中的 APP_ETHIF_PRIO (osPriorityAboveNormal)
```

**步骤 2** — 启用栈溢出检测：

`FreeRTOSConfig.h`：
```c
// 原：/* #define configCHECK_FOR_STACK_OVERFLOW 0 */
#define configCHECK_FOR_STACK_OVERFLOW  2  /* 方法 1 + 方法 2 */
```

在 `freertos.c` 中实现钩子函数：
```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  /* 栈溢出是致命错误，打印任务名后停机 */
  char buf[64];
  snprintf(buf, sizeof(buf), "\r\n[STACK_OVF] %s\r\n", pcTaskName);
  HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 200);
  for (;;) {}
}
```

需要 `#include "usart.h"` 和 `extern UART_HandleTypeDef huart2;`（或通过 `app_config.h` 统一）。

### 3.4 拆分 low_level_init()

将 `ethernetif.c:216-399` 的 183 行函数拆分为：

```c
/* 1. ETH HAL + 内存池初始化 */
static void eth_hal_init(ETH_HandleTypeDef *heth, struct netif *netif);

/* 2. PHY 地址探测（BSR 方式） */
static uint8_t phy_probe_address(ETH_HandleTypeDef *heth);

/* 3. PHY 初始化 + 链路状态检测 */
static uint32_t phy_init_and_detect_link(ETH_HandleTypeDef *heth, uint8_t addr);

/* 4. MAC 配置 + ETH DMA 启动 */
static void eth_start(ETH_HandleTypeDef *heth, uint32_t link_state);
```

`low_level_init()` 变为约 30 行的编排函数：
```c
static void low_level_init(struct netif *netif) {
  eth_hal_init(&heth, netif);
  uint8_t addr = phy_probe_address(&heth);
  uint32_t link = phy_init_and_detect_link(&heth, addr);
  eth_start(&heth, link);
}
```

注意：此重构在 CubeMX 重新生成时会被覆盖，需在 `/* USER CODE BEGIN/END */` 块中操作，
或在重新生成后手动恢复。

---

## 批次 4：工程清理与风格统一（P3）

### 4.1 清理未使用文件

**步骤 1** — 将 `demo&data/` 加入 `.gitignore`：
```gitignore
# 参考示例（不参与构建）
demo&data/
```

**步骤 2** — 评估是否从 Git 中移除 `demo&data/`（需确认是否仍需版本管理）。

**步骤 3** — 删除未使用的 CMSIS 目录（如果确认不需要）：
```
Drivers/CMSIS/NN/
Drivers/CMSIS/DSP/
Drivers/CMSIS/DAP/
Drivers/CMSIS/Core_A/
Drivers/CMSIS/RTOS/
Drivers/CMSIS/RTOS2/Template/
Drivers/CMSIS/Core/Template/
```

注意：这些是 CubeMX 生成时带入的，删除后重新生成 ioc 会恢复。建议仅在 `.gitignore` 中排除。

### 4.2 修复 Makefile

| 修改 | 位置 |
|---|---|
| `ASMM_SOURCES` → `ASM_SOURCES` | `Makefile:191-192` |
| 删除未使用的 `AS_INCLUDES` 定义 | `Makefile:243-272` |
| 添加 `-Wextra` | `Makefile:310` |
| 考虑 FreeRTOS port 改为 `ARM_CM7/r0p1` | `Makefile:101, 255` |

### 4.3 更新 .gitignore

在现有 `.gitignore` 中追加：
```gitignore
# 参考示例
demo&data/

# CubeMX 工程文件
*.mxproject

# Windows 快捷方式
*.lnk
```

### 4.4 链接脚本 /DISCARD/ 段处理

**当前状态**：
```
/DISCARD/ :
{
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
}
```

**风险**：代码增长后，编译器可能生成对 `libgcc` 辅助函数的调用（如 64 位除法），
丢弃后链接失败。

**建议**：删除整个 `/DISCARD/` 段。当前项目不使用浮点和复杂算术，删除后不会增加
Flash 占用（`--gc-sections` 会自动去除未引用符号）。

### 4.5 补充魔法数字注释

对暂不拆分为宏的魔法数字，至少添加行内注释：

| 位置 | 添加注释 |
|---|---|
| `quadsi.c:41` | `/* 200MHz / (5+1) = 33.3MHz QSPI clock */` |
| `quadsi.c:44` | `/* 2^(23+1) = 16MB W25Q128 */` |
| `fdcan.c:46` | `/* 200MHz / 16 / (1+1+1) = ~4.17MHz nominal bitrate */` |
| `lwipopts.h:86` | `/* FreeRTOS priority 24 = osPriorityNormal */` |
| `lwipopts.h:104` | `/* CubeMX default, LwIP internal use only */` |
| `stm32h7xx_hal_msp.c:73` | `/* Lowest priority — required by FreeRTOS for PendSV */` |

### 4.6 清理重复 include

| 文件 | 删除 |
|---|---|
| `main.c:35` | 删除重复的 `#include "FreeRTOS.h"` |
| `freertos.c:24` | 删除重复的 `#include "FreeRTOS.h"` |
| `ethernetif.c:37` | 删除重复的 `#include <string.h>` |

---

## 验证清单

每个批次完成后，执行以下验证：

- [ ] 全量编译通过（零警告）
- [ ] Flash 占用不超过 128KB（检查 `.map` 文件）
- [ ] 烧录后心跳 LED 正常闪烁
- [ ] 串口输出 PHY 地址和链路状态
- [ ] ping 192.168.1.88 通
- [ ] 看门狗正常喂狗（8 秒不喂狗则复位）

批次 2 额外验证：
- [ ] 多次 ping 大包（`ping -l 1400 192.168.1.88 -t`）无丢包

批次 3 额外验证：
- [ ] 串口无 `[STACK_OVF]` 输出
- [ ] 链路断开/重连后自动恢复

---

## 不在本次优化范围

以下问题记录但不在本次优化中处理（影响范围超出阶段 1 或属于阶段 2+ 需求）：

| 问题 | 原因 |
|---|---|
| Fault Handler 改为非阻塞 UART | 需要改写所有 Fault Handler，风险较高，单独处理 |
| MAC 地址按设备分配（OTP/UUID） | 需要硬件配合，阶段 1 仅单设备 |
| FDCAN 配置完善（缓冲区非零） | 阶段 2 任务 |
| FreeRTOS FPU 上下文保存 | 当前无浮点使用，阶段 2 前确认即可 |
| `sys_jiffies()` 实现 | PPP 未使用，当前不触发链接错误 |
| FatFs / QSPI 功能启用 | 阶段 3/4 任务 |
| Flash 空间优化（`-Os` / QSPI XIP） | 接近 128KB 上限时再处理 |
