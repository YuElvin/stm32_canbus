# AGENTS.md

Compact guidance for AI agents working in this STM32H750 CAN gateway repo.
> 中文版见 `AGENTS_CN.md`。修改任一文件时需同步更新另一份。

## Build

```powershell
# PowerShell — toolchain path MUST be prepended
$env:PATH = "D:\arm-gnu-toolchain-14.2\bin;$env:PATH"
make -j8
```

- Working dir: `CANbus_code/`
- Clean build: `rm -r -Force build/; make` (`make clean` is unreliable on Windows)
- Output: `CANbus_code/build/` (`.bin` `.hex` `.elf`)
- Flash: ~99KB / 128KB (`-Og`). Near limit — see Flash section below.

## Flash 128KB Limit — Critical

FatFs `cc936.c` (Chinese code page) is 170KB and **overflows Flash**. Current fix: `_CODE_PAGE=437` + `ccsbcs.c`. If you need Chinese filenames, you must switch to `-Os` or QSPI XIP first.

## CubeMX Regeneration Hazard

`CANbus_code.ioc` generates most files, but **11 files have manual edits** that get overwritten. See `CLAUDE.md` § "CubeMX 重新生成会覆盖的手动修改" for the full list. Workflow:

```bash
git stash          # save current work
# regenerate in CubeMX
git diff           # review what changed
git checkout -- <manually-edited-files>   # restore
git stash pop      # reapply
```

## D2 SRAM Memory Layout — Do Not Touch

ETH DMA descriptors and LwIP heap are pinned to specific addresses via `.lwip_sec` in the linker script:

```
0x30000000  DMARxDscrTab   (4×24B)
0x30000080  DMATxDscrTab   (4×24B)
0x30000100  Rx_PoolSection (12×1536B ≈18KB)
0x30005000  LWIP_RAM_HEAP  (16KB)
```

Changing `ETH_RX_BUFFER_CNT` or `ETH_RX_BUFFER_SIZE` requires rechecking the `.map` file and adjusting `LWIP_RAM_HEAP_POINTER`.

## Peripheral Init Order (main.c)

```
MX_GPIO_Init();
// MX_FDCAN1_Init();    ← disabled, pending phase 5
MX_QUADSPI_Init();      // W25Q128
// MX_SDMMC1_SD_Init(); ← DO NOT CALL: FatFs BSP_SD_Init handles it
MX_USART2_UART_Init();
MX_FATFS_Init();        // triggers BSP_SD_Init internally
```

**SDMMC trap**: Calling `MX_SDMMC1_SD_Init()` + `f_mount()` causes double `HAL_SD_Init()` → FR_NOT_READY. Let FatFs own SD init.

## QSPI W25Q128 — Avoid AutoPolling

`HAL_QSPI_AutoPolling()` has state machine conflicts on STM32H7 when preceded by `HAL_QSPI_Command()`. Use manual `ReadStatusReg1()` polling loop instead. See `w25qxx.c:WaitBusy()`.

## LAN8720 PHY Driver

CubeMX selected `lan8742.c` but hardware is LAN8720. Key differences:
- No SMR register (0x12) — SMR-based address scan returns garbage
- Solution: BSR probe on addresses 0-31 with 5 retries in `ethernetif.c`
- `ETH_PHY_IO_Init()` has `HAL_Delay(2000)` — **do not remove**, LAN8720 module needs 2s for MDIO to stabilize after power-up

## FreeRTOS Stack Sizes

All network tasks must be ≥ 2048 words (8KB). Deep call chain: `ip4_input → etharp_query → etharp_request` causes stack overflow at 1024 words. Task list:

| Task | Stack | Priority |
|---|---|---|
| defaultTask | 2048 | Normal (exits after LwIP init) |
| tcpip_thread | 2048 | — (LwIP internal) |
| EthIf | 2048 | — (LwIP internal) |
| EthLink | 2048 | — (LwIP internal) |
| heartbeatTask | 128 | Low |

## Pin Assignment Quick Reference

```
ETH RMII    : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF   : PC8-PC12/PD2
FDCAN1      : PD0(RX) PD1(TX)
Relay       : PE7(Relay1) PE8(Relay2) — active high, default low
Debug LED   : PE10(DBG_LED1) PE11(DBG_LED2) — active high
USART2      : PD5(TX) PD6(RX) — 115200 8N1
```

## Crash Debugging

Fault handlers print PC/LR/CFSR to USART2. After crash:

```bash
arm-none-eabi-addr2line -e build/CANbus_code.elf -f -C <PC_hex>
```

## Existing Docs

- `CLAUDE.md` — full architecture constraints, CubeMX overwrite list, LwIP/MPU/cache details
- `PROJECT_REQUIREMENTS.md` — hardware spec, pin allocation, phase roadmap
- `DEBUG_LOG.md` — every bug's symptom → diagnosis → fix, numbered lessons #1-#23
- `BUILD_AND_TEST.md` — toolchain setup, wiring guide, step-by-step verification
