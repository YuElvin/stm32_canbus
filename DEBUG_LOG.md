# 调试日志 — STM32H750 + LAN8720 以太网 Ping 验证

> 最后更新：2026-06-04 | 项目阶段：阶段 4（SDMMC 调试中）| 覆盖 commit `7e8ebc3` ~ `b48adec`

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
| `2c5de94` | ETH DMA 永远启动 + 诊断打印 + RX 描述符 4→8 |
| `4bc2703` | ARP 回复链路排查诊断（tcpip_input 返回值、TX 错误码） |
| `2cea3e0` | EthIf 栈 512→1024 words + 启用栈溢出检测 |
| `2e318f0` | PHY BSR 探测全地址扫描 + 重试 + 延时 2000ms |
| `65b2934` | EthIf 栈→2048 words + 栈溢出钩子打印任务名 |
| `9c8742d` | EthLink 栈 1024→2048 words |
| `50c0f0d` | ETH 中断优先级 5→6 + LLO 诊断 |
| `a556216` | HAL_ETH_Init 后清除 PacketAddress[] |
| `86b5c07` | LLO 打印 payload 前 6 字节确认以太网头 |
| `31154a0` | tcpip_thread 邮箱管道诊断计数器（q/P/L） |
| `5cf5d8c` | etharp_input 决策路径计数器（reply_ok/not_for_us/uc/bh） |
| `96be154` | etharp_input 打印 ARP tgt IP vs netif IP 对比（前 8 包） |
| `2f79b76` | 清理: 移除第三轮诊断代码（tcpip/etharp 计数器和 IP 打印） |
| `dafc699` | 修复: gratuitous ARP 广播 + netif_set_up 移到 Start_IT 之后 |
| `a764c97` | 修复: TCPIP_THREAD_STACKSIZE 1024→2048（解决 Reset 后 ping 崩溃） |
| `bebebe9` | 修复: low_level_init 始终用 100M FD 初始化 MAC（解决 rx=0） |

---

## 八、第二轮调试（commit `2c5de94` ~ `86b5c07`）

> 第一轮修复了 MPU/Cache/DMA 等底层问题后，进入实际 ping 测试阶段。

### 问题 20：冷启动 PHY BSR 探测失败（addr=31）

**现象**：上电后 `[ETH] LAN8720 addr=31, link=scan-fallback`，链路停留在 10M 半双工。

**原因**：LAN8720 模块无 RESET 引脚，冷启动时 MDIO 响应慢。BSR 探测只扫地址 0 和 1，均返回 0x0000，回落到 SMR 扫描得到错误地址 31。

**修复**：BSR 探测扩展到 0-31 全地址 + 5 次重试（每次间隔 500ms）+ MDIO 延时 1000→2000ms。

---

### 问题 21：EthIf 任务栈溢出（queue.c:1586 断言）

**现象**：收到 ARP 请求后触发 `[ASSERT] queue.c:1586`。

**原因**：EthIf 任务栈 512 words（2048B），加上 sprintf 诊断代码（rmsg[96]）和 HAL_ETH_ReadData 调用链，栈溢出破坏了 FreeRTOS 队列结构。

**修复**：INTERFACE_THREAD_STACK_SIZE 512→2048 words。

---

### 问题 22：EthLink 任务栈溢出（cmsis_os2.c:2924 断言）

**现象**：`[STACK_OVF] Task: EthLink`。

**原因**：ethernet_link_thread 栈 1024 words（4096B），内有 char dbg[112] + char m[80] + sprintf，总用量超过 4096B。

**修复**：lwip.c 中 INTERFACE_THREAD_STACK_SIZE 1024→2048 words。

---

### 问题 23：ETH 中断优先级边界问题（queue.c:894 断言）

**现象**：ping 操作后触发 `[ASSERT] queue.c:894`（`pvItemToQueue == NULL`）。

**原因**：ETH_IRQn 优先级 = 5，等于 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`（5）。在某些 FreeRTOS port 中，等于临界值的中断调用 `osSemaphoreRelease` 会触发断言。

**修复**：HAL_NVIC_SetPriority(ETH_IRQn, 5→6, 0)。

---

### 问题 24：HAL_ETH_Init 未清除 PacketAddress（ARP 回复发送失败）

**现象**：ARP 请求收到（rx>0），但 ARP 回复不发送（tx=1 只有初始 ARP）。

**原因**：LwIP 初始化时 `etharp_request()` 调用 `HAL_ETH_Transmit_IT()` 设置了 `PacketAddress[0]=非空`。随后 `HAL_ETH_Init()` 重置 TX 描述符但**不清除 PacketAddress[]**。后续 ARP 回复调用 `HAL_ETH_Transmit_IT()` 时 `PacketAddress[0]!=NULL` → 误判描述符忙 → 返回失败。

**修复**：`HAL_ETH_Init()` 后手动 `memset(heth.TxDescList.PacketAddress, 0, ...)`。

---

### 问题 25：ARP 回复目标 MAC 为广播（已解决 — Windows ARP 缓存）

**现象**：`[LLO#2]` 确认 `low_level_output()` 被调用，但 `[TX]` 显示目标 MAC 为广播 `FF:FF:FF:FF:FF:FF` 而非 PC 的 `00:E2:69:7D:EC:40`。

**诊断过程**：

1. **tcpip_thread 邮箱管道诊断**（commit `31154a0`）：在 `tcpip.c` 增加三个计数器 `tcpip_inpkt_queued`/`_lost`/`_proc`。结果 `q=20/20 L=0`，管道完全正常，排除 tcpip_thread 阻塞。

2. **etharp 决策路径诊断**（commit `5cf5d8c`）：增加 `etharp_reply_ok`/`_not_for_us`/`_unconfig`/`_bad_hdr` 四个计数器。结果 `ARP ok=0 no=5 uc=0 bh=0` — etharp 判定所有 ARP 请求都不是发给本机的。

3. **IP 地址对比诊断**（commit `96be154`）：打印 `tgt=X.X.X.X nif=X.X.X.X f_us=X`。发现 PC 发出的 ARP 目标是 `192.168.1.100`（ACD 冲突检测），**从未发出对 `192.168.1.88` 的 ARP 请求**。

**根本原因**：Windows ARP 缓存。之前 flash 烧录 boot 时 PC 缓存了 `.88`→STM32 MAC 的映射。Reset 后 PC 拿着过期条目直接发 IP 包（不带 ARP 请求），收不到回复。ACD 探测（`tgt=192.168.1.100`）不会触发 STM32 回复。

**验证**：PC 执行 `arp -d 192.168.1.88` 清除缓存后再 ping，正常：
```
[ARP#1] tgt=192.168.1.88 nif=192.168.1.88 f_us=1 from=0  ← 正确匹配
[LLO#2] hdr=00:E2:69:7D:EC:40                              ← ARP Reply 单播
[TX] 00:80:E1:00:00:00 -> 00:E2:69:7D:EC:40 type=0806      ← 发送成功
ARP ok=1 no=0 uc=0 bh=0
```
Ping 4 包全通（最短 2ms，最长 38ms）。

**修复**：启动时发送 gratuitous ARP（`etharp_gratuitous(netif)`）主动通告本机 IP/MAC，使 PC 刷新 ARP 缓存。配合后续修复（#28、#29），Reset 后直接 ping 即可通，无需 `arp -d`。

---

### 问题 26：PHY 链路持续抖动

**现象**：`[ETH] raw link: 2 -> 5 -> 1 -> 6` 持续循环（100M FD ↔ 10M HD ↔ LINK_DOWN ↔ 自动协商）。

**原因**：LAN8720 模块 / 网线 / RMII 接线硬件层面问题，MDIO 寄存器值频繁跳变。

**排查方向**：
1. 换网线（最可能）
2. 示波器检查 PA1 REF_CLK（50MHz 晶振）
3. 检查 9 根 RMII 杜邦线接触

**影响**：debounce 机制（500ms 稳定才采纳）使 `debounced_link` 保持 2（100M FD），不影响功能。raw link 变化只有打印，不再触发 Stop_IT/Start_IT。频繁打印会淹没其他输出。

---

### 问题 27：`netif_set_up` 在 `HAL_ETH_Start_IT` 之前触发 LwIP ARP（已修复）

**现象**：`[LLO#1]` 调用时 `gState=16`（BUSY_TX），DMA 未启动，首包 ARP 丢失。`HAL_ETH_Start_IT` 之后才被调用。

**原因**：`low_level_init()` 中 `netif_set_up(netif)` 触发 LwIP 立即发 ARP，但 `HAL_ETH_Start_IT()` 在其后执行。

**修复**（commit `dafc699`）：将 `netif_set_up`/`netif_set_link_up` 移到 `HAL_ETH_Start_IT` 之后执行。同时加入 `etharp_gratuitous(netif)` 广播通告本机 MAC。

---

### 问题 28：`TX ERR=2` 偶发

**现象**：ping 通道正常但偶尔出现 `[TX] ERR=2 gS=64`（HAL_ETH_ERROR_BUSY），随后自动恢复。

**原因**：两个 TX 包间隔太近，第二个包到达时上一个描述符尚未释放。`low_level_output` 的 `do-while` 循环会重试，自动恢复。

**状态**：已知，每次 ping 出现一次，不影响丢包，无需修复。

---

## 九、第四轮调试（commit `dafc699` ~ `bebebe9`）

> 第三轮确认了 ARP 响应链路正常。本阶段解决 Reset 后直接 ping 的稳定性和崩溃问题。

### 问题 29：tcpip_thread 栈溢出 → queue.c:894 断言崩溃（已修复）

**现象**：Reset 后 PC 用缓存 MAC 直接发 ICMP（无 ARP），STM32 崩溃 `[ASSERT] queue.c:894`。

**根因分析**：
```
[RX#1] type=0800               ← PC 直接发 ICMP（无 ARP 请求）
[LLO#3] hdr=FF:FF:FF:FF:FF:FF ← LwIP 为发回包做 etharp_query
[ASSERT] queue.c:894           ← 崩溃
```

PC 发 ARP 请求路径：`etharp_input → etharp_raw`（栈浅）。PC 直接发 ICMP 路径：`ip4_input → icmp_echo_reply → ip_output → etharp_output(miss) → etharp_query(排队 pbuf + 发 ARP)`，调用链深得多。`TCPIP_THREAD_STACKSIZE=1024`（4KB）耗尽，写坏 FreeRTOS 队列结构。

**修复**（commit `a764c97`）：`TCPIP_THREAD_STACKSIZE 1024→2048`（8KB），与 EthIf/EthLink 一致。

---

### 问题 30：MAC 速率误配导致 rx=0 全程收不到包（已修复）

**现象**：上电后 rx=0 持续数十秒，ping 全部"无法访问目标主机"，但下一个 Reset 后又正常。

**根因分析**：

| 启动 | init 时刻 PHY 快照 | MAC 配置 | 结果 |
|---|---|---|---|
| 失败 | `link=5`（10M HD） | 10M HD | MAC/PHY 速率不匹配，DMA 收不到任何包 |
| 成功 | `link=6`（auto-neg） | auto-neg 过程 | 后续 EthLink 纠正为 100M FD |

PHY 自协商期间 MDIO 寄存器值在 `5(10M HD) → 6(auto-neg) → 1(down) → 2(100M FD)` 之间跳变。`low_level_init` 取瞬时快照决定 MAC 速率，抓到非 100M FD 状态即配错。

**修复**（commit `bebebe9`）：`low_level_init` 不再根据 PHY 快照配置 MAC，**始终初始化为 100M Full Duplex**。EthLink 线程在 link 稳定后（debounce 500ms）重新配置正确速率。

---

### 修复效果验证

修复后测试：上电直接 ping（无 `arp -d`）→ 4/4 通，RTT <1ms。Reset 后直接 ping → 4/4 通。全程无断言/崩溃。

Reset 场景（PC 用缓存 MAC 直接发 ICMP）：
```
[RX#1] type=0800              ← PC 直接发 ICMP
[LLO#3] hdr=FF:FF:FF:FF:FF:FF ← STM32 ARP 查询 PC MAC（tcpip 8KB 栈不崩）
[RX#2] type=0806              ← PC ARP 回复
[LLO#4] hdr=00:E2:69:7D:EC:40 ← ICMP 回复 → ping 通
```

---

## 十、当前状态与下一步

**当前状态**：阶段 1（以太网 Ping 验证）**已完成，通过**

**已完成**：
- [x] Ping 通：上电/Reset 直接 ping 4/4 全通，无需 arp -d
- [x] gratuitous ARP 广播通告（解决 PC ARP 缓存过期）
- [x] tcpip_thread 栈增加至 8KB（解决 ICMP 直接发场景崩溃）
- [x] MAC 始终 100M FD 初始化（解决 PHY 快照速率误配导致 rx=0）
- [x] PHY BSR 探测全地址扫描 + 重试
- [x] 栈溢出修复（EthIf 2048w + EthLink 2048w + tcpip 2048w）
- [x] 栈溢出钩子诊断
- [x] ETH 中断优先级 5→6
- [x] PacketAddress 清除
- [x] RX 描述符 4→8
- [x] ETH DMA 永远启动
- [x] netif_set_up 移到 Start_IT 之后

**已知残留问题**：
- [ ] PHY 链路抖动（硬件层面，debounce 已防御）
- [ ] TX ERR=2 偶发（描述符忙，自动恢复，不影响丢包）

**下一步**：进入阶段 2（FDCAN 数据采集）

---

## 十、调试经验总结

1. **CubeMX 默认配置不能信** —— MPU、LwIP heap 指针、PHY 选型都有错
2. **修一个问题不要急于下结论** —— `ETH_PAD_SIZE=2` 看似修了 ARP HardFault，实际只是改变了崩溃位置；真正根本原因是 MPU 编码
3. **现象相似的 fault 可能源自同一原因** —— ARP/IP/cache 维护三类崩溃看似无关，根本都是 MPU `B` 位写错
4. **诊断代码的价值远超修复代码** —— 没有 fault handler 的 PC 打印、没有 rx/tx 计数器、没有链路抖动打印，根本不可能定位到 #16、#18 这种深层问题
5. **从硬件 ARM 规范文档查表是最可靠的** —— ARMv7-M ARM Table B3-13（TEX/C/B 编码）一查就发现 CubeMX 给的是 Implementation-defined 编码

### 第二轮调试经验

6. **栈溢出是 FreeRTOS 最常见的崩溃原因** —— 表现为随机的 queue.c assert 或 cmsis_os2.c assert，根本原因是栈写坏了 TCB 或队列结构。`configCHECK_FOR_STACK_OVERFLOW=2` + 自定义 `vApplicationStackOverflowHook` 是必备诊断
7. **FreeRTOS 中断优先级边界问题** —— `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5` 时，优先级 5 的中断调用 FreeRTOS API 可能在某些 port 上触发断言。保险起见用 6 或更大值
8. **HAL_ETH_Init 不清除 PacketAddress** —— CubeMX HAL 的 `ETH_DMATxDescListInit()` 只重置描述符，不清 `TxDescList.PacketAddress[]`。如果初始化前有包被 `HAL_ETH_Transmit_IT()` 排队，PacketAddress 残留会导致后续所有发送返回 BUSY
9. **LAN8720 冷启动 MDIO 稳定时间长** —— 某些模块需要 2-3 秒 MDIO 才稳定，仅扫地址 0/1 不够，需要全地址扫描 + 重试
10. **诊断代码会增加栈消耗** —— 每个 `sprintf` + 局部缓冲区消耗 100-200B 栈。加诊断后必须同步增大任务栈，否则诊断本身就导致溢出

---

## 十一、第三轮调试（commit `31154a0` ~ `96be154`）

> 第二轮的诊断代码已确认 tcpip_thread 管道和 etharp 都正常工作，但 ping 仍不通。
> 引入分层诊断逐级定位：tcpip 邮箱 → etharp 决策 → IP 地址对比。

### 诊断方法

采用三级分层诊断，从上到下收敛问题范围：

| 层级 | 文件 | 计数器 | 作用 |
|---|---|---|---|
| L1 | `tcpip.c` | `queued` / `lost` / `proc` | 确认包从 EthIf 到达 tcpip_thread 且被消费 |
| L2 | `etharp.c` | `reply_ok` / `not_for_us` / `uc` / `bh` | 确认 etharp_input 判断结果 |
| L3 | `etharp.c` | ARP tgt IP vs netif IP 直接打印 | 对比两个 IP 的具体值 |

### 定位结果

```
tcpip:  q=20/20 L=0        ← L1 完全正常（收到20包，处理20包，0丢失）
etharp: ARP ok=0 no=5      ← L2 ARP 请求不是发给本机的
ARP IP: tgt=192.168.1.100 nif=192.168.1.88  ← L3 PC 没在ping .88，而是在做ACD探测
```

**根因**：PC 侧 Windows ARP 缓存。开发调试每次 Reset 后需 `arp -d 192.168.1.88`。

### 验证

```bash
arp -d 192.168.1.88
ping 192.168.1.88 -S 192.168.1.100
# 4/4 包通，RTT 最小 2ms
```

### 第三轮调试经验

11. **ARP 不通先检查 PC 侧缓存** —— Windows ARP 表在硬件地址变化后不会自动更新。Reset 后 MCU MAC 重新初始化（虽然不变，但 PC 可能因 DHCP/APIPA 等过程把条目标记为 stale）
12. **分层诊断比一次性加满打印更高效** —— 先确认管道，再确认协议层，最后对比数值。每层收敛范围后下一层更有针对性
13. **诊断计数器优于实时打印** —— volatile 计数器不阻塞、不会溢出、不会被淹没。只在周期性 stat 行中汇总打印，零性能影响

### 第四轮调试经验

14. **FreeRTOS + LwIP 栈溢出是崩溃王** —— EthIf/EthLink/tcpip_thread 三个任务均需 ≥8KB 栈。`configCHECK_FOR_STACK_OVERFLOW=2` + `vApplicationStackOverflowHook` 是必备诊断。栈溢出表现随机（queue.c assert / cmsis_os2 assert），根本原因不是队列本身而是 TCB 被写坏
15. **PHY 自协商期间不要信 MDIO 快照** —— LAN8720 的自协商过程中寄存器值频繁跳变（5→6→1→2）。初始化 MAC 速率应始终用目标值（100M FD），由独立 link 线程在稳定后纠正，不能取瞬时值
16. **gratuitous ARP 解决 PC ARP 缓存过期** —— 重启后主动广播通告 MAC，避免依赖 PC 侧 `arp -d`。`etharp_gratuitous(netif)` 发 ARP Request（RFC 5227 格式），PC 收到后刷新缓存

---

## 十二、阶段 3 — QSPI W25Q128 验证（commit `64ab1ff` ~ `e31b734`）

> 目标：验证板载 W25Q128（16MB QSPI Flash）读写功能，为后续配置备份和 XIP 做准备。

### 硬件

| 部件 | 说明 |
|---|---|
| Flash | W25Q128（板载），QSPI Bank1 |
| 引脚 | PB2(CLK) PB10(NCS) PD11(IO0) PD12(IO1) PE2(IO2) PD13(IO3) |
| QSPI 时钟 | HCLK/6 ≈ 33MHz（Prescaler=5，HCLK=200MHz） |

### 验证流程

1. 读 JEDEC ID（期望 `EF 40 18`）
2. 擦除测试扇区（地址 `0xFF0000`，最后一块 4KB 扇区）
3. 读回校验（应全 `0xFF`）
4. 页编程 256 字节（递增 pattern `0x00~0xFF`）
5. 读回校验（数据一致）
6. 清理：擦除测试扇区

### 问题 1：扇区擦除超时（HAL_QSPI_AutoPolling）

**现象**：JEDEC ID 读取正确（`EF 40 18`），但 `W25QXX_EraseSector()` 在 `HAL_QSPI_AutoPolling()` 处超时失败。

**串口输出**：
```
[QSPI] JEDEC ID: EF 40 18
[QSPI] JEDEC ID OK
[QSPI] Erasing sector at 0xFF0000 ... FAIL
```

**根因**：`W25QXX_WaitBusy()` 中先调 `HAL_QSPI_Command()` 发送 Read Status Register 命令，再调 `HAL_QSPI_AutoPolling()` 用同一命令轮询。但 `HAL_QSPI_AutoPolling()` 内部会自己发送命令，重复调用导致 QSPI 外设状态机冲突（Command 被发两次，第二次 AutoPolling 的首字节响应来自第一次 Command 的残留）。

**修复**：去掉 `HAL_QSPI_AutoPolling` 前的 `HAL_QSPI_Command`，只保留 AutoPolling 本身。

**结果**：仍然失败，输出变为 `BUSY_FAIL`。

### 问题 2：AutoPolling 持续超时

**现象**：修复问题 1 后，擦除仍超时，串口输出 `BUSY_FAIL`。

**根因分析**：`HAL_QSPI_AutoPolling()` 在 STM32H7 HAL 实现中对 QSPI 状态机有隐式依赖——需要前一次操作完全结束（TransferComplete 标志）。当上一步 `HAL_QSPI_Command()` 发送 Sector Erase 命令后，QSPI 外设可能仍处于 Command 发送完成但未清理状态，AutoPolling 启动时状态机冲突。

**最终修复**：完全弃用 `HAL_QSPI_AutoPolling()`，改用手动轮询 `W25QXX_ReadStatusReg1()` + `HAL_GetTick()` 超时检测。

```c
static HAL_StatusTypeDef W25QXX_WaitBusy(uint32_t timeout_ms)
{
  uint32_t tick = HAL_GetTick();
  uint8_t sr;
  do {
    if (W25QXX_ReadStatusReg1(&sr) != HAL_OK)
      return HAL_ERROR;
    if ((sr & W25Q_SR_BUSY) == 0)
      return HAL_OK;
  } while ((HAL_GetTick() - tick) < timeout_ms);
  return HAL_TIMEOUT;
}
```

### 验证结果（通过）

```
[QSPI] W25Q128 Verify Start
[QSPI] JEDEC ID: EF 40 18
[QSPI] JEDEC ID OK
[QSPI] Erasing sector at 0xFF0000 ... SR_before=00 SR_after_WREN=02 OK
[QSPI] Erase verify OK (all 0xFF)
[QSPI] Programming 256 bytes ... OK
[QSPI] Program verify OK (256 bytes match)
[QSPI] W25Q128 Verify Done
```

| 步骤 | 结果 |
|---|---|
| JEDEC ID | `EF 40 18`（W25Q128） |
| WriteEnable | SR `00→02`，WEL bit 正确置位 |
| Sector Erase | 成功，BUSY 等待 ~50ms 清除 |
| Erase Verify | 全 `0xFF` |
| Page Program | 256 字节写入成功 |
| Program Verify | 数据完全匹配 |

### 经验总结

17. **HAL_QSPI_AutoPolling 对状态机有隐式依赖** —— 在 STM32H7 上，前一次 Command/Receive 操作完成后外设状态可能未完全清理，直接接 AutoPolling 会冲突。手动轮询 SR1 更可靠
18. **JEDEC ID 读取是最简单的 QSPI 验证** —— 只需 Instruction + Receive，不涉及 WriteEnable/BUSY/地址，适合第一步验证 QSPI 硬件连接和时钟配置是否正确
19. **测试地址选最后扇区（0xFF0000）** —— 避免覆盖可能存在的配置数据，验证后自动擦除清理

---

## 十三、阶段 4 — SDMMC + FatFs TF 卡验证（commit `3eff125` ~ `3584e4c`）

> 目标：验证 SDMMC1 4 位模式 + FatFs 挂载 TF 卡、写文件、读回校验。

### 硬件

| 部件 | 说明 |
|---|---|
| TF 卡座 | 板载，SDMMC1 4 位模式 |
| 引脚 | PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CK) PD2(CMD) |
| SDMMC 时钟 | PLL2 输出，ClockDiv=0（HAL 初始化时自动降到 400kHz 识别，再提速） |

### 问题 1：Flash 溢出 142KB（cc936.c）

**现象**：启用 FatFs 后链接报 `.rodata` 溢出 FLASH region 142500 字节。

**根因**：CubeMX 默认 `_CODE_PAGE=936`（简体中文 GBK），对应 `cc936.c` 包含 ~170KB 的 Unicode-OEM 双向转换表，远超 128KB Flash 容量。

**修复**：`_CODE_PAGE` 改为 437（U.S. English），`cc936.c` 替换为 `ccsbcs.c`（单字节代码页合集，仅几 KB）。

| 文件 | 改动 |
|---|---|
| `FATFS/Target/ffconf.h` | `_CODE_PAGE` 936→437 |
| `Makefile` | `cc936.c` → `ccsbcs.c` |

**限制**：代码页 437 仅支持 ASCII 文件名，不支持中文。后续需要中文文件名时需切回 936 并配合 `-Os` 或 W25Q128 XIP 方案。

**结果**：编译通过，Flash 占用 99KB / 128KB。

### 问题 2：f_mount error 3（FR_NOT_READY）— 重复初始化

**现象**：SD 卡硬件识别正常（`HAL_SD_GetCardInfo` 返回 type=1, BlockNbr=30560256），但 `f_mount()` 返回 3（FR_NOT_READY）。

**串口输出**：
```
[SD] Card detect: PRESENT
[SD] Card type=1 BlockNbr=30560256 BlockSize=512
[SD] FAIL: f_mount error 3
[SD] HAL SD state=1 error=0
```

**根因**：`main.c` 中先调了 `MX_SDMMC1_SD_Init()` → `HAL_SD_Init()` 完成 SD 卡初始化，之后 `f_mount()` 内部的 `SD_initialize()` 又调 `BSP_SD_Init()` → `HAL_SD_Init()` 重复初始化。HAL 状态机在第二次初始化时冲突，`BSP_SD_GetCardState()` 返回非 `SD_TRANSFER_OK` → `Stat = STA_NOINIT` → FatFs 报 FR_NOT_READY。

**修复**：注释掉 `main.c` 中的 `MX_SDMMC1_SD_Init()`，让 FatFs 的 `SD_initialize()` 通过 `BSP_SD_Init()` 统一负责 SDMMC GPIO/时钟/卡初始化。

**结果**：`f_mount` 返回 FR_OK。

### 问题 3：HAL_SD_GetCardInfo 返回全零 — 调用顺序错误

**现象**：移除 `MX_SDMMC1_SD_Init()` 后，`HAL_SD_GetCardInfo()` 在 `f_mount()` 之前调用返回全零（type=0, BlockNbr=0）。

**串口输出**：
```
[SD] Card type=0 BlockNbr=0 BlockSize=0
[SD] FAIL: f_mount error 3
[SD] HAL SD state=0 error=0
```

**根因**：`HAL_SD_GetCardInfo()` 需要 SD 卡已初始化（`HAL_SD_STATE_READY`）。移除 `MX_SDMMC1_SD_Init()` 后，SD 卡在 `f_mount()` → `BSP_SD_Init()` 之前处于 `HAL_SD_STATE_RESET` 状态。

**修复**：将 `HAL_SD_GetCardInfo()` 移到 `f_mount()` 成功之后调用。

**结果**：卡信息正确返回。

### 验证结果（通过）

```
[SD] TF Card Verify Start
[SD] Mount OK
[SD] Card type=1 BlockNbr=30560256 BlockSize=512
[SD] Card: 14902 MB total, 14800 MB free
[SD] Write OK (34 bytes)
[SD] Read verify OK
[SD] TF Card Verify Done
```

| 步骤 | 结果 |
|---|---|
| f_mount | FR_OK |
| f_getfree | 14.6GB 总容量，~14.4GB 可用 |
| f_open + f_write | 34 字节写入成功 |
| f_read + strcmp | 数据完全匹配 |
| f_unlink | 测试文件清理成功 |

### 经验总结

20. **FatFs 与 CubeMX 外设初始化不要重复** —— FatFs 的 `SD_initialize()` 会调 `BSP_SD_Init()` 完成完整初始化（包括 HAL_MspInit GPIO/时钟配置）。main.c 中再调 `MX_SDMMC1_SD_Init()` 会导致 `HAL_SD_Init()` 被调两次，HAL 状态机冲突。正确做法是只保留 FatFs 的初始化路径
21. **FatFs API 调用顺序依赖初始化** —— `HAL_SD_GetCardInfo()` 等 HAL API 需要 SD 卡处于 `READY` 状态，必须在 `f_mount()`（触发 `BSP_SD_Init()`）之后调用
22. **cc936.c（中文代码页）是 Flash 杀手** —— 170KB 转换表远超 128KB Flash。验证阶段用代码页 437（几 KB）足够，中文文件名支持需要 XIP 或 `-Os` 优化方案
23. **SD 卡验证测试文件名用纯 ASCII** —— 代码页 437 不支持中文，测试文件名和内容都用 ASCII 避免编码问题

---

## 十四、阶段 4 续 — SDMMC 调试第二轮（commit `59551ac` ~ `b48adec`）

> 目标：修复 SD 验证在实际硬件上的挂载失败问题，加入物理卡检测。

### 问题 24：SD_Verify() 在 FreeRTOS 调度器启动前调用 → FR_NOT_READY

**现象**：串口 `[SD] FAIL: f_mount error 3`，`HAL SD state=0`（HAL_SD_STATE_RESET）。

**根因**：`main.c` 中 `SD_Verify()` 在 `osKernelStart()` 之前调用。`sd_diskio.c` 的 `SD_initialize()` 有守卫 `osKernelGetState() == osKernelRunning`，内核未运行时跳过 `BSP_SD_Init()` 和 RTOS 消息队列创建，返回 `STA_NOINIT` → FatFs 报 FR_NOT_READY。

对比 QSPI 测试（`W25QXX_Verify()`）可以正常工作的原因：它直接调 HAL API，不依赖 RTOS。

**修复**（`59551ac`）：
- `main.c`：移除 `W25QXX_Verify()` / `SD_Verify()` 直接调用
- `freertos.c`：新增 `initTestTask`（Normal 优先级，4KB 栈），在调度器启动后执行两个验证函数
- `sd_verify.c`：修复 `printf %lu` 格式警告，加 `(unsigned long)` 转换

---

### 问题 25：PA8 卡检测总是读 HIGH，无法检测插卡

**现象**：PA8 配置为输入+内部上拉，预期卡插入时短接到 GND 读 LOW。但无论是否插卡，PA8 始终读 HIGH → `BSP_SD_IsDetected()` 返回 `SD_NOT_PRESENT` → `BSP_SD_Init()` 直接返回不走 `HAL_SD_Init()`。

**根因**：该开发板的 TF 卡检测脚物理连接与假设不一致（可能不是卡插入拉低，或 PA8 未实际接到检测开关）。

**修复**（`2a63fb6`）：
- `BSP_SD_IsDetected()` 临时改为始终返回 `SD_PRESENT`，绕过卡检测先验证 SDMMC 硬件
- `BSP_SD_Init()` 添加串口诊断打印 PA8 实际电平

**状态**：PA8 卡检测逻辑待后续用万用表实测确定正确极性和连接关系。

---

### 问题 26：HAL_SD_ERROR_UNSUPPORTED_FEATURE（0x80000000）

**现象**：绕过卡检测后，`HAL_SD_Init()` 返回 `HAL_ERROR`，`hsd1.State=READY` 但 `hsd1.ErrorCode=0x80000000` → `BSP_SD_Init()` 返回 `MSD_ERROR` → `f_mount` 报 FR_NOT_READY。耗时约 16 秒（HAL 内部超时重试）。

**根因**：STM32H7 HAL 对某些 SDHC/SDXC 卡在初始化序列中标记 `HAL_SD_ERROR_UNSUPPORTED_FEATURE`（如 1.8V 电压切换不支持），但卡实际已进入 `READY` 状态。`BSP_SD_Init()` 只看返回值不检查 State，直接报错。

**修复**（`effc5e7`）：
- `BSP_SD_Init()` 中 `HAL_SD_Init()` 返回非 OK 时，检查 `hsd1.State == HAL_SD_STATE_READY`，若是则清除 `ErrorCode` 并设 `sd_state = MSD_OK` 继续。

---

### 问题 27：ConfigWideBusOperation 4 位模式 CRC 失败

**现象**：修复 #26 后，`HAL_SD_Init()` 通过（state=READY），但 `HAL_SD_ConfigWideBusOperation(4B)` 返回 `HAL_ERROR`，`ErrorCode=0x01`（`HAL_SD_ERROR_CMD_CRC_FAIL`）。耗时缩短到约 2 秒。

**根因**：4 位总线切换时 CMD 线或 D3 线（PC11）通信失败。可能原因：
- PC11（D3）接触不良 / 虚焊
- PD2（CMD）信号质量差
- 该 TF 卡对高速 4 位模式兼容性问题

**修复**（`b48adec`）：
- 4 位模式失败时打印错误码，自动降级尝试 `SDMMC_BUS_WIDE_1B` 1 位模式
- 1 位模式性能较低但功能完整，先确保基本读写可用

**状态**：1 位模式是否通过待实测确认。4 位模式需排查硬件接线。

---

### 经验总结

24. **SD 卡初始化必须在 FreeRTOS 调度器启动后执行** —— `sd_diskio.c` 使用 RTOS 消息队列处理 DMA 完成通知，`SD_initialize()` 内部有内核运行检查。所有 FatFs 操作必须放在任务上下文中。

25. **物理卡检测需要实测确认极性和连接** —— 开发板的卡检测引脚不一定是"插卡拉低"。应先用万用表测 PA8 在插卡/不插卡时的电平，再确定正确的检出逻辑。如无可靠检测口，可暂时绕过。

26. **STM32H7 HAL 的 SD_ERROR_UNSUPPORTED_FEATURE 是可恢复的** —— 部分 SD 卡初始化时 HAL 会标记此错误但卡已就绪（State=READY）。BSP 层应检查 State 而非仅依赖返回值。

27. **4 位总线切换失败应降级为 1 位** —— CMD 或 D3 信号质量差会导致 CRC 失败。1 位模式（仅 D0）对布线要求低得多，作为软降级方案可保证基本功能。排查时优先检查 D3（PC11）和 CMD（PD2）的焊接/接线。

---

## 十五、阶段 4 续 — SDMMC 调试第三轮（commit `200bc2c` ~ `43d71ae`）

> 目标：参考 ST H743 DEMO 示例代码修复 SD 卡挂载失败问题。

### 参考代码

ST 官方 `H743-DEMO2/Applications/FatFs/FatFs_uSD_DMA_RTOS` 示例。

### 问题 28：SD DMA 与 D-Cache 一致性（潜在问题）

**现象**：SD 验证已通过，但对比示例代码发现 MPU 配置差异。

**分析**：
| 项目 | 示例代码 | 当前项目 |
|---|---|---|
| MPU 0x24000000 | Non-Cacheable | Cacheable |
| DMA Cache 维护 | 禁用（因 MPU NC） | 未启用 |

当前项目 MPU 将 AXI SRAM 配置为 Cacheable，但 SD DMA 读写缓冲区（FreeRTOS 堆分配）在该区域。小数据传输碰巧成功，大数据传输可能读到脏缓存数据。

**修复**（`200bc2c`）：
- `sd_diskio.c`：启用 `ENABLE_SD_DMA_CACHE_MAINTENANCE=1`，读操作后 `SCB_InvalidateDCache_by_Addr`，写操作前 `SCB_CleanDCache_by_Addr`
- `sd_verify.c`：读缓冲区改用 `ALIGN_32BYTES` 对齐（Cache 行 32 字节）

**Flash 增量**：96 字节（99912→100008）

---

### 问题 29：BSP_SD_GetCardState() 状态判断反转（根本原因）

**现象**：`f_mount` 返回 error 3（FR_NOT_READY），`HAL SD state=1 error=0`。

**串口输出**：
```
[SD] HAL_SD_Init: ret=1 state=1 err=0x80000000
[SD] UNSUPPORTED_FEATURE cleared
[SD] FAIL: f_mount error 3
[SD] HAL SD state=1 error=0
```

**根因**：`BSP_SD_GetCardState()` 逻辑反转。

```c
// 旧代码：只认 TRANSFER(1) 为 OK
return (state == HAL_SD_CARD_TRANSFER) ? SD_TRANSFER_OK : SD_TRANSFER_BUSY;
```

SD 卡状态机：初始化完成后卡处于 IDLE(0) 或 STBY(2)，不是 TRANSFER(1)。旧代码把 IDLE 判为 BUSY → `SD_CheckStatus()` 返回 `STA_NOINIT` → `f_mount` 报 FR_NOT_READY。

**修复**（`3da8f7f`）：
```c
// 新代码：SENDING/RECEIVING/PROGRAMMING 为 BUSY，其余为 OK
if (card_state == HAL_SD_CARD_SENDING  ||
    card_state == HAL_SD_CARD_RECEIVING ||
    card_state == HAL_SD_CARD_PROGRAMMING)
  return SD_TRANSFER_BUSY;
return SD_TRANSFER_OK;
```

---

### 问题 30：4 位总线失败后 SDMMC 外设状态残留（根本原因之二）

**现象**：4-bit 失败后回退 1-bit 也失败，两次都报 CMD_CRC_FAIL (0x01)。

**串口输出**：
```
[SD] HAL_SD_Init: ret=1 state=1 err=0x80000000
[SD] Trying 4-bit bus...
[SD] 4-bit result: ret=1 err=0x1
[SD] Reinit + 1-bit fallback...
[SD] 1-bit result: ret=1 err=0x1
```

**根因分析**：

`HAL_SD_ConfigWideBusOperation()` 内部执行顺序：
1. **先** 将 SDMMC 外设切到 4-bit（写 `CLKCR.WIDBUS=10`）
2. **再** 发 CMD6 给卡要求切总线宽度
3. CMD6 CRC 失败 → 返回 HAL_ERROR

结果：**外设 4-bit，卡 1-bit，不匹配！**

后续 `f_mount` → `SD_read` → DMA 用 4-bit 外设读 1-bit 卡 → DMA 错误 (0x02000000)。

**尝试的修复及失败原因**：

| 尝试 | 结果 | 原因 |
|---|---|---|
| `HAL_SD_Init()` 重新初始化后切 1-bit | 1-bit 也 CRC 失败 | CMD0 重置卡后重新初始化序列异常 |
| 不做任何恢复直接用 | DMA 错误 0x02000000 | 外设 4-bit 卡 1-bit 不匹配 |

**最终修复**（`43d71ae`）：4-bit 失败后直接写 `SDMMC_CLKCR` 寄存器恢复外设为 1-bit：

```c
hsd1.Instance->CLKCR &= ~SDMMC_CLKCR_WIDBUS;  // WIDBUS[11:10]=00 → 1-bit
```

---

### 问题 31：HAL_SD_Init 重新初始化导致后续通信全部 CRC 失败

**现象**：4-bit 失败后调 `HAL_SD_Init()` 重新初始化，再切 1-bit，1-bit 也报 CMD_CRC_FAIL。

**串口输出**：
```
[SD] 4-bit result: ret=1 err=0x1
[SD] Reinit + 1-bit fallback...
[SD] 1-bit result: ret=1 err=0x1    ← 1.9 秒后超时
```

**分析**：`HAL_SD_Init()` 发 CMD0 重置卡到 IDLE 状态，然后重新走整个初始化序列（CMD8→ACMD41→CMD2→CMD3→CMD9）。但重新初始化后 SDMMC 外设与卡的通信状态异常，CMD13 轮询全部 CRC 失败。

**根因**：`HAL_SD_Init` 的 `SDMMC_Init()` 将外设重新配置为 4-bit（从 CubeMX 的 `hsd1.Init.BusWide` 读取），而卡因 CMD6 失败仍在 1-bit 模式。外设/卡总线宽度不匹配导致所有后续命令 CRC 失败。

**结论**：**不要在 4-bit 失败后调 `HAL_SD_Init` 重新初始化**。正确做法是只恢复外设总线宽度寄存器。

---

### 经验总结（续）

28. **STM32H7 SD DMA 必须考虑 D-Cache 一致性** —— AXI SRAM 是 Cacheable 的，DMA 缓冲区需要用 `ALIGN_32BYTES` 对齐，读操作后 `SCB_InvalidateDCache_by_Addr`，写操作前 `SCB_CleanDCache_by_Addr`。或者将缓冲区所在 MPU 区域设为 Non-Cacheable（如 H743 DEMO 示例的做法）

29. **BSP_SD_GetCardState() 必须正确处理 IDLE 状态** —— SD 卡初始化完成后处于 IDLE(0) 或 STBY(2)，不是 TRANSFER(1)。判断"卡忙"应该检查 SENDING/RECEIVING/PROGRAMMING，而不是只认 TRANSFER 为 OK。这是 CubeMX BSP 模板的一个常见陷阱

30. **HAL_SD_ConfigWideBusOperation 是"先改外设后改卡"** —— 内部先写 SDMMC CLKCR 寄存器切外设总线宽度，再发 CMD6 给卡。CMD6 失败时外设已经改了但卡没改，导致不匹配。修复必须手动恢复外设寄存器

31. **不要在 4-bit 失败后调 HAL_SD_Init 重新初始化** —— `HAL_SD_Init` 读 CubeMX 的 `hsd1.Init.BusWide`（4-bit）重新配置外设，与仍在 1-bit 的卡不匹配，导致所有后续命令 CRC 失败。CMD0 重置卡也会使已成功的初始化状态丢失

32. **对比官方示例代码是有效的调试手段** —— 对比 ST H743 DEMO 示例的 MPU 配置、sd_diskio 实现、初始化流程，发现了多个当前项目的潜在问题（Cache 一致性、状态判断反转、外设寄存器残留）
