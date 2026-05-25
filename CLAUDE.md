# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> 最后更新：2026-05-25 | 项目阶段：阶段 1

---

## 项目定位

STM32H750VBT6 CAN/CAN-FD 数据采集解析网关，当前处于**阶段 1（以太网验证）**。详细需求见 `PROJECT_REQUIREMENTS.md`，调试历史见 `DEBUG_LOG.md`，工程概览见 `README.md`。

---

## 编译命令

工具链需手动指定路径（make 和 arm-none-eabi-gcc 装在非标准位置），须在 **Git Bash** 中运行：

```bash
MAKE="/c/Users/ben.luo/AppData/Local/Microsoft/WinGet/Packages/ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/make.exe"
ARM_PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 Rel1/bin"
export PATH="$ARM_PATH:$PATH"
cd "/d/Ben/stm32_canbus_claude/CANbus_code"

"$MAKE"                        # 增量编译
rm -rf build/ && "$MAKE"       # 全量编译（make clean 在 Windows 下有问题）
rm -f build/foo.o && "$MAKE"   # 强制重编单个文件
```

PowerShell 的 make 找不到 sh.exe，会失败。编译输出在 `CANbus_code/build/`。

**当前 Flash 占用**：约 76KB / 128KB（`-Og` 调试优化）。

---

## 关键架构约束

### 1. STM32H7 D-Cache 与 MPU 配置

当前采用 **Non-Cacheable D2 SRAM** 方案，ETH DMA 描述符和 Rx 缓冲池均在 Non-Cacheable 区域，
**不需要手动 Clean/Invalidate D-Cache**（之前版本曾有，已移除）。

| 区域 | 基地址 | 大小 | MPU 属性 | 用途 |
|---|---|---|---|---|
| D2 SRAM | 0x30000000 | 256KB | Non-Cacheable, Bufferable | DMA 描述符 + Rx Pool + LwIP Heap |
| AXI SRAM | 0x24000000 | 512KB | Cacheable | 代码/数据/pbuf payload |

Tx 路径：`HAL_ETH_Transmit_IT` 零拷贝，pbuf payload 在 Cacheable 区域，DMA 直接读取。
Rx 路径：Rx Pool 在 Non-Cacheable 区域，接收无需 Cache 维护。

如未来将 Rx Pool 移至 Cacheable 区域，需重新加入 `SCB_InvalidateDCache_by_Addr`。

### 2. D2 SRAM 内存布局（不能随意改动地址）

```
0x30000000 ~ 0x3000005F  DMARxDscrTab   (4×24B Rx 描述符)
0x30000080 ~ 0x300000DF  DMATxDscrTab   (4×24B Tx 描述符)
0x30000100 ~ 0x30004A83  Rx_PoolSection (12×RxBuff_t ≈18.3KB)
0x30005000 ~             LWIP_RAM_HEAP  (MEM_SIZE=16KB)
```

`LWIP_RAM_HEAP_POINTER` 必须在 Rx_PoolSection 结束地址之后。修改 `ETH_RX_BUFFER_CNT` 或 `ETH_RX_BUFFER_SIZE` 后，必须重新检查 `.map` 文件确认 pool 实际结束地址，再调整堆起点。

### 3. LwIP 必须配置的参数（lwipopts.h）

| 参数 | 值 | 原因 |
|---|---|---|
| `ETH_PAD_SIZE` | `0`（不设置） | 已通过覆写 `SMEMCPY` 为逐字节拷贝解决非对齐访问问题（见 `lwipopts.h:124-138`）。设置 `ETH_PAD_SIZE=2` 反而导致 LwIP pbuf 对齐计算错误 |
| `MEM_SIZE` | `16*1024` | LwIP 默认 1600B 不够，pbuf/TCP 缓冲区分配失败触发 assert |
| `LWIP_RAM_HEAP_POINTER` | `0x30005000` | 必须在 Rx_PoolSection 之后（见上方布局） |

### 4. LAN8720 PHY 驱动适配

驱动文件是 `lan8742.c`（CubeMX 选型错误），LAN8720 无 SMR 寄存器（reg 0x12），`LAN8742_Init()` 的 SMR 扫描会得到随机地址。

**已实现的解决方案**（`ethernetif.c` `PHY_PRE_CONFIG` 段）：用 BSR（reg 0x01）探测地址 0 和 1，找到有效值后直接设置 `LAN8742.DevAddr` 并置 `Is_Initialized=1`，跳过 SMR 扫描。

`ETH_PHY_IO_Init()` 中有 `HAL_Delay(1000)`，LAN8720 模块无 RESET 引脚，上电后需等待 1000ms MDIO 才稳定。**不能删除或缩短**。

### 5. FreeRTOS 配置要求

- `configTOTAL_HEAP_SIZE = 32768`（32KB）：LwIP + ETH 需要 4 个任务（defaultTask/tcpip_thread/EthIf/EthLink）+ 队列/信号量，15KB 不够。
- 心跳灯（PE7）在独立的 `heartbeatTask`（osPriorityLow）中，与 LwIP 初始化解耦，是系统活体的唯一可靠指示。

### 6. 外设初始化顺序（`main.c`）

```c
MX_GPIO_Init();
// MX_FDCAN1_Init();    ← 注释，待阶段 2 恢复
// MX_QUADSPI_Init();   ← 注释，待阶段 3 恢复
// MX_SDMMC1_SD_Init(); ← 注释，待阶段 4 恢复
MX_USART2_UART_Init();
// MX_FATFS_Init();     ← 注释，待阶段 4 恢复
// LwIP 在 FreeRTOS 调度启动后由 defaultTask 调用 MX_LWIP_Init()
```

恢复外设时逐个打开，每次验证通过后再开下一个。

---

## 调试诊断代码（当前保留）

以下调试代码在正常运行时静默，崩溃时才输出，**不影响正常功能，保留即可**：

- `main.c`：`vAssertCalled(file, line)` — FreeRTOS assert 触发时打印 `[ASSERT] 文件:行号`
- `main.c`：`Error_Handler()` — 打印 `[ERROR_HANDLER] LR=0x...`
- `stm32h7xx_it.c`：HardFault/MemManage/BusFault/UsageFault — 从异常栈帧读 PC/LR，打印 `[HARDFAULT] PC=0x... CFSR=0x...`

崩溃后用 `arm-none-eabi-addr2line -e build/CANbus_code.elf -f -C <PC地址>` 定位源码行。

---

## CubeMX 重新生成会覆盖的手动修改

| 文件 | 修改内容 |
|---|---|
| `STM32H750XX_FLASH.ld` | 末尾加了 `.lwip_sec` 段（ETH DMA 描述符强制映射到 D2 SRAM） |
| `LWIP/Target/ethernetif.c` | PHY BSR 探测、1000ms 延时、SMEMCPY 覆写、EthIf 栈 512 words、串口打印 |
| `LWIP/Target/lwipopts.h` | `SMEMCPY` 覆写为逐字节拷贝、`MEM_SIZE=16KB`、`LWIP_RAM_HEAP_POINTER=0x30005000` |
| `Core/Src/freertos.c` | defaultTask（LwIP init 后退出）+ heartbeatTask（PE7 心跳） |
| `Core/Src/main.c` | 注释了 FDCAN/QSPI/SDMMC/FATFS 初始化；加了 vAssertCalled/Error_Handler 打印 |
| `Core/Src/stm32h7xx_it.c` | Fault handler 改为打印 PC/LR/CFSR |
| `Core/Inc/FreeRTOSConfig.h` | `configTOTAL_HEAP_SIZE=32768`；`configASSERT` 改为调用 `vAssertCalled` |
| `Middlewares/Third_Party/FatFs/src/option/syscall.c` | 加了 `FreeRTOS.h` / `task.h` include |

建议：修改 ioc 后先 `git stash`，生成后 `git diff` 对比，只接受 ioc 本身的变化。

---

## 引脚分配（详见 `PROJECT_REQUIREMENTS.md`）

```
ETH RMII    : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF卡 : PC8/PC9/PC10/PC11/PC12/PD2
FDCAN1      : PD0(RX) / PD1(TX)
继电器      : PE7(Relay1) / PE8(Relay2)  — 高电平触发，上电默认低
USART2      : PD5(TX) / PD6(RX)  — 115200 8N1
SWD         : PA13 / PA14
```

---

## 验证状态（阶段 1）

- [x] 编译通过（76KB，零警告）
- [x] LAN8720 PHY 地址探测（addr=1，BSR）
- [x] MPU/Cache 配置修正（D2 SRAM Normal Non-Cacheable）
- [x] SMEMCPY 逐字节拷贝（非对齐安全）
- [x] FreeRTOS 堆 32KB + MEM_SIZE 16KB
- [x] heartbeatTask 独立心跳
- [ ] ping 192.168.1.88 通（待上板验证）
- [ ] 后续阶段：FDCAN / QSPI / SDMMC

---

## Flash 容量限制

128KB 内部 Flash 接近上限。超出时：
1. 先改 `OPT = -Os`（Makefile）
2. 长期方案：W25Q128 XIP（bootloader 放内部 Flash，主程序 Memory Mapped 到 QSPI）
