# STM32H750 CAN 网关项目审核报告

**审核日期**：2026-05-25
**审核范围**：全部源码、构建系统、文档、工程结构
**项目阶段**：阶段 1 — 以太网验证（LAN8720 Ping）

---

## 一、总评

| 维度 | 评分 | 等级 |
|---|---|---|
| 安全性 | 55 / 100 | D — 存在隐患 |
| 代码结构 | 70 / 100 | C — 可接受 |
| 耦合度 | 60 / 100 | C — 偏高 |
| 美观度 | 72 / 100 | C+ — 尚可 |
| 易用性 | 68 / 100 | C — 文档有误导 |
| 维护难度 | 58 / 100 | D+ — 需改进 |

核心问题：文档与代码不一致（CLAUDE.md 的 D-Cache / ETH_PAD_SIZE 指导会直接导致回归），无看门狗，存在数据竞争和缓冲区溢出风险。

---

## 二、安全性（55/100）

### 2.1 高风险

| # | 问题 | 位置 | 影响 |
|---|---|---|---|
| S1 | 无看门狗 — IWDG/WWDG 均未启用，任何任务死锁导致系统永久挂起 | `hal_conf.h:64,82` | 系统可靠性 |
| S2 | Fault Handler 阻塞 UART — `fault_print()` 在 HardFault 中调用 `HAL_UART_Transmit`（500ms 阻塞轮询），若 UART 忙则 Handler 永久卡死 | `stm32h7xx_it.c:56-66` | 故障不可恢复 |
| S3 | RxAllocStatus 数据竞争 — ISR 写、任务读，未声明 `volatile`，无临界区 | `ethernetif.c:109,640,989` | 偶发丢包或状态错误 |
| S4 | `sprintf` 无边界检查 — `vAssertCalled` 中 `__FILE__` 路径可超 80 字节缓冲区 | `main.c:209-211` | 栈溢出 |

### 2.2 中风险

| # | 问题 | 位置 | 影响 |
|---|---|---|---|
| S5 | FPU 上下文未保存 — `configENABLE_FPU=0`，若任何路径使用浮点指令，任务切换将静默损坏数据 | `FreeRTOSConfig.h:58` | 潜在数据损坏 |
| S6 | MAC 地址全设备相同 — 硬编码 `00:80:E1:00:00:00`，多设备部署会产生网络冲突 | `ethernetif.c:229-234` | 网络冲突 |
| S7 | `eth_rx_err_count` 声明但从未递增 — ETH 错误回调中未统计错误 | `ethernetif.c:43,194` | 错误不可观测 |

### 2.3 低风险

| # | 问题 | 位置 |
|---|---|---|
| S8 | HAL `assert_param` 未启用（`USE_FULL_ASSERT` 被注释） | `hal_conf.h:238` |
| S9 | `configHEAP_CLEAR_MEMORY_ON_FREE=0`，释放内存不清零 | `FreeRTOSConfig.h:74` |
| S10 | `HAL_ETH_GetMACConfig/SetMACConfig/Start_IT` 返回值未检查 | `ethernetif.c:381,947-953` |

---

## 三、代码结构（70/100）

### 3.1 工程结构

遵循 CubeMX 标准目录布局，`Core/Src`、`Core/Inc`、`LWIP/App`、`LWIP/Target` 分层清晰。

### 3.2 存在的问题

| # | 问题 | 严重度 |
|---|---|---|
| O1 | `demo&data/` 参考目录被 Git 跟踪，应移除或加入 `.gitignore` | 中 |
| O2 | CMSIS 中有大量未使用目录：`NN/`、`DSP/`、`DAP/`、`Core_A/`、`RTOS/` | 低 |
| O3 | `Drivers/CMSIS/Include/` 与 `Drivers/CMSIS/Core/Include/` 头文件完全重复 | 低 |
| O4 | 链接脚本 `/DISCARD/` 丢弃 `libc.a`、`libm.a`、`libgcc.a` 全部符号，代码增长后可能导致链接失败 | **高** |
| O5 | `sys_jiffies()` 在 `ethernetif.h` 中声明但无实现，PPP 模块编译后可能链接失败 | 中 |

### 3.3 Makefile 问题

| 问题 | 位置 |
|---|---|
| `ASMM_SOURCES` 拼写错误（应为 `ASM`） | `Makefile:191-192` |
| `AS_INCLUDES` / `ASFLAGS` 定义但从未在构建规则中使用 | `Makefile:243-272, 308` |
| FreeRTOS 用 `ARM_CM4F` port 而非 `ARM_CM7`（兼容但语义不对） | `Makefile:101, 255` |
| 仅 `-Wall`，缺少 `-Wextra` / `-Werror` | `Makefile:310` |

---

## 四、耦合度（60/100）

### 4.1 耦合热点：ethernetif.c

该文件同时依赖 5 个层，是全项目耦合最严重的文件：

```
ethernetif.c
├── FreeRTOS (cmsis_os.h) — 信号量/线程/延时
├── HAL ETH — 全部 ETH 外设 API
├── BSP lan8742 — PHY 驱动
├── LwIP — 内存池/pbuf/tcpip
└── Debug UART — extern huart2 + sprintf 调试打印
```

调试打印直接嵌入 PHY 驱动代码（`ethernetif.c:327-331, 873-885, 959-961`），生产代码与调试代码未分离。

### 4.2 重复代码

| 位置 | 内容 |
|---|---|
| `ethernetif.c:351-373` vs `ethernetif.c:918-942` | PHY 链路速率 switch-case 完全重复，应提取为函数 |
| `main.c:200`, `stm32h7xx_it.c:54`, `ethernetif.c:38` | `extern huart2` 重复声明 3 处，应 `#include "usart.h"` |
| `stm32h7xx_it.c:70`, `lwip.h:48` | `extern heth` 重复声明 |
| `main.h:53`, `lwip.c:37`, `ethernetif.h:38` | `Error_Handler` 重复声明 |

### 4.3 配置分散

配置值散布在 5+ 个文件中，无统一来源：

| 配置项 | 位置 | 问题 |
|---|---|---|
| IP 地址 192.168.1.88 | `lwip.c:66-77` | 函数体内硬编码，非 `#define` |
| MAC 地址 | `ethernetif.c:229-234` | 与 `hal_conf.h:226-231` 定义的 MAC 冲突 |
| UART 波特率 115200 | `usart.c:42` | 嵌入结构体，无宏定义 |
| FDCAN 分频系数 16 | `fdcan.c:46` | 无注释，不知目标波特率 |
| QSPI 分频系数 5 | `quadsi.c:41` | 无注释 |
| `HAL_UART_Transmit` 超时 | 多处 | 100ms / 200ms / 500ms 混用，无统一定义 |

---

## 五、美观度（72/100）

### 5.1 缩进和大括号风格

- 缩进：大部分文件用 2 空格，一致
- 大括号：K&R / Allman 混用 — `freertos.c` 用 K&R（同行开括号），`main.c` 的 `SystemClock_Config` 用 Allman（换行开括号）
- 变量命名：手动代码统一用 `snake_case`，但有 `char m[80]`（`ethernetif.c:959`）等单字母变量

### 5.2 魔法数字（共 11 处）

| 位置 | 值 | 应改为 |
|---|---|---|
| `quadsi.c:41` | `ClockPrescaler = 5` | `#define QSPI_PRESCALER 5` + 注释 33MHz |
| `quadsi.c:44` | `FlashSize = 23` | `#define W25Q128_FLASH_SIZE 23` + 注释 16MB |
| `fdcan.c:46` | `NominalPrescaler = 16` | 需注释目标波特率 |
| `ethernetif.c:239` | `RxBuffLen = 1536` | 应使用 `ETH_RX_BUFFER_SIZE` 宏 |
| `lwipopts.h:104` | `RECV_BUFSIZE_DEFAULT = 2000000000` | 2GB 接收缓冲区，CubeMX 默认值 |
| `lwipopts.h:86` | `TCPIP_THREAD_PRIO = 24` | 需注释与 FreeRTOS 优先级的映射 |
| `ethernetif.c:105` | `ETH_RX_BUFFER_CNT = 12` | 无选择理由注释 |
| `ethernetif.c:859` | `STABLE_THRESHOLD = 5` | 500ms 去抖时间无依据 |
| `main.c:204` | UART timeout = 200 | 无统一定义 |
| `stm32h7xx_it.c:65` | UART timeout = 500 | 同上 |
| `freertos.c:55` | `stack_size = 512 * 4` | `* 4` 含义不直观 |

### 5.3 注释质量

优秀（值得保留）：
- `main.c:81-88` — UNALIGN_TRP 清除的原因
- `main.c:236-243` — MPU TEX/C/B 编码与 CubeMX 默认值的差异
- `ethernetif.c:69-89` — 零拷贝缓冲区架构文档
- `lwipopts.h:124-138` — SMEMCPY 覆写原因

有害（会误导）：
- `ethernetif.c:763` — "PHI IO Functions" 应为 "PHY IO Functions"
- CubeMX 空 `USER CODE` 块占 `freertos.c` 篇幅的 40%

---

## 六、易用性（68/100）

### 6.1 文档与代码不一致（最严重问题）

| 文档声明 | 实际代码 | 影响 |
|---|---|---|
| CLAUDE.md: "Tx: `SCB_CleanDCache_by_Addr`" | `low_level_output()` 中无 Cache Clean | 按文档操作会引入回归 |
| CLAUDE.md: "Rx: 已有 `SCB_InvalidateDCache_by_Addr`" | `HAL_ETH_RxLinkCallback()` 中无 Cache Invalidate | 同上 |
| CLAUDE.md: "`ETH_PAD_SIZE=2`" | `lwipopts.h` 注释说明必须不设此值 | 同上 |
| README.md: "D-Cache: Tx Clean / Rx Invalidate" | 代码已移除 Cache 操作 | 误导新开发者 |
| README.md: "ETH_PAD_SIZE=2" | 代码中未定义 | 同上 |
| CONFIG_SUMMARY.md: `StartDefaultTask` 有心跳循环 | 心跳在 `heartbeatTask` 中 | 误导 |

### 6.2 新手上手体验

优点：
- README.md 提供了快速上手流程
- BUILD_AND_TEST.md 工具链安装指南详细
- DEBUG_LOG.md 记录了 19 个问题的排查过程，极具参考价值

不足：
- README.md 的"关键设计决策"章节与代码矛盾
- 编译命令需在 Git Bash 中手动指定路径，无一键脚本
- 缺少烧录工具的具体配置步骤

---

## 七、维护难度（58/100）

### 7.1 长函数

| 函数 | 行数 | 文件 | 问题 |
|---|---|---|---|
| `low_level_init()` | 183 行 | `ethernetif.c:216-399` | 混合了 HAL 初始化、内存池、PHY 探测、链路检测、MAC 配置、ETH 启动 |
| `ethernet_link_thread()` | 127 行 | `ethernetif.c:844-971` | 去抖逻辑 + 调试打印 + 链路状态机 + MAC 重配 |

### 7.2 FreeRTOS 任务设计风险

| 问题 | 详情 |
|---|---|
| EthIf 优先级过高 | `osPriorityRealtime(40)` — 若信号量卡死，饿死所有任务。建议降为 `osPriorityAboveNormal` |
| 无栈溢出检测 | `configCHECK_FOR_STACK_OVERFLOW=0`，EthIf 仅 2048B 栈 + LwIP 深调用链 |
| 无 Idle Hook | `configUSE_IDLE_HOOK=0`，无法在空闲时做低功耗或看门狗喂狗 |

### 7.3 Flash 空间

当前 76KB / 128KB（`-Og`），剩余 52KB。随着 FDCAN + FatFs + Web 界面加入，预计会超出。

### 7.4 .gitignore 缺失

- `demo&data/` 未忽略
- `.mxproject` 未忽略
- `*.lnk`（Windows 快捷方式）未忽略

---

## 八、优先级排序

| 优先级 | 问题 | 类别 | 工作量 |
|---|---|---|---|
| P0 | 更新 CLAUDE.md / README.md 与代码一致 | 文档 | 30 分钟 |
| P0 | 启用 IWDG 看门狗 | 安全 | 1 小时 |
| P1 | `sprintf` → `snprintf` 全局替换 | 安全 | 30 分钟 |
| P1 | `RxAllocStatus` 改 `volatile` + 临界区 | 安全 | 30 分钟 |
| P1 | 创建 `app_config.h` 集中配置 | 维护 | 1 小时 |
| P2 | 拆分 `low_level_init()` | 维护 | 2 小时 |
| P2 | 降低 EthIf 优先级 + 启用栈溢出检测 | 安全 | 30 分钟 |
| P2 | 消除重复代码 | 结构 | 1 小时 |
| P3 | 清理 `demo&data/` 和未使用 CMSIS 目录 | 结构 | 30 分钟 |
| P3 | 链接脚本 `/DISCARD/` 段风险评估 | 结构 | 1 小时 |
| P3 | 统一代码风格和魔法数字 | 美观 | 2 小时 |

---

**结论**：项目在阶段 1 以太网验证的功能目标上已达成，DMA/MPU/Cache 配置正确。主要风险集中在文档误导、无安全网（看门狗/栈检测）、和配置管理混乱三个方面。建议在进入阶段 2（FDCAN）之前先处理 P0/P1 项。
