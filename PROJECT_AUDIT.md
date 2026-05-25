# STM32H750 CAN 网关 — 项目审核与优化方案

> 最后更新：2026-05-25 | 项目阶段：阶段 1 | 合并自 `REVIEW_REPORT.md` + `OPTIMIZATION_PLAN.md`

---

## 一、审核总评

| 维度 | 评分 | 等级 | 说明 |
|---|---|---|---|
| 安全性 | 55/100 | D | 无看门狗、sprintf 溢出风险、数据竞争 |
| 代码结构 | 70/100 | C | CubeMX 标准布局，但耦合度高 |
| 文档一致性 | 60/100 | C | CLAUDE.md 已修正，README 待验证 |
| 维护难度 | 58/100 | D+ | 长函数、配置分散、无栈检测 |

核心结论：阶段 1 以太网验证功能代码已到位，DMA/MPU/Cache 配置正确。主要风险在文档误导（已修复）、安全网缺失、配置管理混乱。

---

## 二、安全问题（P0~P1）

| # | 优先级 | 问题 | 文件 | 风险 |
|---|---|---|---|---|
| S1 | P0 | 无看门狗，任务死锁系统永久挂起 | `hal_conf.h:64` | 系统可靠性 |
| S2 | P1 | `sprintf` 无边界检查，`__FILE__` 路径可溢出 80B 缓冲区 | `main.c:209`、`ethernetif.c` | 栈溢出 |
| S3 | P1 | `RxAllocStatus` ISR 写/任务读，无 `volatile`，无临界区 | `ethernetif.c:109,640,989` | 数据竞争 |
| S4 | P1 | MAC 地址硬编码，多设备冲突 | `ethernetif.c:229-234` | 网络冲突 |
| S5 | P2 | EthIf 任务优先级 `osPriorityRealtime`，可饿死所有任务 | `ethernetif.c:287` | 死锁 |
| S6 | P2 | 栈溢出检测未启用 (`configCHECK_FOR_STACK_OVERFLOW=0`) | `FreeRTOSConfig.h` | 栈溢出静默 |
| S7 | P2 | FPU 上下文未保存 (`configENABLE_FPU=0`) | `FreeRTOSConfig.h:58` | 浮点数据损坏 |
| S8 | P3 | 链接脚本 `/DISCARD/` 丢弃 `libgcc.a`，代码增长后链接失败 | `STM32H750XX_FLASH.ld` | 链接失败 |

---

## 三、结构与维护问题（P1~P3）

| # | 优先级 | 问题 | 文件 |
|---|---|---|---|
| O1 | P1 | 配置散布在 5+ 文件，IP/MAC/波特率硬编码 | `lwip.c`、`ethernetif.c`、`usart.c` |
| O2 | P2 | `low_level_init()` 183 行，混合 HAL/PHY/MAC 初始化 | `ethernetif.c:216-399` |
| O3 | P2 | PHY 速率配置 switch-case 在 2 处完全重复 | `ethernetif.c:351-373,918-942` |
| O4 | P2 | `extern huart2` / `extern heth` / `Error_Handler` 重复声明 | 多个文件 |
| O5 | P3 | `demo&data/` 目录被 Git 跟踪，应加入 `.gitignore` | 根目录 |
| O6 | P3 | Makefile `ASMM_SOURCES` 拼写错误、`AS_INCLUDES` 未使用 | `Makefile:191,243` |
| O7 | P3 | 魔法数字 11 处（QSPI prescaler、FDCAN 分频、去抖阈值等） | `quadsi.c`、`fdcan.c`、`ethernetif.c` |
| O8 | P3 | 重复 include：`FreeRTOS.h` 在 `main.c` 和 `freertos.c` 各重复一次 | `main.c:35`、`freertos.c:24` |

---

## 四、优化批次与执行状态

### 批次 1：安全与文档（P0）— 2 小时

| # | 任务 | 状态 | 涉及文件 |
|---|---|---|---|
| 1.1 | CLAUDE.md D-Cache/ETH_PAD_SIZE 修正 | ✅ 已完成 | `CLAUDE.md` |
| 1.2 | README.md 关键设计决策修正 | ✅ 已完成 | `README.md` |
| 1.3 | 启用 IWDG 看门狗（8s 超时，heartbeatTask 喂狗） | ⬜ 待执行 | `hal_conf.h`、`main.c`、`freertos.c` |
| 1.4 | `sprintf` → `snprintf` 全局替换 | ⬜ 待执行 | `main.c`、`stm32h7xx_it.c`、`ethernetif.c` |

### 批次 2：数据竞争与配置管理（P1）— 2 小时

| # | 任务 | 状态 | 涉及文件 |
|---|---|---|---|
| 2.1 | `RxAllocStatus` 改 `volatile` + 临界区 | ⬜ 待执行 | `ethernetif.c` |
| 2.2 | 创建 `Core/Inc/app_config.h` 集中配置 | ⬜ 待执行 | 新建 + 6 个文件 |
| 2.3 | 统一 MAC 地址（消除 `hal_conf.h` 中的冲突定义） | ⬜ 待执行 | `ethernetif.c`、`hal_conf.h` |

### 批次 3：代码去重与任务优化（P2）— 3 小时

| # | 任务 | 状态 | 涉及文件 |
|---|---|---|---|
| 3.1 | 提取 `phy_apply_link_config()` 消除重复代码 | ⬜ 待执行 | `ethernetif.c` |
| 3.2 | 消除重复 `extern` 声明，改用 `#include "usart.h"` | ⬜ 待执行 | `main.c`、`stm32h7xx_it.c`、`ethernetif.c` |
| 3.3 | 降低 EthIf 优先级 (`Realtime` → `AboveNormal`) + 启用栈溢出检测 | ⬜ 待执行 | `ethernetif.c`、`FreeRTOSConfig.h`、`freertos.c` |
| 3.4 | 拆分 `low_level_init()` 为 4 个子函数 | ⬜ 待执行 | `ethernetif.c` |

### 批次 4：工程清理与风格统一（P3）— 3 小时

| # | 任务 | 状态 | 涉及文件 |
|---|---|---|---|
| 4.1 | `demo&data/` 加入 `.gitignore` | ⬜ 待执行 | `.gitignore` |
| 4.2 | Makefile 修正（`ASM_SOURCES`、`-Wextra`、`ARM_CM7/r0p1`） | ⬜ 待执行 | `Makefile` |
| 4.3 | 链接脚本删除 `/DISCARD/` 段 | ⬜ 待执行 | `STM32H750XX_FLASH.ld` |
| 4.4 | 补充魔法数字注释 | ⬜ 待执行 | `quadsi.c`、`fdcan.c`、`lwipopts.h` |
| 4.5 | 清理重复 include | ⬜ 待执行 | `main.c`、`freertos.c`、`ethernetif.c` |

---

## 五、不在本次优化范围

| 问题 | 原因 |
|---|---|
| Fault Handler 改为非阻塞 UART | 风险较高，需单独处理 |
| MAC 地址按设备分配（OTP/UUID） | 需硬件配合，阶段 1 仅单设备 |
| FDCAN 配置完善 | 阶段 2 任务 |
| FreeRTOS FPU 上下文保存 | 当前无浮点使用，阶段 2 前确认即可 |
| `sys_jiffies()` 实现 | PPP 未使用，不触发链接错误 |
| FatFs / QSPI 功能启用 | 阶段 3/4 任务 |
| Flash 空间优化（`-Os` / QSPI XIP） | 接近 128KB 上限时再处理 |

---

## 六、验证清单（每批次完成后执行）

- [ ] 全量编译通过（零警告）
- [ ] Flash 占用不超过 128KB
- [ ] 烧录后心跳 LED 正常闪烁（1Hz）
- [ ] 串口输出 PHY 地址和链路状态
- [ ] `ping 192.168.1.88` 通
- [ ] 看门狗正常喂狗（8s 不喂则复位）

批次 2 额外：
- [ ] `ping -l 1400 192.168.1.88 -t` 大包无丢包

批次 3 额外：
- [ ] 串口无 `[STACK_OVF]` 输出
- [ ] 链路断开/重连后自动恢复