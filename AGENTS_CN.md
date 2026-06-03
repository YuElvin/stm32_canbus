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
- **每次编译成功后**：`git add -A; git commit -m "..." ; git push`

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
// MX_FDCAN1_Init();       ← 已禁用，待阶段 5 恢复
MX_QUADSPI_Init();         // W25Q128
// MX_SDMMC1_SD_Init();    ← 禁止调用：FatFs 的 BSP_SD_Init 内部处理
MX_USART2_UART_Init();
MX_FATFS_Init();           // 注册 SD_Driver（不初始化卡）
SD_Detect_GPIO_Init();     // PA8 卡检测引脚（输入 + 上拉）
// → QSPI_Verify + SD_Verify 在 initTestTask（freertos.c）调度器启动后执行
```

### SDMMC 陷阱 — 五条关键规则

1. **禁止调用 `MX_SDMMC1_SD_Init()`** —— FatFs 的 `SD_initialize()` 在 `f_mount()` 内部调用 `BSP_SD_Init()`。双重 `HAL_SD_Init()` 导致 `FR_NOT_READY`。

2. **所有 FatFs/SD 操作必须在 FreeRTOS 任务中执行**（`osKernelStart()` 之后）。`sd_diskio.c:SD_initialize()` 检查 `osKernelGetState() == osKernelRunning`，内核未运行时跳过初始化。在 `main()` 中、调度器启动前调用 `f_mount()`/`SD_Verify()` 将静默失败，输出 `FR_NOT_READY, state=0 error=0`。

3. **STM32H7 HAL `SD_ERROR_UNSUPPORTED_FEATURE`（0x80000000）** —— `HAL_SD_Init()` 可能返回此错误但卡实际已就绪（`State=HAL_SD_STATE_READY`）。本仓库的 `BSP_SD_Init()` 会检查 `State` 并清除错误码。不要删除此容错逻辑。

4. **4 位总线 → 1 位降级** —— `HAL_SD_ConfigWideBusOperation(4B)` 在某些硬件上可能报 `CMD_CRC_FAIL`（0x01）。`BSP_SD_Init()` 会自动降级为 1 位模式。诊断信息打印到 USART2。

5. **PA8 卡检测已绕过** —— `BSP_SD_IsDetected()` 当前硬编码返回 `SD_PRESENT`。PA8 极性和连接尚未用万用表确认。确认前不要恢复 PA8 检测逻辑。

## QSPI W25Q128 — 避免使用 AutoPolling

STM32H7 上 `HAL_QSPI_AutoPolling()` 在 `HAL_QSPI_Command()` 之后调用会出现状态机冲突。应改用手动轮询 `ReadStatusReg1()` 的方式。详见 `w25qxx.c:WaitBusy()`。

## LAN8720 PHY 驱动

CubeMX 选了 `lan8742.c` 但硬件是 LAN8720。关键差异：
- 无 SMR 寄存器（0x12）—— 基于 SMR 的地址扫描会返回垃圾值
- 解决方案：在 `ethernetif.c` 中用 BSR 探测地址 0-31，重试 5 次
- `ETH_PHY_IO_Init()` 中有 `HAL_Delay(2000)` —— **不能删除**，LAN8720 模块上电后需要 2 秒 MDIO 才稳定

## FreeRTOS 栈大小

所有网络任务栈必须 ≥ 2048 words（8KB）。深调用链 `ip4_input → etharp_query → etharp_request` 在 1024 words 时会栈溢出。

| 任务 | 栈大小 | 优先级 | 说明 |
|---|---|---|---|
| defaultTask | 2048 | Normal | LwIP 初始化后退出 |
| initTestTask | 1024 | Normal | QSPI + SD 验证后退出 |
| tcpip_thread | 2048 | — | LwIP 内部 |
| EthIf | 2048 | — | LwIP 内部 |
| EthLink | 2048 | — | LwIP 内部 |
| heartbeatTask | 128 | Low | PE10 1Hz 心跳 |

## 引脚分配速查

```
ETH RMII    : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF卡 : PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CK) PD2(CMD)
SD 卡检测   : PA8（输入 + 上拉，代码中已绕过 — 见 SDMMC 陷阱）
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

## 串口调试输出 — SD 错误码

`[SD] HAL SD state=X error=Y` — 常见值：
| state | 含义 | error | 含义 |
|---|---|---|---|
| 0 | RESET（HAL_SD_Init 从未调用） | 0 | 无错误 |
| 1 | READY（卡初始化成功） | 0x01 | CMD_CRC_FAIL |
| | | 0x80000000 | UNSUPPORTED_FEATURE（可恢复） |

## 相关文档

- `CLAUDE.md` — 完整架构约束、CubeMX 覆盖文件列表、LwIP/MPU/Cache 细节
- `PROJECT_REQUIREMENTS.md` — 硬件规格、引脚分配、阶段路线图
- `DEBUG_LOG.md` — 每个 bug 的现象→排查→根因→修复，编号经验 #1-#27
- `BUILD_AND_TEST.md` — 工具链安装、接线指南、分步验证
