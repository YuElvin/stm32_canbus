# STM32H750 CAN 网关 — 当前配置清单

> 基于 commit `a838c9d` — feat: LAN8720 ping-only 固件适配
> 生成时间：2026-05-24

---

## 一、CubeMX 配置（.ioc 文件）

### 1.1 MCU 基本信息

| 项目 | 配置 |
|---|---|
| 芯片型号 | STM32H750VBTx |
| 封装 | LQFP100 |
| 内部 Flash | 128KB |
| 内部 SRAM | DTCM 128KB + AXI 512KB + D2 288KB + D3 64K |
| CubeMX 版本 | 6.17.0 |
| HAL 库版本 | STM32Cube FW_H7 V1.13.0 |
| 工具链 | Makefile (GCC) |

### 1.2 时钟树配置

| 时钟源 | 频率 | 说明 |
|---|---|---|
| HSE 外部晶振 | 25 MHz | 板载有源晶振 |
| LSE 外部晶振 | 32.768 kHz | RTC 时钟源 |
| PLL1 输入 | 25 MHz / 2 = 12.5 MHz | DIVM1=2 |
| PLL1 VCO | 12.5 MHz × 64 = 800 MHz | DIVN1=64 |
| PLL1 P 输出（SYSCLK） | 800 MHz / 2 = **400 MHz** | DIVP1=2，CPU 主频 |
| PLL1 Q 输出 | 800 MHz / 8 = **100 MHz** | DIVQ1=8，FDCAN/USB |
| AHB 总线（HCLK） | 400 MHz / 2 = **200 MHz** | HPRE=DIV2 |
| APB1 总线 | 200 MHz / 2 = **100 MHz** | D2PPRE1=DIV2 |
| APB2 总线 | 200 MHz / 2 = **100 MHz** | D2PPRE2=DIV2 |
| APB3 总线 | 200 MHz / 2 = **100 MHz** | D1PPRE=DIV2 |
| APB4 总线 | 200 MHz / 2 = **100 MHz** | D3PPRE=DIV2 |

**关键频率**：
- CPU (Cortex-M7)：400 MHz
- AXI/AHB：200 MHz
- FDCAN 时钟：100 MHz
- ETH 参考时钟：50 MHz（外部 LAN8720 模块提供）

### 1.3 MPU（内存保护单元）配置

| 区域 | 基地址 | 大小 | 属性 | 用途 |
|---|---|---|---|---|
| Region 0 | 0x30000000 | 256KB | Non-Cacheable, Bufferable, Full Access | **D2 SRAM（ETH DMA 描述符区）** |
| Region 1 | 0x24000000 | 512KB | Cacheable, Bufferable, Full Access | AXI SRAM（普通数据） |

**关键**：Region 0 必须配置为 **Non-Cacheable**，否则 ETH DMA 会因 Cache 一致性问题导致收发失败。

### 1.4 外设配置

#### 1.4.1 ETH（以太网 MAC）

| 参数 | 配置 |
|---|---|
| 接口模式 | RMII（Reduced MII） |
| PHY 芯片 | LAN8742（CubeMX 选的，实际硬件是 LAN8720，寄存器兼容） |
| 中断优先级 | 5（FreeRTOS 管理） |

**引脚分配**：
```
PA1   ETH_REF_CLK     ← 50MHz 时钟输入（LAN8720 模块提供）
PA2   ETH_MDIO        管理数据
PA7   ETH_CRS_DV      载波侦听/数据有效
PC1   ETH_MDC         管理时钟
PC4   ETH_RXD0        接收数据 0
PC5   ETH_RXD1        接收数据 1
PB11  ETH_TX_EN       发送使能
PB12  ETH_TXD0        发送数据 0
PB13  ETH_TXD1        发送数据 1
```

#### 1.4.2 FDCAN1（CAN-FD）

| 参数 | 配置 |
|---|---|
| 模式 | FDCAN_Activate |
| 自动重传 | 使能 |
| 标称波特率（计算值） | 2.083 Mbps（实际使用时会重新配置为 500K/1M） |
| 时间量子 | 160 ns |
| 中断优先级 | 5（FreeRTOS 管理） |

**引脚分配**：
```
PD0   FDCAN1_RX
PD1   FDCAN1_TX
```

**注意**：当前固件已注释掉 FDCAN1 初始化（仅验证以太网）。

#### 1.4.3 QUADSPI（外部 Flash W25Q128）

| 参数 | 配置 |
|---|---|
| 模式 | Single Bank 1 |
| 时钟分频 | 5（QSPI 时钟 = 200MHz / (5+1) ≈ 33.3 MHz） |
| Flash 大小 | 2^23 = 8MB（实际 W25Q128 是 16MB，配置偏小） |
| 片选高电平时间 | 2 个时钟周期 |
| 采样移位 | 半周期 |
| FIFO 阈值 | 4 字节 |

**引脚分配**：
```
PB2   QUADSPI_CLK
PB10  QUADSPI_BK1_NCS
PD11  QUADSPI_BK1_IO0
PD12  QUADSPI_BK1_IO1
PE2   QUADSPI_BK1_IO2
PD13  QUADSPI_BK1_IO3
```

**注意**：当前固件已注释掉 QSPI 初始化（仅验证以太网）。

#### 1.4.4 SDMMC1（TF 卡）

| 参数 | 配置 |
|---|---|
| 模式 | 4-bit Wide Bus |
| 时钟频率 | 默认 400kHz 初始化，协商后最高 50MHz |

**引脚分配**：
```
PC8   SDMMC1_D0
PC9   SDMMC1_D1
PC10  SDMMC1_D2
PC11  SDMMC1_D3
PC12  SDMMC1_CK
PD2   SDMMC1_CMD
```

**注意**：当前固件已注释掉 SDMMC1 初始化（仅验证以太网）。

#### 1.4.5 USART2（调试串口）

| 参数 | 配置 |
|---|---|
| 模式 | 异步（Asynchronous） |
| 波特率 | 115200（默认，代码中可能未显式设置） |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无 |

**引脚分配**：
```
PD5   USART2_TX → USB-TTL RX
PD6   USART2_RX ← USB-TTL TX
```

#### 1.4.6 GPIO（继电器 + 心跳灯）

| 引脚 | 模式 | 初始状态 | 用途 |
|---|---|---|---|
| PE7 | GPIO_Output | 低电平 | 继电器 1 / 心跳灯（当前用作心跳） |
| PE8 | GPIO_Output | 低电平 | 继电器 2 |

**注意**：当前固件中 PE7 用作心跳灯（500ms toggle），PE8 未使用。

### 1.5 中间件配置

#### 1.5.1 FreeRTOS

| 参数 | 配置 |
|---|---|
| 版本 | CMSIS-RTOS v2 API |
| 最小栈大小 | 512 字（configMINIMAL_STACK_SIZE） |
| 堆大小 | 0x200（512 字节，在 main.c 中定义） |
| 时基 | TIM6（HAL Tick 独立于 SysTick） |

**默认任务**：
- 名称：`defaultTask`
- 优先级：24（osPriorityNormal）
- 栈大小：512 × 4 = 2048 字节
- 入口函数：`StartDefaultTask`（在 freertos.c 中）

**当前任务行为**（手动修改）：
```c
void StartDefaultTask(void *argument)
{
  MX_LWIP_Init();  // 初始化 LwIP
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);  // PE7 心跳灯
    osDelay(500);  // 500ms toggle → 1Hz 闪烁
  }
}
```

#### 1.5.2 LwIP（轻量级 TCP/IP 协议栈）

| 参数 | 配置 |
|---|---|
| 版本 | v2.2.1_Cube |
| DHCP | 禁用（固定 IP） |
| IP 地址 | **192.168.1.88** |
| 子网掩码 | 255.255.255.0 |
| 网关 | 192.168.1.1 |
| PHY 驱动 | LAN8742（BSP 组件） |

**关键配置**（lwipopts.h）：
- `NO_SYS = 0`（使用 FreeRTOS）
- `MEM_ALIGNMENT = 4`
- `LWIP_NETIF_LINK_CALLBACK = 1`（链路状态回调）
- `LWIP_ETHERNET = 1`
- `LWIP_ARP = 1`
- `LWIP_ICMP = 1`（支持 ping）

#### 1.5.3 FatFs（文件系统）

| 参数 | 配置 |
|---|---|
| 长文件名支持 | 使能（_USE_LFN=3，动态分配） |
| 代码页 | 936（简体中文 GBK） |
| 底层驱动 | SDIO（SDMMC1） |

**注意**：当前固件已注释掉 FatFs 初始化（仅验证以太网）。

---

## 二、手动修改（非 CubeMX 生成）

### 2.1 链接脚本修复（STM32H750XX_FLASH.ld）

**问题**：CubeMX 生成的链接脚本缺少 ETH DMA 段定义，导致 `ethernetif.c` 中的 `__attribute__((section(...)))` 无法链接。

**修复内容**（在 `/DISCARD/` 之前插入）：
```ld
/* ETH DMA descriptors and Rx buffer pool must be in D2 SRAM (0x30000000) for cache coherency */
.lwip_sec (NOLOAD) :
{
  . = ABSOLUTE(0x30000000);
  *(.RxDescripSection)

  . = ABSOLUTE(0x30000080);
  *(.TxDescripSection)

  . = ABSOLUTE(0x30000100);
  *(.Rx_PoolSection)
} >RAM_D2
```

**作用**：
- `.RxDescripSection`：ETH 接收 DMA 描述符（4 个，每个 16 字节，共 64 字节）
- `.TxDescripSection`：ETH 发送 DMA 描述符（4 个，每个 16 字节，共 64 字节）
- `.Rx_PoolSection`：LwIP 接收缓冲池（12 个 1536 字节的 pbuf，约 18KB）
- 全部映射到 D2 SRAM（0x30000000），与 MPU Region 0 的 Non-Cacheable 配置对齐

### 2.2 main.c 剥离非以太网外设

**修改位置**：`Core/Src/main.c` 的 `main()` 函数

**修改内容**：
```c
/* Initialize all configured peripherals */
MX_GPIO_Init();
/* MX_FDCAN1_Init(); */     /* Disabled for ETH-only test */
/* MX_QUADSPI_Init(); */     /* Disabled for ETH-only test */
/* MX_SDMMC1_SD_Init(); */   /* Disabled for ETH-only test */
MX_USART2_UART_Init();
/* MX_FATFS_Init(); */       /* Disabled for ETH-only test */
```

**作用**：
- 只初始化 GPIO、USART2、LwIP（在 FreeRTOS 任务中）
- 减少代码体积，避免未连接外设的初始化错误
- 方便单独验证以太网功能

### 2.3 freertos.c 加心跳灯

**修改位置**：`Core/Src/freertos.c` 的 `StartDefaultTask()` 函数

**修改内容**：
```c
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);  /* PE7 heartbeat: 500ms toggle */
    osDelay(500);
  }
  /* USER CODE END StartDefaultTask */
}
```

**作用**：
- PE7 每 500ms 翻转一次（1Hz 闪烁）
- 证明程序在运行（未卡死）
- 可用继电器模块或万用表观察

### 2.4 ethernetif.c 打印 PHY 地址和链路状态

**修改位置**：`LWIP/Target/ethernetif.c`

**修改 1：头文件和外部声明**（USER CODE BEGIN 0）：
```c
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart2;
```

**修改 2：打印 PHY 地址**（`low_level_init()` 函数，LAN8742_Init 成功后）：
```c
/* USER CODE BEGIN PHY_POST_INIT */
/* Print detected PHY address to USART2 */
char msg[64];
sprintf(msg, "[ETH] PHY detected at address: %lu\r\n", LAN8742.DevAddr);
HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
/* USER CODE END PHY_POST_INIT */
```

**修改 3：打印链路状态**（`low_level_init()` 函数，LAN8742_GetLinkState 之后）：
```c
/* USER CODE BEGIN PHY_LINK_CHECK */
sprintf(msg, "[ETH] PHY link state: %ld\r\n", PHYLinkState);
HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
/* USER CODE END PHY_LINK_CHECK */
```

**作用**：
- 通过 USART2（115200 8N1）输出 PHY 地址（0-31，如果是 32 说明未检测到）
- 输出链路状态（1=LINK_DOWN, 2=100M全双工, 3=100M半双工, 4=10M全双工, 5=10M半双工, 6=自协商未完成）
- 方便实测确认 LAN8720 模块的 PHY 地址和链路协商结果

---

## 三、当前固件功能范围

### 3.1 已启用

✅ **以太网**：RMII 接口 + LAN8720 PHY + LwIP 协议栈
✅ **固定 IP**：192.168.1.88 / 255.255.255.0 / 网关 192.168.1.1
✅ **ICMP**：支持 ping
✅ **串口调试**：USART2 115200 8N1，输出 PHY 地址和链路状态
✅ **心跳灯**：PE7 以 1Hz 频率闪烁
✅ **FreeRTOS**：单任务（defaultTask），初始化 LwIP 后进入心跳循环
✅ **MPU**：D2 SRAM (0x30000000) 配置为 Non-Cacheable，确保 ETH DMA 一致性

### 3.2 已禁用（注释掉初始化）

❌ **FDCAN1**：CAN/CAN-FD 收发（一期 MVP 需要，当前测试不需要）
❌ **QSPI**：W25Q128 外部 Flash（一期验证，二期 XIP）
❌ **SDMMC1**：TF 卡读写（一期 MVP 需要，当前测试不需要）
❌ **FatFs**：文件系统（依赖 SDMMC1）

### 3.3 未实现（一期 MVP 需要）

⚠️ **DBC 解析**：CAN 报文解码
⚠️ **规则引擎**：信号 → 继电器控制
⚠️ **数据日志**：CSV 写 TF 卡
⚠️ **Web 服务器**：HTTP REST API
⚠️ **CAN 周期发送**：按 DBC 编码发送

---

## 四、验证目标（当前 commit）

| 项目 | 预期结果 |
|---|---|
| 编译 | `make` 无错误，生成 .elf/.hex/.bin |
| 烧录 | ST-Link 烧录成功，复位后程序运行 |
| 串口输出 | `[ETH] PHY detected at address: 0` 或 `1`（不是 32） |
| 串口输出 | `[ETH] PHY link state: 2`（或 3/4/5，不是 1 或 6） |
| 心跳灯 | PE7 以 1Hz 频率闪烁（继电器吸合 500ms → 释放 500ms） |
| Ping 测试 | 电脑 `ping 192.168.1.88` 稳定通，延迟 <1ms |

---

## 五、下一步计划（按 CLAUDE.md 路线）

1. ✅ **阶段 1：单独验证 LAN8720**（当前 commit）
2. ⏭️ **阶段 2：串口 + GPIO + 继电器**（恢复 PE8，加 printf 重定向）
3. ⏭️ **阶段 3：QSPI W25Q128**（读 JEDEC ID、擦写校验）
4. ⏭️ **阶段 4：SDMMC + FatFs**（挂载 TF 卡、写 `/log/test.csv`）
5. ⏭️ **阶段 5：FDCAN1**（内部回环 → 正常模式 + 分析仪收发）
6. ⏭️ **阶段 6：CAN + Web 原始帧**（Web 显示 CAN 原始数据、手发帧）
7. ⏭️ **阶段 7：日志**（CSV 写 TF 卡、Web 下载）
8. ⏭️ **阶段 8：DBC 解析**（Web 上传 DBC、解析显示物理值）
9. ⏭️ **阶段 9：规则引擎**（Web 配置 → 控制继电器 + 滞回/延时）
10. ⏭️ **阶段 10：DBC 编辑发送**（Web 表单按 DBC 编码发送）
11. ⏭️ **阶段 11：ESP32-C3 WiFi（二期）**（先 UART 验证，再 SPI）

---

**生成时间**：2026-05-24
**对应 commit**：`a838c9d` — feat: LAN8720 ping-only 固件适配
**参考文档**：`CLAUDE.md`、`BUILD_AND_TEST.md`
