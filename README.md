# STM32H750 CAN/CAN-FD 网关

> 最后更新：2026-05-25 | 项目阶段：阶段 1（以太网验证） | 对应 commit：`bdf031b`

基于 STM32H750VBT6 的 CAN 数据采集解析网关，支持 DBC 信号解析、规则引擎控制继电器、以太网 Web 配置界面，后续可通过 ESP32-C3 扩展 WiFi。

## 硬件平台

| 部件 | 型号 | 接口 |
|---|---|---|
| 主控 | YD-STM32H750VBT6 开发板 | Cortex-M7 @ 400MHz, 128KB Flash |
| 以太网 PHY | LAN8720 模块（50MHz 有源晶振） | RMII |
| CAN 收发器 | TJA1042 | CAN-FD |
| 外部 Flash | W25Q128（板载） | QUADSPI |
| TF 卡 | 板载卡座 | SDMMC1 4-bit |
| 继电器 | 2 路（高电平触发） | GPIO (PE7/PE8) |
| 调试串口 | USART2 (PD5/PD6) | 115200 8N1 |

## 开发阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| 1 | 以太网验证 — LAN8720 ping 通 | 开发完成，待硬件验证 |
| 2 | FDCAN1 收发 + DBC 解析 | 待开发 |
| 3 | QSPI W25Q128 读写 | 待开发 |
| 4 | SDMMC + FatFs 日志记录 | 待开发 |
| 5 | 以太网 Web 配置界面 | 待开发 |

## 快速上手

### 环境准备

- **工具链**：[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) `arm-none-eabi-gcc` 13.x+
- **构建工具**：GNU Make（Windows 下用 Git Bash 运行）
- **烧录**：ST-Link 或 J-Link（SWD 接口 PA13/PA14）

### 编译

```bash
# Git Bash 中运行（Windows 需指定 make 和 gcc 路径，详见 CLAUDE.md）
cd CANbus_code
make
```

产物在 `CANbus_code/build/`：`.bin` / `.hex` / `.elf`。

当前 Flash 占用约 **76KB / 128KB**（`-Og` 调试优化）。

### 烧录

```bash
# ST-Link CLI
ST-LINK_CLI.exe -c SWD -w build/CANbus_code.bin 0x08000000 -v -Rst

# 或 OpenOCD
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program build/CANbus_code.elf verify reset exit"
```

### 验证

1. 上电后 PE7 心跳 LED 每 500ms 闪烁（系统存活指示）
2. 串口（115200）输出 PHY 地址和链路状态
3. 网线直连电脑，`ping 192.168.1.88`

## 工程结构

```
├── .github/workflows/build.yml   # GitHub Actions CI（自动编译 + 产物上传）
├── CANbus_code/
│   ├── CANbus_code.ioc           # CubeMX 配置（引脚/时钟/外设）
│   ├── STM32H750XX_FLASH.ld      # 链接脚本（含 .lwip_sec D2 SRAM 段）
│   ├── Makefile                  # 构建脚本
│   ├── Core/
│   │   ├── Inc/                  # 头文件（FreeRTOSConfig.h 等）
│   │   └── Src/
│   │       ├── main.c            # 外设初始化 + MPU + 异常处理
│   │       ├── freertos.c        # FreeRTOS 任务（LwIP init + 心跳）
│   │       ├── stm32h7xx_it.c    # 中断向量 + HardFault 诊断
│   │       └── gpio/usart/fdcan/quadspi/sdmmc.c
│   ├── LWIP/
│   │   ├── App/lwip.c            # IP 配置 (192.168.1.88)
│   │   └── Target/
│   │       ├── ethernetif.c      # ETH DMA 驱动（含 D-Cache 处理）
│   │       └── lwipopts.h        # LwIP 参数
│   ├── FATFS/                    # FatFs（阶段 4 启用）
│   └── Drivers/                  # HAL + BSP + CMSIS
├── STM32项目沟通.docx          # 原始需求沟通记录（归档）
├── PROJECT_REQUIREMENTS.md       # 完整硬件需求和引脚分配
├── BUILD_AND_TEST.md             # 工具链安装和编译详细指南
├── DEBUG_LOG.md                  # 调试历史和问题排查记录
├── PROJECT_AUDIT.md              # 项目审核报告 + 优化方案（合并）
└── CLAUDE.md                     # Claude Code 辅助指令（架构约束）
```

## 关键设计决策

- **D-Cache 处理**：ETH DMA 描述符和 Rx 缓冲池放在 D2 SRAM（MPU Non-Cacheable），不需要手动 Clean/Invalidate D-Cache。见 `lwipopts.h` 注释。
- **ETH_PAD_SIZE=0**：通过覆写 `SMEMCPY` 为逐字节拷贝解决 Cortex-M7 非对齐访问问题，而非添加以太网帧 padding。见 `lwipopts.h:124-138`。
- **FreeRTOS 堆 32KB**：LwIP + ETH 需 4 个任务 + 队列/信号量，默认 15KB 不够。
- **PHY BSR 探测**：LAN8720 无 SMR 寄存器（lan8742.c 的 SMR 扫描不兼容），改用 BSR 探测地址 0/1。

## 相关文档

- [需求规格](PROJECT_REQUIREMENTS.md) — 硬件清单、引脚分配、功能需求
- [编译指南](BUILD_AND_TEST.md) — 工具链安装、编译步骤、常见问题
- [调试日志](DEBUG_LOG.md) — 每个问题的现象、排查过程、根因和修复
- [审核与优化方案](PROJECT_AUDIT.md) — 项目审核报告 + 分批次优化计划（合并）

## 许可证

私有项目，未公开授权。
