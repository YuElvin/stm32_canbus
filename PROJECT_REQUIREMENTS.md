# STM32H750 CAN 网关项目 — 需求理解备忘

> 最后更新：2026-05-25 | 项目阶段：阶段 1 | 原始来源：`STM32项目沟通.docx`

本文件由 Claude 根据 `STM32项目沟通.docx` 整理，用于在动手之前与你对齐理解。
如有错漏请直接修改本文件，或在对话中指出，确认后再开始下一步开发。

---

## 一、一句话定义

基于 STM32H750VBT6 的 **CAN/CAN-FD 数据采集解析网关 + 本地以太网 Web 配置终端 + 规则控制器 + 日志记录器**，后续可通过 ESP32-C3 SPI 扩展 WiFi 访问。

---

## 二、硬件清单（已确认）

| 部件 | 型号/规格 | 接口 | 备注 |
|---|---|---|---|
| 主控核心板 | YD-STM32H750VBT6（淘宝现成开发板） | — | 板载 25MHz 晶振、32.768kHz、TF 卡座、W25QXX、USB-C、SWD |
| 网络 PHY | LAN8720 模块 | RMII | 模块自带 50MHz 有源晶振，未引出复位脚 |
| CAN 收发器 | TJA1042 模块 | — | 5V 供电、支持 CAN-FD、无 STB/EN，**RXD 电平待实测** |
| TF 卡 | 板载卡座 | SDMMC1 4 位 | PC8/PC9/PC10/PC11/PC12/PD2 |
| 外部 Flash | W25Q128（板载） | QUADSPI Bank1 | PB2/PB10/PD11/PD12/PE2/PD13，16MB |
| 继电器 | 2 路模块 | GPIO | 3.3V 供电，跳线选高/低电平触发，**第一版按高电平触发** |
| 调试串口 | USB-TTL | USART2 | PD5/PD6，115200 8N1 |
| 二期 WiFi | ESP32-C3-MINI-1 | SPI（首选） | 一期不接，二期再决定具体 SPI 实例 |

---

## 三、引脚分配 v1（与 `CANbus_code.ioc` 对齐）

```
以太网（LAN8720，RMII）
  PA1   ETH_RMII_REF_CLK     ← LAN8720 nINT/RETCLK（50MHz 输入）
  PA2   ETH_MDIO
  PA7   ETH_RMII_CRS_DV
  PC1   ETH_MDC
  PC4   ETH_RMII_RXD0
  PC5   ETH_RMII_RXD1
  PB11  ETH_RMII_TX_EN
  PB12  ETH_RMII_TXD0
  PB13  ETH_RMII_TXD1

QSPI（W25Q128）
  PB2   QUADSPI_CLK
  PB10  QUADSPI_BK1_NCS
  PD11  QUADSPI_BK1_IO0
  PD12  QUADSPI_BK1_IO1
  PE2   QUADSPI_BK1_IO2
  PD13  QUADSPI_BK1_IO3

SDMMC1（TF 卡）
  PC8/PC9/PC10/PC11  D0/D1/D2/D3
  PC12               时钟
  PD2                命令

FDCAN1（一期单路 CAN）
  PD0   FDCAN1_RX
  PD1   FDCAN1_TX

继电器
  PE7   Relay1（GPIO 输出，上电默认关）
  PE8   Relay2（GPIO 输出，上电默认关）

调试
  PD5   USART2_TX → USB-TTL RX
  PD6   USART2_RX ← USB-TTL TX
  PA13  SWDIO
  PA14  SWCLK
```

预留：FDCAN2 二期接 PB5/PB6（待 SPI 方案确定后再确认是否冲突）。

---

## 四、功能需求（按优先级）

### 4.1 一期 MVP（必须有）

1. **CAN 收发**
   - FDCAN1 一路，标准帧 + 扩展帧
   - 波特率 Web 可选：125K / 250K / 500K / 1M
   - CAN-FD 支持（仲裁段 500K + 数据段 2M/4M，BRS 可选）
   - 周期发送 CAN 报文
   - 支持过滤器、总线错误统计

2. **DBC 解析与编码**
   - Web 上传 DBC 文件，存到 TF 卡 `/dbc/`
   - 多 DBC 切换（当前激活的写入 `/config/config.json`）
   - **接收方向**：解析 BO_/SG_，输出物理值 `物理值 = 原始值 × factor + offset`
   - **发送方向**：根据 DBC 反算原始值，按比特位填入 CAN data
   - 支持 Intel/Motorola 字节序、有/无符号、factor/offset、min/max、unit
   - 一期不做：Multiplex、J1939、复杂 BA_ 属性、VAL_ 枚举

3. **数据日志**
   - 写入 TF 卡 `/log/`
   - **格式默认 CSV**，可选保存原始帧 / 解析信号 / 同时保存
   - **采样周期 100ms**
   - 文件命名：启动时按当前时间生成（如 `20260524_153000.csv`）
   - 不要求断电不丢最后几秒数据
   - Web 可下载日志

4. **规则引擎控制继电器**
   - Web 配置规则：信号名 + 比较运算符 + 阈值 + 目标继电器
   - **手动模式优先级最高**（手动 > 失效保护 > 自动规则 > 默认）
   - **滞回控制**（双阈值：开启阈值 / 关闭阈值）
   - **触发延时**（持续 N 毫秒才动作）
   - **CAN 超时安全动作**（N 秒未收到关键报文 → 继电器进入预设状态）
   - 上电默认状态可配置

5. **以太网 Web**
   - **固定 IP 192.168.1.88**，掩码 255.255.255.0，网关 192.168.1.1
   - 局域网访问，**不需要登录密码**
   - 中文界面，简洁明亮暖色调风格
   - 实时刷新 **1 秒**
   - REST API + 后续可升级 SSE / WebSocket 推送

6. **Web 页面结构**
   - 概览（系统状态 / CAN 状态 / 当前 DBC / TF 卡 / 继电器）
   - 实时数据（信号表格、按消息筛选、按名称搜索）
   - DBC 管理（上传、列表、切换、删除、查看）
   - CAN 发送（原始帧、按 DBC 编辑发送、周期发送列表）
   - 日志（开关、模式、文件列表、下载/删除）
   - 规则（添加、阈值、滞回、延时、目标、超时动作）
   - 系统设置（IP、波特率、CAN-FD 参数、时间、重启）

### 4.2 二期扩展

- 第 2 路 FDCAN
- ESP32-C3 SPI WiFi（推荐先用 UART 验证，再切 SPI）
- W25Q128 做配置、最小 Web、当前 DBC 的 Flash 备份

---

## 五、存储分工

| 介质 | 用途 |
|---|---|
| 内部 Flash 128KB | 仅 bootloader 和关键代码（容量受限） |
| **W25Q128 16MB** | 一期：仅做硬件验证（JEDEC ID 读取、读写校验）。二期：配置备份、最小 Web 页面、当前 DBC 索引 |
| **TF 卡** | 主存储：Web 静态文件 `/www/`、DBC `/dbc/`、日志 `/log/`、配置 `/config/` |

```
TF 卡目录结构：
/
├── www/        index.html、app.js、style.css、assets/
├── dbc/        *.dbc
├── log/        YYYYMMDD_HHMMSS.csv
├── config/     config.json、rules.json、can_tx.json
└── sys/        version.txt
```

---

## 六、软件架构

**技术栈**：STM32CubeMX + HAL + FreeRTOS + LwIP + FatFs + 自研轻量 DBC 解析器 + HTTP REST + 后续 SSE
**开发环境**：VSCode + Arm GNU 工具链 + CubeMX 生成的 Makefile（已具备）+ Cortex-Debug + ST-Link

### FreeRTOS 任务划分（一期）

| 任务 | 优先级 | 职责 |
|---|---|---|
| AppMain | — | 系统初始化、状态监控 |
| CanRx | 高 | FDCAN FIFO → 接收队列 |
| CanTx | 高 | 手动 / 周期 / 按 DBC 编码发送 |
| DbcDecode | 中高 | 接收队列 → DBC 解码 → 信号缓存 |
| Rule | 中高 | 规则扫描 / 事件驱动 → 控制继电器 |
| Web | 中 | HTTP 服务、API、文件上传下载 |
| Logger | 中低 | 100ms 快照 → RAM 缓存 → 批量写入 TF 卡 |
| Config | 低 | 配置保存、DBC 切换、QSPI 写入 |

### 关键互斥

- **TF 卡互斥锁**：日志写入、Web 读静态文件、DBC 加载、配置读写都必须先拿锁
- **信号缓存**：DBC 解码 → 规则 + 日志 + Web 多读者，建议读写锁或快照机制

---

## 七、已识别的关键风险

1. **STM32H7 以太网 + D-Cache + MPU**（最高风险）：DMA 描述符必须放在 D2 SRAM，MPU 配置成不可缓存，发送前 Clean、接收后 Invalidate Cache
2. **CubeMX 选了 LAN8742 但硬件是 LAN8720**：寄存器布局兼容（BCR/BSR/SMR/PHYSCSR 一致），现有 lan8742 驱动可直接复用，但需实测 PA1 的 50MHz 时钟和 PHY 地址
3. **CAN 模块 5V 供电的 RXD 电平**：必须用万用表测空闲电压，5V 直接接 STM32 会损坏 IO
4. **内部 Flash 仅 128KB**：HAL + RTOS + LwIP + FatFs 一起编译就很吃紧，二期可能需要切到 W25Q128 XIP（bootloader + Memory Mapped 模式）
5. **DBC 反向编码**：发送侧的比特位填充比解析更难，Motorola 字节序容易写错
6. **TF 卡阻塞**：单次写延迟可能超过 50ms，必须 RAM 缓存 + 批量刷盘，且与 Web 静态资源读取存在竞争

---

## 八、当前工程状态

- ✅ CubeMX 工程已生成（`CANbus_code.ioc`），Makefile 和启动文件齐全
- ✅ 引脚分配与 v1 方案一致
- ✅ 已备份为 `CANbus_code_backup_20260524`
- ⚠️ CubeMX PHY 选的是 LAN8742，需在代码层确认能驱动 LAN8720
- ⚠️ 链接脚本 `STM32H750XX_FLASH.ld` **缺少 `.RxDescripSection` / `.TxDescripSection` / `.Rx_PoolSection` 段定义**，但 `ethernetif.c` 引用了它们 —— 这是必须修复的
- ⚠️ 主程序 `main.c` 当前会初始化 FDCAN / QSPI / SDMMC / USART / FATFS 等所有外设，单独验证以太网时建议剥离

---

## 九、分阶段开发路线

| 阶段 | 目标 | 验收标准 |
|---|---|---|
| **0. 资料和电平实测** | 确认 PA1 有 50MHz 时钟、CAN 模块 RXD 电平、PHY 地址 | 示波器和万用表数据 |
| **1. 单独验证 LAN8720** | 剥离非以太网外设，固定 IP + ping | `ping 192.168.1.88` 稳定通 |
| 2. 串口 + GPIO + 继电器 | USART2 printf、PE7/PE8 继电器闪 | 串口有输出、继电器能吸合 |
| 3. QSPI W25Q128 | 读 JEDEC ID、4KB 扇区擦除、256B 页编程、读回校验 | 读到 `EF 40 18` |
| 4. SDMMC + FatFs | 挂载 TF 卡、写 `/log/test.csv` | 文件能在电脑上打开 |
| 5. FDCAN1 | 内部回环 → 正常模式 + 分析仪收发 | CAN 分析仪能看到帧 |
| 6. CAN + Web 原始帧 | Web 显示 CAN 原始数据、Web 手动发帧 | 浏览器能实时看到帧 |
| 7. 日志 | CSV 写 TF 卡、Web 下载日志 | 下载的文件能正常打开 |
| 8. DBC 解析 | Web 上传 DBC、解析显示物理值 | 信号值对得上 |
| 9. 规则引擎 | Web 配置 → 控制继电器 + 滞回 / 延时 | 规则触发正确 |
| 10. DBC 编辑发送 | Web 表单按 DBC 编码发送 | 分析仪收到正确帧 |
| 11. ESP32-C3 WiFi（二期） | 先用 UART 验证，再切 SPI | 手机连 AP 后能访问 Web |

---

## 十、当前任务

**你当前明确的任务**：
> 适配 LAN8720 PHY，编译一份只验证 LAN8720 的固件，让电脑能 ping 通

**Claude 计划**（待你确认本文档后执行）：
1. 修复链接脚本，补上 ETH DMA 段定义（`.RxDescripSection` / `.TxDescripSection` / `.Rx_PoolSection`）
2. 确认 lan8742 驱动可复用驱动 LAN8720（识别 PHY 地址、读 BSR / PHYSCSR）
3. 把 `main.c` 剥离成只跑以太网：保留 USART2 printf + 一个心跳指示（GPIO 闪灯）+ MX_LWIP_Init，关掉 FDCAN / QSPI / SDMMC / FATFS 初始化
4. 检查 MPU 配置覆盖 0x30000000 起的 D2 SRAM 256KB（已有），确认 ETH DMA 描述符落在该区域
5. 用现有 Makefile 编译，验证无错误
6. 给出烧录与测试步骤（电脑网卡 IP 设置、ping 命令、失败时的排查清单）
