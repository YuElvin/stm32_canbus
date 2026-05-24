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

> 本表按发现顺序排列。**带 ⚠️ 的修复后被推翻或修正**，请阅读到表末才能看到最终结论。

| # | 问题 | 现象 | 根本原因 | 修复方法 | 修复文件 |
|---|---|---|---|---|---|
| 0 | 链接脚本缺 ETH DMA 段 | ETH DMA 描述符未落在 D2 SRAM | CubeMX 未生成 `.RxDescripSection` 等段定义 | 手动在 ld 脚本加 `.lwip_sec` 段，强制映射到 0x30000000 | `STM32H750XX_FLASH.ld` |
| 1 | FatFs 编译错误 | `pvPortMalloc` 未声明 | `syscall.c` 缺少 FreeRTOS 头文件 | 加 `#include "FreeRTOS.h"` 和 `#include "task.h"` | `FatFs/src/option/syscall.c` |
| 2 | PHY 地址识别错误（31） | 串口输出 `addr=31` | LAN8720 无 SMR 寄存器，LAN8742 驱动的 SMR 扫描失效 | 改用 BSR 寄存器探测地址 0 和 1 | `LWIP/Target/ethernetif.c` |
| 3 | 冷启动 PHY 探测失败 | 上电时 addr=31，Reset 后 addr=1 | LAN8720 无复位引脚，上电后 MDIO 未稳定，读到无效值 | `ETH_PHY_IO_Init()` 加 `HAL_Delay(1000)` | `LWIP/Target/ethernetif.c` |
| 4 | 多网卡路由走错 | ping 回复来自 10.0.40.9 | Windows 默认路由走 WiFi | ping 加 `-S 192.168.1.100` 指定源地址 | — （电脑配置） |
| 5 ⚠️ | Ping 无回复（曾以为是 D-Cache 问题） | 全部超时或"无法访问目标主机" | ⚠️ 后来证明 D-Cache 维护**根本不该做**（D2 SRAM 是 non-cacheable），真正原因是 MPU 编码错误（见 #18） | 先加 `SCB_CleanDCache_by_Addr`，**最终全部移除** | `LWIP/Target/ethernetif.c` |
| 6 | 接收任务栈太小 | 可能引发栈溢出 | `INTERFACE_THREAD_STACK_SIZE = 350`（1400B）太小 | 增大到 512 words（2048B） | `LWIP/Target/ethernetif.c` |
| 7 | LWIP_RAM_HEAP_POINTER 与 Rx 缓冲池冲突 | ping 无回复，ARP 数据被覆盖 | Rx_PoolSection 结束于 0x30004A83，堆起点 0x30004000 落在 pool 内部 | 0x30004000 → 0x30005000 | `LWIP/Target/lwipopts.h` |
| 8 | FreeRTOS 堆不足，程序卡死 | PE7 常亮或不亮，MX_LWIP_Init 卡死 | `configTOTAL_HEAP_SIZE=15360`（15KB）不够，4 个任务+队列+信号量超出上限 | 15360 → 32768（32KB） | `Core/Inc/FreeRTOSConfig.h` |
| 9 | 心跳灯与 LwIP 耦合，无法区分崩溃类型 | PE7 常亮/不亮/闪几下熄灭 | 心跳和 LwIP 初始化在同一任务 | 新增独立 heartbeatTask（osPriorityLow） | `Core/Src/freertos.c` |
| 10 | LwIP 堆未定义 MEM_SIZE | PE7 闪几下后熄灭，configASSERT 触发 | LwIP 默认 1600 字节，pbuf 分配失败 | `MEM_SIZE = 16 * 1024` | `LWIP/Target/lwipopts.h` |
| 11 | configASSERT 和 fault handler 静默死循环 | 崩溃后无任何输出，无法定位 | 默认实现是 `while(1)` | configASSERT 打印文件名行号；Fault Handler 从异常栈帧读 PC/LR/CFSR | `FreeRTOSConfig.h`、`stm32h7xx_it.c`、`main.c` |
| 12 ⚠️ | LwIP ARP 非对齐 HardFault（PC=0x0800DAA2） | `[HARDFAULT] CFSR=0x01000000` UNALIGNED | ⚠️ 当时以为是 ARP 头偏移问题，加 `ETH_PAD_SIZE=2` 让 IP 头 4 字节对齐 | 加 `ETH_PAD_SIZE=2`（**后来证明这只是缓解，真正原因见 #15、#18**） | `LWIP/Target/lwipopts.h` |
| 13 | LwIP ARP 非对齐 HardFault（PC=0x0800DAA6，**ETH_PAD_SIZE 没解决**） | 崩溃位置未变，PC 偏移几字节 | LwIP 的 `SMEMCPY` 是 `memcpy`，GCC 14 把它内联成 `str.w`（32-bit store），访问 packed struct 字段（如 ARP `dhwaddr` offset 18）非对齐地址 | 在 `lwipopts.h` 覆盖 `SMEMCPY` 为强制按字节拷贝（`volatile uint8_t *` 防优化） | `LWIP/Target/lwipopts.h` |
| 14 | imprecise BusFault（PC=0x08000AF4） | `[HARDFAULT] CFSR=0x00000400` BFSR.IMPRECISERR，崩溃在 `low_level_output` 内 cache clean 循环 | 对 D2 SRAM（应是 non-cacheable）调 `SCB_CleanDCache_by_Addr` 触发 BusFault。结合 #18 看，本质是 MPU 编码错误把 D2 SRAM 配成了 device-like | 先把 cache 维护操作改为只对 AXI SRAM 调用，**最终在 #18 修复后整段删除** | `LWIP/Target/ethernetif.c` |
| 15 ⚠️ | ETH_PAD_SIZE=2 导致 ARP 静默丢弃 | ping 不通，`rx=29 tx=2`（收到但不回应） | LwIP 的 `ethernet_input` 在 `pbuf_remove_header(ETH_PAD_SIZE)` 跳过 2 字节，但 STM32H7 ETH RX DMA 没有产生这 2 字节填充，跳过后 hdr 错位 → 帧解析失败 → 静默丢弃 | 移除 `ETH_PAD_SIZE`（**但移除后 #12 的非对齐又复发，最终在 #18 修复**） | `LWIP/Target/lwipopts.h` |
| 16 | PHY 链路状态在 100M FD 和 10M HD 之间疯狂抖动 | log 中 `link change: 2->5` 每 100ms 一次，DMA 反复 Stop/Start，rx 计数几乎不增长 | MDIO 读 PHYSCSR 寄存器值不稳定（PHY 内部协商时寄存器抖动）；原 `ethernet_link_thread` 每次状态变化都 `HAL_ETH_Stop_IT+Start_IT`，破坏 RX DMA 描述符 | ① 链路状态去抖动：必须连续 5 次（500ms）相同才采纳 ② ETH 只 `Start_IT` 一次，永不 `Stop_IT` | `LWIP/Target/ethernetif.c` |
| 17 | ip4_input 非对齐 HardFault（PC=0x0800E514） | `[HARDFAULT] CFSR=0x01000000` UNALIGNED，`ip4.c:549` 读 IP dst 地址 | IP header 在 ETH 帧偏移 14 处，`iphdr->dest`（offset 16）= pbuf+30，非 4 字节对齐；`ldr.w` 32-bit 加载触发 fault | 见 #18（最终修复） | — |
| 18 ⭐ | **MPU 编码错误（所有非对齐/Cache fault 的根本原因）** | 之前所有看似独立的问题，实际是同一个根本原因 | CubeMX 默认 MPU 配置 `TEX=001, C=0, B=1, S=0` 在 ARMv7-M 规范中是 "Implementation-defined"，STM32H7 上行为接近 Device memory：禁止非对齐访问，cache 维护可能触发 fault。**正确的 Normal Non-Cacheable 编码必须 B=0** | `IsBufferable: BUFFERABLE → NOT_BUFFERABLE`（B=1→0），并显式 `SCB->CCR &= ~UNALIGN_TRP_Msk` 双重保护 | `Core/Src/main.c` |
| 19 | 移除 D-Cache 维护操作 | #14 已临时绕过 cache fault，#18 修复后 D2 SRAM 真的是 Normal NC，cache 维护本就是 no-op | 既然内存非 cacheable，CPU 访问直接到内存，DMA 看到的就是最新数据 | 删除 `low_level_output` 中所有 cache clean，删除 `HAL_ETH_RxLinkCallback` 中的 cache invalidate | `LWIP/Target/ethernetif.c` |

---

## 六、关键经验总结

### STM32H7 + LwIP 必须知道的"坑王"：MPU 编码错误

**最重要的经验**：CubeMX 默认生成的 MPU 配置 `TEX=001, C=0, B=1, S=0` 是 ARMv7-M 规范中的 **Implementation-defined** 编码，在 STM32H7 上行为接近 Device memory：
- **禁止非对齐 word 访问**（即使 `SCB->CCR.UNALIGN_TRP=0`）
- **cache 维护操作可能触发 imprecise BusFault**

**正确的 Normal Outer/Inner Non-Cacheable 编码必须是 `TEX=001, C=0, B=0`**（`IsBufferable = NOT_BUFFERABLE`）。

这一个错误衍生出了好几种崩溃现象（看似毫无关联）：
- LwIP ARP 处理时 packed struct 字段访问 HardFault
- LwIP IP header 字段（offset 16）访问 HardFault
- D-Cache 维护操作触发 imprecise BusFault
- pbuf payload 任何非对齐 32-bit 访问 HardFault

**调试要点**：如果 STM32H7 上以太网出现"看似随机但 PC 都在 LwIP 内部 ldr/str 指令"的崩溃，**第一个怀疑就是 MPU 的 B 位**。

### STM32H7 + LwIP 其他必须配置

1. **链接脚本**：手动加 `.lwip_sec` 段，强制 ETH DMA 描述符落在 D2 SRAM (0x30000000)
2. **`LWIP_RAM_HEAP_POINTER`**：必须在 Rx 缓冲池之后（通过 `.map` 文件确认 pool 实际结束地址）
3. **`MEM_SIZE`**：显式定义至少 16KB（默认 1600B 不够）
4. **`configTOTAL_HEAP_SIZE`**：≥ 32KB（4 个任务 + 队列 + 信号量）
5. **不需要** `ETH_PAD_SIZE` —— 加了反而破坏接收链路（H7 RX DMA 不产生填充）
6. **不需要** D-Cache 维护 —— D2 SRAM 配为 Normal Non-Cacheable 后，CPU 直写直读
7. **PHY 链路状态必须去抖动** —— MDIO 抖动会导致频繁 Stop_IT/Start_IT 破坏 DMA
8. **`HAL_ETH_Stop_IT` 慎用** —— 任何状态下都不应该停止 ETH DMA，让外设保持运行

### LAN8720 使用注意

1. **没有 SMR 寄存器**：用 BSR（reg 0x01）探测地址 0 和 1，不能用 LAN8742 驱动的 SMR 扫描
2. **没有 RESET 引脚**：上电后等 ≥1000ms 再访问 MDIO
3. **PHY 地址固定为 1**（由 PHYAD[2:0] 引脚电平决定）

### GCC 14 + LwIP 的隐患

- `SMEMCPY` 默认是 `memcpy()`，GCC 内联成 `str.w`/`ldr.w`
- packed struct 的字节对齐字段被当成 word 访问
- **修复**：在 `lwipopts.h` 覆盖 `SMEMCPY` 为 `volatile uint8_t *` 字节循环
- 注：MPU 编码修复（#18）后这个问题"自然消失"，但保留 SMEMCPY override 作为安全网

### 调试方法经验

1. **心跳灯独立任务**（`heartbeatTask`，`osPriorityLow`）：业务卡死时心跳还在，能区分"业务卡死"和"系统崩溃"
2. **`configASSERT` 必须打印文件名行号**：默认静默死循环，改为打印能立刻定位 LwIP/RTOS 内部断言
3. **Fault Handler 必须打印 PC/LR/CFSR**：从异常栈帧（MSP/PSP）读出，用 `arm-none-eabi-addr2line` 定位源码行
4. **`.map` 文件验证内存布局**：每次改内存配置都要看 map，避免堆/缓冲池/段重叠
5. **CFSR 解码必须仔细**：`UFSR` 在 `CFSR[31:16]`、`BFSR` 在 `CFSR[15:8]`、`MMFSR` 在 `CFSR[7:0]`，bit 位置容易看错
6. **运行时统计计数器**：rx/tx 包计数 + 链路状态打印是定位"收得到不回应"vs"根本收不到"的关键
7. **诊断代码不要急着删**：调试代码静默运行不影响功能，等所有问题都修完再清理

### Windows 多网卡 Ping 问题

- 电脑同时连 WiFi 和有线时，ping 走默认路由（通常 WiFi）
- 用 `ping 目标IP -S 直连网卡IP` 强制源地址
- 或临时关闭 WiFi

---

## 七、commit 记录

| commit | 说明 |
|---|---|
| `7e8ebc3` | 项目初始化，CubeMX 工程骨架入库 |
| `a838c9d` | LAN8720 ping-only 固件适配：链接脚本修复、main.c 剥离、PE7 心跳灯 |
| `3495682` | 修复 FatFs/syscall.c 编译错误 |
| `421bcd0` | LAN8720 PHY 地址探测改用 BSR |
| `0225cfe` | LAN8720 上电延时 300ms |
| `88bf2a0` | ⚠️ Tx D-Cache Clean（后来证明不需要，#19 删除）+ EthIf 任务栈 512 |
| `06e5c99` | LWIP_RAM_HEAP_POINTER 与 Rx 缓冲池地址冲突修复 |
| `0604050` | FreeRTOS 堆 32KB + 上电延时 500ms |
| `b07d2d8` | 心跳灯独立任务（heartbeatTask） |
| `6cf010b` | LwIP `MEM_SIZE = 16KB` |
| `a390aa8` | configASSERT/Error_Handler 改为打印 |
| `d5c8e19` | 4 个 Fault Handler 打印 PC/LR/CFSR；上电延时 1000ms |
| `15dcadf` | ⚠️ `ETH_PAD_SIZE=2`（后来证明会破坏接收，`8019de8` 删除） |
| `2cbd9cc` | MPU Region 0 改为 NOT_SHAREABLE（缓解但未根治） |
| `8a3db19` | SMEMCPY 强制字节拷贝（`#13` 修复） |
| `f8b5989` | 移除 D-Cache 维护操作（#14、#19） |
| `9e60f92` | 加入 ETH Rx/Tx 计数器和链路状态打印 |
| `2bf8479` | PHY 链路去抖动 + 永不 Stop_IT（#16） |
| `8019de8` | **移除 `ETH_PAD_SIZE`**（#15 修复，让接收链路恢复） |
| `bdf031b` | ⭐ **MPU TEX/C/B 编码修正：B=1→B=0**（#18，所有 fault 的根本原因） |

---

## 八、当前状态与下一步

**当前状态**：等待烧录 commit `bdf031b` 验证 ping 通

**预期串口输出**：
```
[ETH] LAN8720 addr=1, link=probed-OK
[ETH] PHY link state: 1 或 6（上电时未协商完）
[ETH] raw link: ... -> 2  ← 协商完成
[ETH] LINK UP confirmed: speed=16384 duplex=8192
[ETH] stat rx=N tx=N raw=2 stable=2 up=1
```

**预期行为**：
- 无任何 `[HARDFAULT]` 输出
- PE7 持续 1Hz 闪烁
- ping 期间 rx 和 tx 同步增长

**预期 Ping 结果**：
```powershell
ping 192.168.1.88 -S 192.168.1.100
# 来自 192.168.1.88 的回复: 字节=32 时间<1ms TTL=255
```

**Ping 通后下一步（按 CLAUDE.md 路线）**：
- 阶段 2：恢复 FDCAN1 初始化，验证 CAN 收发
- 阶段 3：恢复 QSPI，读 W25Q128 JEDEC ID
- 阶段 4：恢复 SDMMC + FatFs，挂载 TF 卡
- 阶段 5：以太网 + CAN 联调，Web 显示原始 CAN 帧

---

## 九、本轮调试的总反思

1. **CubeMX 默认配置不能信** —— MPU、LwIP heap 指针、PHY 选型都有错
2. **修一个问题不要急于下结论** —— `ETH_PAD_SIZE=2` 看似修了 ARP HardFault，实际只是改变了崩溃位置；真正根本原因是 MPU 编码
3. **现象相似的 fault 可能源自同一原因** —— ARP/IP/cache 维护三类崩溃看似无关，根本都是 MPU `B` 位写错
4. **诊断代码的价值远超修复代码** —— 没有 fault handler 的 PC 打印、没有 rx/tx 计数器、没有链路抖动打印，根本不可能定位到 #16、#18 这种深层问题
5. **从硬件 ARM 规范文档查表是最可靠的** —— ARMv7-M ARM Table B3-13（TEX/C/B 编码）一查就发现 CubeMX 给的是 Implementation-defined 编码
