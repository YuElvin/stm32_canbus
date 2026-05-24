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
| 3 | 冷启动 PHY 探测失败 | 上电时 addr=31，Reset 后 addr=1 | LAN8720 无复位引脚，上电后 MDIO 未稳定，读到无效值 | `ETH_PHY_IO_Init()` 加 `HAL_Delay(300)` | `LWIP/Target/ethernetif.c` |
| 4 | 多网卡路由走错 | ping 回复来自 10.0.40.9 | Windows 默认路由走 WiFi | ping 加 `-S 192.168.1.100` 指定源地址 | — （电脑配置） |
| 5 | Ping 无回复（根本问题） | 全部超时或"无法访问目标主机" | 发送路径缺 `SCB_CleanDCache_by_Addr`，DMA 读到脏 Cache 数据 | `low_level_output()` 每个 pbuf 发送前 Clean D-Cache | `LWIP/Target/ethernetif.c` |
| 6 | 接收任务栈太小 | 可能引发栈溢出、接收崩溃 | `INTERFACE_THREAD_STACK_SIZE = 350`（1400B）太小 | 增大到 512 words（2048B） | `LWIP/Target/ethernetif.c` |

---

## 六、关键经验总结

### STM32H7 以太网调试三大坑

1. **D-Cache Clean（发送）**：pbuf payload 在 Cacheable 的 SRAM 里，发送前必须 `SCB_CleanDCache_by_Addr`，否则 DMA 读到旧数据，发出去的包内容错误。

2. **D-Cache Invalidate（接收）**：DMA 写完 Rx Buffer 后，CPU 读之前必须 `SCB_InvalidateDCache_by_Addr`，否则 CPU 读到的是 Cache 里的旧数据。（HAL_ETH_RxLinkCallback 里已有，无需改动）

3. **ETH DMA 描述符必须在 D2 SRAM**：ETH DMA 只能访问 D2 SRAM（0x30000000），且必须配置 MPU Non-Cacheable。CubeMX 生成的链接脚本不会自动保证这一点，必须手动在 ld 脚本里强制地址。

### LAN8720 使用注意

1. **没有 SMR 寄存器**：不能用 LAN8742 驱动的 SMR 自动扫描 PHY 地址，要改用 BSR（标准寄存器，所有 PHY 都有）探测。

2. **没有 RESET 引脚**（模块未引出）：上电后必须等待 ≥300ms 再初始化 MDIO，否则读不到有效值。如果后续自己画板，强烈建议把 LAN8720 的 NRST 引脚接到 STM32 的一个 GPIO。

3. **PHY 地址由硬件引脚决定**：该模块地址为 1（由 PHYAD[2:0] 引脚决定，模块内部已固定）。

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
| `88bf2a0` | **D-Cache Clean on Tx + 增大 EthIf 任务栈**（解决 ping 不通根本原因） |

---

## 八、当前状态与下一步

**当前状态**：等待第四轮烧录测试验证 ping 通

**预期串口输出（上电后）**：
```
[ETH] LAN8720 addr=1, link=probed-OK
[ETH] PHY link state: 2
```

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
