# AGENTS_CN.md

AI 代理在本 STM32H750 CAN 网关仓库中工作的紧凑指引（中文版）。
> 与 `AGENTS.md`（英文版）保持同步，修改任一文件时需同步更新另一份。

## 编译

```powershell
# PowerShell — 必须手动指定工具链路径
$env:PATH = "D:\arm-gnu-toolchain-14.2\bin;$env:PATH"
make -j8
```

- 工作目录：`CANbus_code/`
- 全量编译：`rm -r -Force build/; make`（Windows 下 `make clean` 不可靠）
- 输出：`CANbus_code/build/`（`.bin` `.hex` `.elf`）
- Flash 占用：约 99KB / 128KB（`-Og`），接近上限——见下方 Flash 章节

## Flash 128KB 限制 — 关键

FatFs 的 `cc936.c`（中文代码页）体积 170KB，**会溢出 Flash**。当前方案：`_CODE_PAGE=437` + `ccsbcs.c`。如需中文文件名，必须先切到 `-Os` 或 QSPI XIP 方案。

## CubeMX 重新生成风险

`CANbus_code.ioc` 生成大部分文件，但 **11 个文件有手动修改会被覆盖**。完整列表见 `CLAUDE.md` § "CubeMX 重新生成会覆盖的手动修改"。操作流程：

```bash
git stash          # 保存当前工作
# 在 CubeMX 中重新生成
git diff           # 检查变化
git checkout -- <被覆盖的文件>   # 恢复手动修改
git stash pop      # 重新应用
```

## D2 SRAM 内存布局 — 禁止修改

ETH DMA 描述符和 LwIP 堆通过链接脚本中的 `.lwip_sec` 段固定在特定地址：

```
0x30000000  DMARxDscrTab   (4×24B)
0x30000080  DMATxDscrTab   (4×24B)
0x30000100  Rx_PoolSection (12×1536B ≈18KB)
0x30005000  LWIP_RAM_HEAP  (16KB)
```

修改 `ETH_RX_BUFFER_CNT` 或 `ETH_RX_BUFFER_SIZE` 后，必须重新检查 `.map` 文件并调整 `LWIP_RAM_HEAP_POINTER`。

## 外设初始化顺序（main.c）

```
MX_GPIO_Init();
// MX_FDCAN1_Init();    ← 已禁用，待阶段 5 恢复
MX_QUADSPI_Init();      // W25Q128
// MX_SDMMC1_SD_Init(); ← 禁止调用：FatFs 的 BSP_SD_Init 内部处理
MX_USART2_UART_Init();
MX_FATFS_Init();        // 内部触发 BSP_SD_Init
```

**SDMMC 陷阱**：同时调用 `MX_SDMMC1_SD_Init()` + `f_mount()` 会导致 `HAL_SD_Init()` 被调用两次 → FR_NOT_READY。SD 初始化应完全交给 FatFs 管理。

## QSPI W25Q128 — 避免使用 AutoPolling

STM32H7 上 `HAL_QSPI_AutoPolling()` 在 `HAL_QSPI_Command()` 之后调用会出现状态机冲突。应改用手动轮询 `ReadStatusReg1()` 的方式。详见 `w25qxx.c:WaitBusy()`。

## LAN8720 PHY 驱动

CubeMX 选了 `lan8742.c` 但硬件是 LAN8720。关键差异：
- 无 SMR 寄存器（0x12）—— 基于 SMR 的地址扫描会返回垃圾值
- 解决方案：在 `ethernetif.c` 中用 BSR 探测地址 0-31，重试 5 次
- `ETH_PHY_IO_Init()` 中有 `HAL_Delay(2000)` —— **不能删除**，LAN8720 模块上电后需要 2 秒 MDIO 才稳定

## FreeRTOS 栈大小

所有网络任务栈必须 ≥ 2048 words（8KB）。深调用链 `ip4_input → etharp_query → etharp_request` 在 1024 words 时会栈溢出。任务列表：

| 任务 | 栈大小 | 优先级 |
|---|---|---|
| defaultTask | 2048 | Normal（LwIP 初始化后退出） |
| tcpip_thread | 2048 | —（LwIP 内部） |
| EthIf | 2048 | —（LwIP 内部） |
| EthLink | 2048 | —（LwIP 内部） |
| heartbeatTask | 128 | Low |

## 引脚分配速查

```
ETH RMII    : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF卡 : PC8-PC12/PD2
FDCAN1      : PD0(RX) PD1(TX)
继电器      : PE7(Relay1) PE8(Relay2) — 高电平触发，上电默认低
调试LED     : PE10(DBG_LED1) PE11(DBG_LED2) — 高电平点亮
USART2      : PD5(TX) PD6(RX) — 115200 8N1
```

## 崩溃调试

故障处理函数会打印 PC/LR/CFSR 到 USART2。崩溃后执行：

```bash
arm-none-eabi-addr2line -e build/CANbus_code.elf -f -C <PC十六进制地址>
```

## 相关文档

- `CLAUDE.md` — 完整架构约束、CubeMX 覆盖文件列表、LwIP/MPU/Cache 细节
- `PROJECT_REQUIREMENTS.md` — 硬件规格、引脚分配、阶段路线图
- `DEBUG_LOG.md` — 每个 bug 的现象→排查→根因→修复，编号经验 #1-#23
- `BUILD_AND_TEST.md` — 工具链安装、接线指南、分步验证
