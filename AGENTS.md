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
- **After every successful build**: `git add -A; git commit -m "..." ; git push`

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
// MX_FDCAN1_Init();       ← disabled, pending phase 5
MX_QUADSPI_Init();         // W25Q128
// MX_SDMMC1_SD_Init();    ← DO NOT CALL: FatFs BSP_SD_Init handles it
MX_USART2_UART_Init();
MX_FATFS_Init();           // registers SD_Driver (no card init yet)
SD_Detect_GPIO_Init();     // PA8 card-detect pin (input + pull-up)
// → QSPI_Verify + SD_Verify run in initTestTask (freertos.c) after scheduler starts
```

### SDMMC Traps — Two Critical Rules

1. **Never call `MX_SDMMC1_SD_Init()`** — FatFs' `SD_initialize()` calls `BSP_SD_Init()` internally on `f_mount()`. Double `HAL_SD_Init()` causes `FR_NOT_READY`.

2. **All FatFs/SD operations MUST run in a FreeRTOS task** (after `osKernelStart()`). `sd_diskio.c:SD_initialize()` checks `osKernelGetState() == osKernelRunning` and skips init if the kernel isn't running. Calling `f_mount()`/`SD_Verify()` from `main()` before the scheduler starts will silently fail with `FR_NOT_READY`, `state=0 error=0`.

3. **STM32H7 HAL `SD_ERROR_UNSUPPORTED_FEATURE` (0x80000000)** — `HAL_SD_Init()` may return this error while the card is actually ready (`State=HAL_SD_STATE_READY`). `BSP_SD_Init()` in this repo checks `State` and clears the error. Do not remove this workaround.

4. **4-bit bus → 1-bit fallback** — `HAL_SD_ConfigWideBusOperation(4B)` may fail with `CMD_CRC_FAIL` (0x01) on some hardware. `BSP_SD_Init()` automatically falls back to 1-bit mode. Diagnostic printed to USART2.

5. **PA8 card-detect is bypassed** — `BSP_SD_IsDetected()` currently hardcoded to `SD_PRESENT`. PA8 polarity/connection not yet verified. Do not restore the PA8 check until confirmed with a multimeter.

## QSPI W25Q128 — Avoid AutoPolling

`HAL_QSPI_AutoPolling()` has state machine conflicts on STM32H7 when preceded by `HAL_QSPI_Command()`. Use manual `ReadStatusReg1()` polling loop instead. See `w25qxx.c:WaitBusy()`.

## LAN8720 PHY Driver

CubeMX selected `lan8742.c` but hardware is LAN8720. Key differences:
- No SMR register (0x12) — SMR-based address scan returns garbage
- Solution: BSR probe on addresses 0-31 with 5 retries in `ethernetif.c`
- `ETH_PHY_IO_Init()` has `HAL_Delay(2000)` — **do not remove**, LAN8720 module needs 2s for MDIO to stabilize after power-up

## FreeRTOS Stack Sizes

All network tasks must be ≥ 2048 words (8KB). Deep call chain: `ip4_input → etharp_query → etharp_request` causes stack overflow at 1024 words.

| Task | Stack | Priority | Notes |
|---|---|---|---|
| defaultTask | 2048 | Normal | LwIP init, then exits |
| initTestTask | 1024 | Normal | QSPI + SD verify, then exits |
| tcpip_thread | 2048 | — | LwIP internal |
| EthIf | 2048 | — | LwIP internal |
| EthLink | 2048 | — | LwIP internal |
| heartbeatTask | 128 | Low | 1Hz PE10 blink |

## Pin Assignment Quick Reference

```
ETH RMII    : PA1/PA2/PA7/PC1/PC4/PC5/PB11/PB12/PB13
QSPI W25Q128: PB2/PB10/PD11/PD12/PE2/PD13
SDMMC1 TF   : PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CK) PD2(CMD)
SD detect   : PA8 (input + pull-up, bypassed in code — see SDMMC traps)
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

## Serial Debug Output — SD Error Codes

`[SD] HAL SD state=X error=Y` — common values:
| state | meaning | error | meaning |
|---|---|---|---|
| 0 | RESET (HAL_SD_Init never called) | 0 | no error |
| 1 | READY (card initialized OK) | 0x01 | CMD_CRC_FAIL |
| | | 0x80000000 | UNSUPPORTED_FEATURE (recoverable) |

## Existing Docs

- `CLAUDE.md` — full architecture constraints, CubeMX overwrite list, LwIP/MPU/cache details
- `PROJECT_REQUIREMENTS.md` — hardware spec, pin allocation, phase roadmap
- `DEBUG_LOG.md` — every bug's symptom → diagnosis → fix, numbered lessons #1-#27
- `BUILD_AND_TEST.md` — toolchain setup, wiring guide, step-by-step verification
