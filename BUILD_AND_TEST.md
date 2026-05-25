# STM32H750 CAN 网关 — 编译与测试指南

> 目标：STM32H750 + LAN8720 以太网验证（阶段 1），串口打印 PHY 地址和链路状态

---

## 一、安装工具链（首次需要）

### 1.1 下载 Arm GNU 工具链

访问官网下载页面：
https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

选择：
- **版本**：13.2.Rel1 或更新（推荐 13.3）
- **平台**：Windows (mingw-w64-i686) hosted cross toolchains
- **目标**：AArch32 bare-metal target (arm-none-eabi)
- **文件**：`arm-gnu-toolchain-13.x-mingw-w64-i686-arm-none-eabi.exe`（安装器）或 `.zip`（免安装）

安装到：`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\13.x\`（默认路径）

### 1.2 下载 Make for Windows

**方案 A：MinGW-w64 make（推荐）**
- 下载：https://github.com/skeeto/w64devkit/releases
- 解压 `w64devkit-x.x.x.zip` 到 `C:\w64devkit`
- make 位于 `C:\w64devkit\bin\make.exe`

**方案 B：MSYS2**
- 下载：https://www.msys2.org/
- 安装后在 MSYS2 终端运行：`pacman -S make`
- make 位于 `C:\msys64\usr\bin\make.exe`

### 1.3 配置 PATH 环境变量

将以下路径加入系统 PATH（Win + R → `sysdm.cpl` → 高级 → 环境变量 → 系统变量 PATH → 编辑 → 新建）：

```
C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\13.x\bin
C:\w64devkit\bin
```

**验证安装**（重启 PowerShell 后）：
```powershell
arm-none-eabi-gcc --version
make --version
```

---

## 二、编译固件

### 2.1 清理旧构建产物

```powershell
cd D:\Ben\stm32_canbus_claude\CANbus_code
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
```

### 2.2 编译

```powershell
make
```

**预期输出**（最后几行）：
```
arm-none-eabi-size build/CANbus_code.elf
   text    data     bss     dec     hex filename
 123456    1234   12345  137035   21745 build/CANbus_code.elf
arm-none-eabi-objcopy -O ihex build/CANbus_code.elf build/CANbus_code.hex
arm-none-eabi-objcopy -O binary -S build/CANbus_code.elf build/CANbus_code.bin
```

**生成文件**：
- `build/CANbus_code.elf`（调试用）
- `build/CANbus_code.hex`（烧录用）
- `build/CANbus_code.bin`（烧录用）

### 2.3 常见编译错误

| 错误 | 原因 | 解决 |
|---|---|---|
| `make: command not found` | make 不在 PATH | 检查 1.3 步骤，重启 PowerShell |
| `arm-none-eabi-gcc: command not found` | 工具链不在 PATH | 检查 1.3 步骤，重启 PowerShell |
| `region 'FLASH' overflowed` | 代码超过 128KB | 正常，一期 MVP 会超，二期切 W25Q128 XIP |
| `undefined reference to ...` | 链接错误 | 检查 Makefile 的 C_SOURCES 是否包含所有 .c |

---

## 三、硬件连接

### 3.1 最小系统

| 接口 | 连接 |
|---|---|
| **电源** | USB-C 5V（板载稳压） |
| **调试** | ST-Link V2 → SWDIO(PA13) / SWCLK(PA14) / GND / 3.3V |
| **串口** | USB-TTL → RX(PD5) / TX(PD6) / GND，**115200 8N1** |
| **以太网** | LAN8720 模块 → 网线 → 电脑网卡（或交换机） |

### 3.2 LAN8720 模块接线（RMII）

| STM32H750 | LAN8720 模块 | 说明 |
|---|---|---|
| PA1 | nINT/RETCLK | 50MHz 时钟输入（模块自带晶振） |
| PA2 | MDIO | 管理数据 |
| PA7 | CRS_DV | 载波侦听/数据有效 |
| PC1 | MDC | 管理时钟 |
| PC4 | RXD0 | 接收数据 0 |
| PC5 | RXD1 | 接收数据 1 |
| PB11 | TX_EN | 发送使能 |
| PB12 | TXD0 | 发送数据 0 |
| PB13 | TXD1 | 发送数据 1 |
| 3.3V | VCC | 模块供电 |
| GND | GND | 公共地 |

**注意**：
- LAN8720 模块的 50MHz 时钟由板载有源晶振提供，接到 STM32 的 PA1（ETH_RMII_REF_CLK）
- 模块未引出 RESET 脚，上电自动复位
- 确认模块跳线选择 RMII 模式（非 MII）

### 3.3 继电器模块（可选，仅用于心跳指示）

| STM32H750 | 继电器模块 |
|---|---|
| PE7 | IN1 |
| PE8 | IN2 |
| 3.3V | VCC |
| GND | GND |

**跳线设置**：高电平触发（PE7/PE8 输出高时继电器吸合）

---

## 四、烧录固件

### 4.1 使用 ST-Link Utility（Windows）

1. 打开 STM32 ST-LINK Utility
2. Target → Connect
3. File → Open File → 选择 `build/CANbus_code.hex`
4. Target → Program & Verify
5. 勾选 "Reset after programming"
6. 点击 Start

### 4.2 使用 STM32CubeProgrammer（跨平台）

```powershell
STM32_Programmer_CLI -c port=SWD -w build/CANbus_code.hex -v -rst
```

### 4.3 使用 OpenOCD + GDB（命令行）

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "program build/CANbus_code.elf verify reset exit"
```

---

## 五、测试步骤

### 5.1 串口监控（必须先做）

打开串口工具（PuTTY / Tera Term / minicom）：
- 端口：COM? （设备管理器查看 USB-TTL 的 COM 号）
- 波特率：115200
- 数据位：8
- 停止位：1
- 校验：无
- 流控：无

**预期输出**（上电后 2-3 秒内）：
```
[ETH] PHY detected at address: 0
[ETH] PHY link state: 1
```

或者：
```
[ETH] PHY detected at address: 1
[ETH] PHY link state: 2
```

**PHY 地址含义**：
- `0` 或 `1`：LAN8720 模块的 MDIO 地址（由模块硬件跳线决定）
- 如果输出 `32`（超出 0-31 范围）：说明 PHY 未检测到，检查 RMII 接线

**链路状态含义**（参考 `lan8742.h` 定义）：
- `1`：`LAN8742_STATUS_LINK_DOWN`（网线未插或对端未 up）
- `2`：`LAN8742_STATUS_100MBITS_FULLDUPLEX`（100M 全双工，正常）
- `3`：`LAN8742_STATUS_100MBITS_HALFDUPLEX`（100M 半双工）
- `4`：`LAN8742_STATUS_10MBITS_FULLDUPLEX`（10M 全双工）
- `5`：`LAN8742_STATUS_10MBITS_HALFDUPLEX`（10M 半双工）
- `6`：`LAN8742_STATUS_AUTONEGO_NOTDONE`（自协商未完成，等 1-2 秒）

### 5.2 心跳灯检查

观察 PE7（继电器 1 或用万用表测 PE7 电压）：
- **预期**：每 500ms 翻转一次（1Hz 闪烁，高 500ms → 低 500ms）
- **如果不闪**：程序卡死或未启动，检查烧录、复位、电源

### 5.3 配置电脑网卡

**Windows**：
1. 控制面板 → 网络和共享中心 → 更改适配器设置
2. 右键以太网适配器 → 属性 → Internet 协议版本 4 (TCP/IPv4) → 属性
3. 选择"使用下面的 IP 地址"：
   - IP 地址：`192.168.1.100`（或 .2 ~ .254，避开 .88）
   - 子网掩码：`255.255.255.0`
   - 默认网关：留空（或 `192.168.1.1`）
4. 确定

**Linux / macOS**：
```bash
sudo ifconfig eth0 192.168.1.100 netmask 255.255.255.0
```

### 5.4 Ping 测试

```powershell
ping 192.168.1.88 -t
```

**预期输出**：
```
正在 Ping 192.168.1.88 具有 32 字节的数据:
来自 192.168.1.88 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.1.88 的回复: 字节=32 时间<1ms TTL=64
来自 192.168.1.88 的回复: 字节=32 时间<1ms TTL=64
```

**如果超时**：
1. 检查串口输出的 PHY 地址是否为 0-31（不是 32）
2. 检查串口输出的链路状态是否为 2/3/4/5（不是 1 或 6）
3. 检查网线是否插好、LAN8720 模块 LED 是否亮
4. 检查电脑网卡 IP 是否在 192.168.1.x 网段
5. 关闭电脑防火墙（临时测试）
6. 用 Wireshark 抓包看 ARP 请求是否发出

---

## 六、故障排查清单

### 6.1 串口无输出

| 症状 | 可能原因 | 排查 |
|---|---|---|
| 完全无输出 | 程序未运行 | 检查烧录、复位、电源、ST-Link 连接 |
| 乱码 | 波特率错误 | 确认串口工具设为 115200 8N1 |
| 只有部分输出 | 程序卡死 | 检查 FreeRTOS 堆栈溢出、MPU 配置 |

### 6.2 PHY 地址显示 32

**原因**：`LAN8742_Init()` 扫描 0-31 地址都未找到 PHY（SMR 寄存器读取失败）

**排查**：
1. 检查 RMII 接线（特别是 MDC/MDIO/REF_CLK）
2. 用示波器测 PA1 是否有 50MHz 时钟（LAN8720 模块晶振输出）
3. 检查 LAN8720 模块供电（3.3V）
4. 检查 LAN8720 模块是否损坏（换一个模块）

### 6.3 链路状态一直是 1（LINK_DOWN）

**原因**：PHY 检测到但链路未建立

**排查**：
1. 插好网线（两端都要插紧）
2. 检查对端设备（电脑网卡、交换机）是否 up
3. 检查 LAN8720 模块 LED：
   - 绿灯常亮：链路 OK
   - 黄灯闪烁：有数据传输
   - 都不亮：链路未建立
4. 换一根网线
5. 直连电脑网卡（不经过交换机）

### 6.4 Ping 超时但串口显示链路正常

**原因**：LwIP 协议栈或 ETH DMA 配置问题

**排查**：
1. 检查电脑网卡 IP 是否在 192.168.1.x 网段
2. 关闭电脑防火墙（临时）
3. 用 Wireshark 抓包：
   - 能看到 ARP 请求但无应答 → STM32 未收到或 DMA 配置错误
   - 完全看不到 ARP 请求 → 电脑网卡问题
4. 检查 MPU 配置（0x30000000 必须是 non-cacheable）
5. 检查链接脚本的 `.lwip_sec` 段是否正确映射到 RAM_D2

### 6.5 编译后 Flash 超过 128KB

**原因**：HAL + FreeRTOS + LwIP + FatFs 一起编译接近或超过 128KB

**临时方案**：
- 在 Makefile 的 `OPT` 改为 `-Os`（优化体积）
- 注释掉 FatFs 相关源文件（一期 ping 测试不需要）

**长期方案**（二期）：
- 切换到 W25Q128 XIP 模式（bootloader 在内部 Flash，主程序在外部 Flash Memory Mapped 执行）

---

## 七、成功标志

✅ 串口输出 `[ETH] PHY detected at address: 0` 或 `1`（不是 32）
✅ 串口输出 `[ETH] PHY link state: 2` 或 `3/4/5`（不是 1 或 6）
✅ PE7 以 1Hz 频率闪烁（继电器吸合 500ms → 释放 500ms）
✅ 电脑 `ping 192.168.1.88` 稳定通，延迟 <1ms

**下一步**：
- 加入 USART2 printf 重定向（方便后续调试）
- 恢复 FDCAN1 初始化，验证 CAN 收发
- 恢复 SDMMC + FatFs，验证 TF 卡读写
- 开始 DBC 解析器开发

---

## 八、参考资料

- STM32H750 数据手册：`demo&data/STM32/STM32H7XX-sch.pdf`
- LAN8720 参考代码：`demo&data/LAN8720/源代码stm32f407zgt6-LAN8720A 网络通信实验/`
- LwIP 官方文档：https://www.nongnu.org/lwip/2_1_x/index.html
- STM32H7 ETH 应用笔记：AN5348（ST 官网下载）

---

**编写时间**：2026-05-24
**对应 commit**：`a838c9d` — feat: LAN8720 ping-only 固件适配
