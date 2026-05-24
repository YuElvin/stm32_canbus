# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## 项目定位

STM32H750VBT6 CAN/CAN-FD 数据采集解析网关。当前处于**阶段 1（以太网验证）**，部分外设初始化已注释掉。最终目标：CAN 收发 + DBC 解析 + 规则控制继电器 + 以太网 Web 配置界面。详细需求见 `CLAUDE.md`（用户版），调试历史见 `DEBUG_LOG.md`。

---

## 编译命令

工具链需手动指定路径（make 和 arm-none-eabi-gcc 装在非标准位置）：

```bash
MAKE="/c/Users/ben.luo/AppData/Local/Microsoft/WinGet/Packages/ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/make.exe"
ARM_PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 Rel1/bin"
export PATH="$ARM_PATH:$PATH"
cd "/d/Ben/stm32_canbus_claude/CANbus_code"

"$MAKE"              # 增量编译
"$MAKE" clean        # 清除 build/（Windows 下有问题，改用 rm -rf build/）
rm -rf build/ && "$MAKE"  # 全量编译
```

须在 **Git Bash** 中运行（PowerShell 的 make 找不到 shell，会失败）。

编译输出在 `CANbus_code/build/`：`.elf`（调试）、`.hex`（烧录）、`.bin`（烧录）。

**当前 Flash 占用**：约 75KB / 128KB（调试优化 `-Og`）。

---

## 工程结构

```
CANbus_code/
├── CANbus_code.ioc          # CubeMX 配置文件（不要手动修改引脚相关代码，改这里再重新生成）
├── STM32H750XX_FLASH.ld     # 链接脚本（手动添加了 .lwip_sec 段）
├── Makefile                 # CubeMX 生成，工具链前缀 arm-none-eabi-
├── Core/Src/
│   ├── main.c               # 外设初始化顺序（部分已注释）+ MPU 配置
│   ├── freertos.c           # FreeRTOS 任务定义（当前只有 defaultTask）
│   ├── fdcan.c              # FDCAN1 HAL 配置（当前未 init）
│   ├── gpio.c               # PE7/PE8 继电器 GPIO
│   └── usart.c              # USART2 115200 8N1
├── LWIP/
│   ├── App/lwip.c           # IP 配置（192.168.1.88）、netif 注册、链路线程
│   └── Target/
│       ├── ethernetif.c     # ETH DMA 接口（含大量手动修改，见下）
│       └── lwipopts.h       # LwIP 参数（LWIP_RAM_HEAP_POINTER、校验硬件卸载）
├── FATFS/                   # FatFs（当前未 init）
└── Drivers/BSP/Components/lan8742/  # PHY 驱动（用于 LAN8720，寄存器兼容）
```

---

## 关键架构约束

### STM32H7 D-Cache 规则（违反会导致以太网静默失败）

| 操作 | 位置 | 要求 |
|---|---|---|
| 发送（Tx） | `low_level_output()` pbuf 遍历循环内 | **必须** `SCB_CleanDCache_by_Addr(payload, (len+31)&~31)` |
| 接收（Rx） | `HAL_ETH_RxLinkCallback()` | 已有 `SCB_InvalidateDCache_by_Addr`，不要删 |
| DMA 描述符 | `.lwip_sec` 段 | 强制放在 0x30000000（D2 SRAM，MPU Non-Cacheable） |
| LwIP 堆 | `lwipopts.h` | `LWIP_RAM_HEAP_POINTER 0x30004000`（D2 SRAM 内） |

MPU Region 0（0x30000000, 256KB）= Non-Cacheable Bufferable（ETH DMA 用）。
MPU Region 1（0x24000000, 512KB）= Cacheable（代码/数据，pbuf payload 在这里）。

### LAN8720 PHY 驱动适配

驱动文件是 `lan8742.c`（CubeMX 选型错误），但 LAN8720 寄存器与 LAN8742 基本兼容，**唯一的例外是 SMR 寄存器（reg 0x12）**：

- **LAN8742** 有 SMR，`LAN8742_Init()` 通过读 SMR bit[4:0] 自动扫描 PHY 地址。
- **LAN8720** 无 SMR，扫描会得到随机地址。

**解决方案**（已在 `ethernetif.c` `PHY_PRE_CONFIG` 段实现）：上电后用 BSR（reg 0x01，所有 PHY 都有的标准寄存器）探测地址 0 和 1，找到有效值后直接设置 `LAN8742.DevAddr` 并置 `Is_Initialized=1`，跳过 `LAN8742_Init()` 内的 SMR 扫描。

### LAN8720 上电延时

`ETH_PHY_IO_Init()` 中有 `HAL_Delay(300)`。LAN8720 模块无 RESET 引脚引出，上电后需等待约 300ms MDIO 才稳定。**不能删除**，否则冷启动 PHY 探测失败。

### 外设初始化顺序（`main.c`）

当前只启用以太网验证所需的外设：
```c
MX_GPIO_Init();
// MX_FDCAN1_Init();   ← 注释，待阶段 2 恢复
// MX_QUADSPI_Init();  ← 注释，待阶段 3 恢复
// MX_SDMMC1_SD_Init();← 注释，待阶段 4 恢复
MX_USART2_UART_Init();
// MX_FATFS_Init();    ← 注释，待阶段 4 恢复
// 以太网在 FreeRTOS 调度启动后由 defaultTask 调用 MX_LWIP_Init()
```

恢复外设时按顺序逐个打开，每次验证通过后再开下一个。

### FreeRTOS 任务（当前）

只有一个任务 `defaultTask`（`freertos.c`）：初始化 LwIP → PE7 心跳灯 500ms toggle。以太网接收由 `low_level_init()` 在 netif 初始化时创建的 `EthIf` 任务处理（栈 512 words），链路监控由 `lwip.c` 创建的 `EthLink` 任务处理（栈 1024 words）。

---

## 已知问题和规避方法

### CubeMX 重新生成会覆盖手动修改

以下文件有手动修改，CubeMX 重新生成时**会被覆盖**，需要在生成后手动还原：

| 文件 | 修改内容 |
|---|---|
| `STM32H750XX_FLASH.ld` | 末尾加了 `.lwip_sec` 段定义 |
| `LWIP/Target/ethernetif.c` | PHY 探测逻辑、300ms 延时、Tx D-Cache Clean、EthIf 栈大小、串口打印 |
| `Core/Src/freertos.c` | defaultTask 改为心跳灯 |
| `Core/Src/main.c` | 注释了 FDCAN/QSPI/SDMMC/FATFS 初始化 |
| `Middlewares/Third_Party/FatFs/src/option/syscall.c` | 加了 FreeRTOS.h / task.h include |

建议：修改 ioc 后先 `git stash`，生成后 `git diff` 对比，只接受 ioc 本身的变化。

### make 在 PowerShell 中失败

make 依赖 sh.exe 来执行 `mkdir` 等命令，PowerShell 没有。必须用 Git Bash。

### Flash 容量限制（128KB）

HAL + FreeRTOS + LwIP + FatFs 全部开启后接近上限。后续如果超出：
- 先改优化级别为 `-Os`（Makefile 中 `OPT = -Os`）
- 长期方案：W25Q128 XIP（bootloader 放内部 Flash，主程序 Memory Mapped 到 QSPI）

---

## 引脚分配摘要

```
ETH RMII : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF卡: PC8/PC9/PC10/PC11/PC12/PD2
FDCAN1  : PD0(RX) / PD1(TX)
继电器  : PE7(Relay1) / PE8(Relay2)  — 高电平触发
USART2  : PD5(TX) / PD6(RX)  — 115200 8N1
SWD     : PA13 / PA14
```

PE7/PE8 上电默认低电平（继电器释放）。

---

## 当前验证状态

- [x] 编译通过（75KB，零警告）
- [x] LAN8720 PHY 地址探测（addr=1，BSR 探测）
- [x] Tx D-Cache Clean 已加入
- [ ] ping 192.168.1.88 通（待验证）
- [ ] FDCAN1 收发
- [ ] QSPI W25Q128 读 JEDEC ID
- [ ] SDMMC + FatFs 挂载 TF 卡
