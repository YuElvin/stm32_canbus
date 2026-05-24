# 调试日志 — STM32H750 + LAN8720 以太网 Ping 验证

> 项目：STM32H750 CAN 网关
> 调试阶段：阶段 1 — 单独验证 LAN8720 以太网 Ping 通
> 记录时间：2026-05-24

---

## 一、目标

烧录只包含以太网功能的最小固件，验证 STM32H750 + LAN8720 模块能被电脑 ping 通。
固定 IP：192.168.1.88，串口输出 PHY 地址和链路状态。

---

## 二、硬件环境

| 部件 | 型号 |
|---|---|
| 主控 | YD-STM32H750VBT6 核心板 |
| PHY | LAN8720A 模块（板载 50MHz 有源晶振，无 RESET 引脚引出） |
| 接口 | RMII，9 根线，PA1 接 nINT/RETCLK（50MHz 输入） |
| 调试串口 | USB-TTL → USART2（PD5/PD6，115200 8N1） |
| 电脑 | 以太网口 IP：192.168.1.100，直连开发板 |

---

## 三、初始工程状态

CubeMX 生成的工程存在以下问题，需要手动修复后才能编译运行：

### 问题 0：链接脚本缺少 ETH DMA 段定义

**现象**：`ethernetif.c` 中 ETH DMA 描述符使用 `__attribute__((section(".RxDescripSection")))` 等属性，但链接脚本 `STM32H750XX_FLASH.ld` 完全没有定义这些段。

**影响**：链接器将这些缓冲区随机放置，不能保证落在 D2 SRAM（0x30000000），导致 ETH DMA 与 MPU Non-Cacheable 配置不一致，以太网可能无法工作。

**修复**：在链接脚本末尾（`/DISCARD/` 之前）添加：
```ld
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
**原因**：STM32H7 的以太网 DMA 描述符和接收缓冲池必须放在 D2 SRAM，并且该区域必须配置为 Non-Cacheable，否则 CPU Cache 和 DMA 看到的数据不一致，会导致收发异常。MPU Region 0 已经把 0x30000000 起的 256KB 配置为 Non-Cacheable，所以必须强制让这些段落在这个地址。

---

### 问题 1：FatFs syscall.c 编译错误

**现象**：首次编译报错：
```
error: implicit declaration of function 'pvPortMalloc'
error: returning 'int' from a function with return type 'void *'
```

**原因**：`ffconf.h` 把 `ff_malloc` 定义为 `pvPortMalloc`，但 `syscall.c` 只 include 了 `ff.h`，没有 include FreeRTOS 头文件，导致 `pvPortMalloc` 和 `vPortFree` 未声明。

**修复**：在 `Middlewares/Third_Party/FatFs/src/option/syscall.c` 第 24 行后添加：
```c
#include "FreeRTOS.h"
#include "task.h"
```

**结果**：编译通过，Flash 占用 75116B（73.4KB / 128KB），零错误零警告。

---

## 四、调试过程记录

### 第一轮烧录测试

**串口输出**：
```
[ETH] PHY detected at address: 31
[ETH] PHY link state: 5
```

**问题分析**：
- PHY 地址 31 是错误的。`LAN8742_Init()` 通过扫描 SMR 寄存器（reg 0x12）来自动识别 PHY 地址，但 **LAN8720 没有 LAN8742 的 SMR 寄存器**。扫描时 addr=31 的 MDIO 读返回的随机值恰好使 `regvalue & 0x1F == 31` 匹配，于是错误地记录为地址 31。
- 链路状态 5 = `LAN8742_STATUS_10MBITS_HALFDUPLEX`（10M 半双工），说明 PHY 硬件实际是通的，RMII 接线正确，50MHz 时钟正常，但地址识别用了错误的方法。

**修复**：绕过 SMR 扫描，改用读 BSR（Basic Status Register，reg 0x01）来探测地址。BSR 是所有 IEEE 802.3 PHY 的标准寄存器，LAN8720 有效时会返回 0x7809 或类似非零非 0xFFFF 的值。

在 `PHY_PRE_CONFIG` 阶段探测地址 0 和 1：
```c
uint32_t bsr = 0;
uint32_t lan8720_addr = 0xFFFFFFFF;
for (probe = 0; probe <= 1; probe++) {
    if (ETH_PHY_IO_ReadReg(probe, 0x01, &bsr) == 0) {
        if (bsr != 0x0000 && bsr != 0xFFFF) {
            lan8720_addr = probe;
            break;
        }
    }
}
if (lan8720_addr != 0xFFFFFFFF) {
    LAN8742.DevAddr = lan8720_addr;
    LAN8742.Is_Initialized = 1;  /* 跳过 LAN8742_Init 内的 SMR 扫描 */
}
```

---

### 第二轮烧录测试（上电 vs Reset 的区别）

**上电串口输出**：
```
[ETH] LAN8720 addr=31, link=scan-fallback
[ETH] PHY link state: 5
```

**按 Reset 后串口输出**：
```
[ETH] LAN8720 addr=1, link=probed-OK
[ETH] PHY link state: 2
```

**问题分析**：
- **上电时**：BSR 探测 addr=0 和 addr=1 均返回 0x0000 或 0xFFFF，探测失败，回落到 SMR 扫描，得到错误地址 31。
- **Reset 后**：LAN8720 模块已经稳定运行了一段时间，BSR 探测成功，得到正确地址 1，链路状态 2 = 100M 全双工。

**根本原因**：LAN8720 模块没有引出 RESET 引脚，无法被 STM32 主动复位。上电后模块需要一定时间才能稳定响应 MDIO 通信。代码中在 `ETH_PHY_IO_Init()` 里没有等待时间，初始化太早，MDIO 读不到有效值。

**修复**：在 `ETH_PHY_IO_Init()` 的 `HAL_ETH_SetMDIOClockRange()` 调用之后加 300ms 延时：
```c
HAL_ETH_SetMDIOClockRange(&heth);
HAL_Delay(300);  /* 等待 LAN8720 上电稳定 */
```

**注意**：LAN8720 datasheet 要求上电后至少 25ms，考虑到没有 RESET 引脚，实际建议等待 100~300ms。

---

### 第三轮烧录测试（PHY OK 但 Ping 不通）

**上电串口输出**：
```
[ETH] LAN8720 addr=1, link=probed-OK
[ETH] PHY link state: 1
```
（链路状态 1 = LINK_DOWN，说明上电 300ms 后链路协商还未完成，属于正常，`ethernet_link_thread` 会持续轮询）

**第一次 Ping 结果**（未指定源地址）：
```
来自 192.168.1.100 的回复: 无法访问目标主机。  ← 路由走错了网卡
来自 10.0.40.9 的回复: TTL 传输中过期。        ← WiFi 网关在转发
```

**问题分析**：电脑有多个网络接口（有线 + WiFi），ping 命令走了默认路由，没有从直连 STM32 的以太网口发出。

**修复**：使用 `-S` 参数指定源地址：
```powershell
ping 192.168.1.88 -S 192.168.1.100
```

**按 Reset 后再次 Ping（指定源地址）**：
```
来自 192.168.1.100 的回复: 无法访问目标主机。（4次）
```

**新问题**：路由问题解决了（回复来自 192.168.1.100，说明从正确网卡出去了），但 STM32 没有回复。

---

### 第四轮分析（Ping 不通的真正原因）

**问题一：ETH 接收任务栈太小**

`ethernetif.c` 中：
```c
#define INTERFACE_THREAD_STACK_SIZE ( 350 )  /* 350 words = 1400 bytes */
```
`ethernetif_input` 任务在调用 `netif->input()` → `ethernet_input()` → `etharp_input()` 时调用链较深，350 words 的栈极有可能溢出，导致接收任务崩溃或行为异常。

**修复**：增大到 512 words（2048 bytes）。

**问题二：发送路径缺少 D-Cache Clean（根本原因）**

`low_level_output()` 中遍历 pbuf 链发送数据，但完全没有调用 `SCB_CleanDCache_by_Addr`。

- pbuf 的 payload 存储在 AXI SRAM（0x24000000），该区域在 MPU Region 1 中配置为 **Cacheable**。
- LwIP 协议栈（在 CPU 上运行）填写 ARP Reply / ICMP Echo Reply 的 payload 后，数据在 D-Cache 中是脏数据，尚未写回到实际内存。
- ETH DMA 直接读物理内存，读到的是修改前的旧数据（可能是全零或之前的帧），发出去的包内容错误，对端自然丢弃。
- 接收路径（`HAL_ETH_RxLinkCallback`）里已经有 `SCB_InvalidateDCache_by_Addr`，但发送路径漏了。

**这是 STM32H7 以太网最常见的坑之一。**

**修复**：在 `low_level_output()` 的 pbuf 遍历循环中，每段 payload 填入 Txbuffer 后立即 Clean Cache：
```c
Txbuffer[i].buffer = q->payload;
Txbuffer[i].len = q->len;

/* D-Cache Clean：确保 ETH DMA 读到 CPU 最新写入的数据 */
SCB_CleanDCache_by_Addr((uint32_t *)q->payload,
                        (int32_t)((q->len + 31) & ~31U));
```
长度向上对齐到 32 字节（Cache line 大小）是为了覆盖完整的 Cache line，避免部分行未被 Clean。

---

## 五、各问题与修复汇总

| # | 问题 | 现象 | 根本原因 | 修复方法 | 修复文件 |
|---|---|---|---|---|---|
| 0 | 链接脚本缺 ETH DMA 段 | ETH DMA 描述符未落在 D2 SRAM | CubeMX 未生成 `.RxDescripSection` 等段定义 | 手动在 ld 脚本加 `.lwip_sec` 段，强制映射到 0x30000000 | `STM32H750XX_FLASH.ld` |
| 1 | FatFs 编译错误 | `pvPortMalloc` 未声明 | `syscall.c` 缺少 FreeRTOS 头文件 | 加 `#include "FreeRTOS.h"` 和 `#include "task.h"` | `FatFs/src/option/syscall.c` |
| 2 | PHY 地址识别错误（31） | 串口输出 `addr=31` | LAN8720 无 SMR 寄存器，LAN8742 驱动的 SMR 扫描失效 | 改用 BSR 寄存器探测地址 0 和 1 | `LWIP/Target/ethernetif.c` |
| 3 | 冷启动 PHY 探测失败 | 上电时 addr=31，Reset 后 addr=1 | LAN8720 无复位引脚，上电后 MDIO 未稳定，读到无效值 | `ETH_PHY_IO_Init()` 加 `HAL_Delay(1000)` | `LWIP/Target/ethernetif.c` |
| 4 | 多网卡路由走错 | ping 回复来自 10.0.40.9 | Windows 默认路由走 WiFi | ping 加 `-S 192.168.1.100` 指定源地址 | — （电脑配置） |
| 5 | Ping 无回复 | 全部超时或"无法访问目标主机" | 发送路径缺 `SCB_CleanDCache_by_Addr`，DMA 读到脏 Cache 数据 | `low_level_output()` 每个 pbuf 发送前 Clean D-Cache（地址向下对齐到 32 字节） | `LWIP/Target/ethernetif.c` |
| 6 | 接收任务栈太小 | 可能引发栈溢出 | `INTERFACE_THREAD_STACK_SIZE = 350`（1400B）太小 | 增大到 512 words（2048B） | `LWIP/Target/ethernetif.c` |
| 7 | LWIP_RAM_HEAP_POINTER 与 Rx 缓冲池冲突 | ping 无回复，ARP 数据被覆盖 | Rx_PoolSection 结束于 0x30004A83，堆起点 0x30004000 落在 pool 内部，LwIP 堆分配覆盖 Rx 缓冲池 | `LWIP_RAM_HEAP_POINTER` 从 0x30004000 改为 0x30005000 | `LWIP/Target/lwipopts.h` |
| 8 | FreeRTOS 堆不足，程序卡死 | PE7 常亮或不亮，MX_LWIP_Init 卡死 | `configTOTAL_HEAP_SIZE=15360`（15KB）不够，4 个任务+队列+信号量超出上限，tcpip_init 内部任务创建失败 | 15360 → 32768（32KB） | `Core/Inc/FreeRTOSConfig.h` |
| 9 | 心跳灯与 LwIP 耦合，无法区分崩溃类型 | PE7 常亮/不亮/闪几下熄灭，无法判断是 LwIP 卡死还是系统崩溃 | 心跳和 LwIP 初始化在同一任务，LwIP 任何阻塞都影响心跳 | 新增独立 heartbeatTask（osPriorityLow），defaultTask 只做 LwIP 初始化后退出 | `Core/Src/freertos.c` |
| 10 | LwIP 堆未定义 MEM_SIZE，默认 1600B 不够 | PE7 闪几下后熄灭，configASSERT 触发 | `lwipopts.h` 未定义 `MEM_SIZE`，LwIP 使用默认 1600 字节，pbuf/TCP 缓冲区分配失败触发 LWIP_ASSERT | `MEM_SIZE = 16 * 1024` | `LWIP/Target/lwipopts.h` |
| 11 | configASSERT 和 fault handler 静默死循环 | 崩溃后无任何输出，无法定位 | 原 `configASSERT` 直接 `taskDISABLE_INTERRUPTS+for(;;)`，fault handler 也是 `while(1)` | configASSERT 改为打印文件名行号；HardFault/MemManage/BusFault/UsageFault 改为从栈帧读 PC/LR/CFSR 打印 | `FreeRTOSConfig.h`、`stm32h7xx_it.c`、`main.c` |
| 12 | LwIP ARP 非对齐访问 HardFault（根本原因） | 每次上电必崩，`[HARDFAULT] PC=0x0800DAA2 CFSR=0x01000000` | 以太网帧头 14 字节，ARP 头内部字段偏移 18 字节，不是 4 字节对齐；LwIP SMEMCPY 展开为 `str.w`，Cortex-M7 UNALIGN_TRP 触发 UsageFault→HardFault | `ETH_PAD_SIZE=2`，在 pbuf 前插入 2 字节填充，使所有字段 4 字节对齐 | `LWIP/Target/lwipopts.h` |

---

## 六、关键经验总结

### STM32H7 以太网调试坑（按踩坑顺序）

1. **链接脚本必须手动加 ETH DMA 段**：CubeMX 不会自动生成 `.RxDescripSection` 等段，必须手动在 ld 脚本里强制映射到 D2 SRAM（0x30000000）。

2. **D-Cache Clean（发送）**：pbuf payload 在 Cacheable 的 AXI SRAM，发送前必须 `SCB_CleanDCache_by_Addr`，地址要向下对齐到 32 字节 cache line 边界，否则 DMA 读到旧数据，发出去的包内容错误。

3. **D-Cache Invalidate（接收）**：DMA 写完 Rx Buffer 后，CPU 读之前必须 `SCB_InvalidateDCache_by_Addr`。`HAL_ETH_RxLinkCallback` 里已有，不要删。

4. **LWIP_RAM_HEAP_POINTER 必须在 Rx 缓冲池之后**：CubeMX 默认值 0x30004000 会与 Rx_PoolSection 重叠（pool 约 18.3KB，结束于 0x30004A83）。必须通过 `.map` 文件确认 pool 实际结束地址，再设置堆起点。

5. **必须显式定义 MEM_SIZE**：LwIP 默认 MEM_SIZE 只有 1600 字节，远不够用，必须在 `lwipopts.h` 里显式设置（本项目用 16KB）。

6. **ETH_PAD_SIZE=2 是必须的**：以太网帧头 14 字节，不加 padding 时 ARP/IP 结构体字段会落在非 4 字节对齐地址，Cortex-M7 的 UNALIGN_TRP 触发 UsageFault→HardFault。这是 LwIP 在 STM32H7 上的已知问题。

7. **FreeRTOS 堆要足够大**：LwIP + ETH 需要同时运行 4 个任务（defaultTask/tcpip_thread/EthIf/EthLink），加上队列、信号量、TCB 开销，15KB 不够，至少需要 32KB。

### LAN8720 使用注意

1. **没有 SMR 寄存器**：不能用 LAN8742 驱动的 SMR 自动扫描 PHY 地址，要改用 BSR（标准寄存器，所有 PHY 都有）探测地址 0 和 1。

2. **没有 RESET 引脚**（模块未引出）：上电后必须等待 ≥1000ms 再初始化 MDIO，否则读不到有效值。如果后续自己画板，强烈建议把 LAN8720 的 NRST 引脚接到 STM32 的一个 GPIO。

3. **PHY 地址由硬件引脚决定**：该模块地址为 1（由 PHYAD[2:0] 引脚决定，模块内部已固定）。

### 调试方法经验

1. **心跳灯要独立任务**：不要把心跳和业务逻辑放在同一个任务，否则业务卡死时无法区分是"业务卡死"还是"系统崩溃"。

2. **configASSERT 必须打印**：默认的 `taskDISABLE_INTERRUPTS+for(;;)` 完全静默，改为打印文件名和行号后立即能定位问题。

3. **Fault Handler 必须打印 PC/LR/CFSR**：HardFault 等异常的默认 `while(1)` 无法定位，从异常栈帧读出 PC 后用 `arm-none-eabi-addr2line` 可以精确到源码行。

4. **用 .map 文件验证内存布局**：每次修改内存相关配置后，检查 `build/CANbus_code.map` 确认各段实际地址，避免地址冲突。

### Windows 多网卡 Ping 问题

- 电脑同时连 WiFi 和有线时，ping 走的是默认路由（通常是 WiFi）
- 指定源地址：`ping 目标IP -S 直连网卡IP`
- 或者临时关闭 WiFi 再测试

---

## 七、commit 记录

| commit | 说明 |
|---|---|
| `7e8ebc3` | 项目初始化，CubeMX 工程骨架入库 |
| `a838c9d` | LAN8720 ping-only 固件适配：链接脚本修复、main.c 剥离、ethernetif.c 打印 PHY 信息、PE7 心跳灯 |
| `3495682` | 修复 FatFs/syscall.c 编译错误，完成首次成功构建（75KB，零错误） |
| `421bcd0` | LAN8720 PHY 地址探测改用 BSR，绕过 LAN8742 的 SMR 扫描 |
| `0225cfe` | LAN8720 上电延时 300ms，解决冷启动 PHY 探测失败 |
| `88bf2a0` | D-Cache Clean on Tx（地址对齐修正）+ 增大 EthIf 任务栈 |
| `06e5c99` | 修复 LWIP_RAM_HEAP_POINTER 与 Rx 缓冲池地址冲突（通过 .map 文件确认） |
| `0604050` | FreeRTOS 堆扩大到 32KB + 上电延时增加到 500ms |
| `b07d2d8` | 心跳灯独立任务（heartbeatTask），defaultTask 只做 LwIP 初始化后退出 |
| `6cf010b` | 显式定义 MEM_SIZE=16KB，解决 LwIP 堆耗尽导致 assert 崩溃 |
| `a390aa8` | configASSERT 改为串口打印文件名行号；Error_Handler 打印 LR |
| `d5c8e19` | HardFault/MemManage/BusFault/UsageFault 改为打印 PC/LR/CFSR；上电延时增加到 1000ms |
| `15dcadf` | **ETH_PAD_SIZE=2，解决 LwIP ARP 非对齐访问 HardFault（最终根本原因）** |

---

## 八、当前状态与下一步

**当前状态**：等待烧录 commit `15dcadf` 验证 ping 通

**预期串口输出（上电后）**：
```
[ETH] LAN8720 addr=1, link=probed-OK
[ETH] PHY link state: 2
```
无任何 `[HARDFAULT]` 输出，PE7 持续 1Hz 闪烁。

**预期 Ping 结果**：
```powershell
ping 192.168.1.88 -S 192.168.1.100
# 来自 192.168.1.88 的回复: 字节=32 时间<1ms TTL=255
```

**Ping 通后下一步（按 CLAUDE.md 路线）**：
- 阶段 2：恢复 FDCAN1 初始化，验证 CAN 收发（串口打印原始帧）
- 阶段 3：恢复 QSPI，读 W25Q128 JEDEC ID（应得到 `EF 40 18`）
- 阶段 4：恢复 SDMMC + FatFs，挂载 TF 卡，写 `/log/test.csv`
- 阶段 5：以太网 + CAN 联调，Web 显示原始 CAN 帧
