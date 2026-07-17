# Claude Handoff — MIPS_STM32

Context for resuming this project in a new Claude session. Read this plus the
three docs in `docs/` and you'll have the full picture.

## What this project is

Migrating GAA Custom Electronics' **MIPS** controller (mass spectrometry / ion
processing, deployed at 47+ institutions) from the **Arduino Due (SAM3X8E,
Cortex-M3, 84 MHz)** to the **STM32H743 (Cortex-M7, 480 MHz)** on **native
STM32Cube (HAL/LL)**. The goal is "clean up a lot, not just port."

Owner: Gordon Anderson. Works on macOS (M1 iMac), VS Code + PlatformIO, uses the
PlatformIO Git GUI. Original firmware: `github.com/GordonAnderson/MIPS`
(~42,700 lines, 41 modules). Core library: `github.com/GordonAnderson/GAACE_Core`.

## Decisions already locked (do not relitigate without reason)

1. **MCU:** STM32H743ZIT6 (144-pin) for the final board; WeAct MiniSTM32H743VITX
   (100-pin VIT6) as the bring-up board. Rev V silicon (480 MHz/VOS0).
2. **No preemptive RTOS.** Bare-metal + cooperative scheduler + PendSV
   DeferredQueue. The queue uses PendSV; a preemptive RTOS would claim it — they
   are mutually exclusive, which is *the* concrete reason for bare-metal.
3. **Hybrid LL/HAL.** LL for the pulse timer (immediate mid-period compare needs
   register precision); HAL for USB/QSPI/SPI/I2C/UART.
4. **GAACE_Core command processor**, adopted and made Arduino-free on a branch
   called **`stm32`** (already pushed). Serial.cpp monolith → per-module tables.
5. **Ethernet = external RS-232-to-Ethernet module on a UART** (original MIPS
   approach). NO native LwIP/MAC/PHY. This removes the linker-script/RAM_D2
   complexity that other GAA-CE H7 projects carry.
6. **Storage:** GAACE FlashFS on QSPI NOR for config; SD (display module) as USB
   MSC. **USB:** composite CDC+MSC, HAL non-RTOS mode.
7. **Cleanup scope = Tier 2:** build a clean core first, migrate the 41 modules
   onto it conservatively, cleaning each as it lands.

## What's been built (and verified off-target)

In `lib/` (this repo):
- **bus_queue/** — `DeferredQueue.h` / `I2CQueue.h` / `SPIQueue.h`. Ported from
  the Arduino/SAMD version to native Cube (CMSIS-only core, HAL I2C/SPI adapters,
  PRIMASK critical sections, `__DMB` barriers, DWT µs clock). Compile + function
  tested. Note: I2C WriteRead uses `HAL_I2C_Mem_Read` (the Seq_* APIs are IT/DMA
  only). See `docs/port_notes/DeferredQueue_H7_PortNotes.md`.
- **pulse_timer/** — `STM32PulseTimer.h/.cpp`. TIM2, LL-based. CRITICAL design:
  compare preload DISABLED for immediate mid-period pulse updates; passed-compare
  hazard handling (HAZARD_FIRE_NOW default); narrow active-low LDAC pulse for
  AD5668; ETR hardware trigger; 3 callbacks. Caller passes real TIM2 kernel clock
  to begin(). See `docs/port_notes/STM32PulseTimer_NOTES.md`.
- **transports/** — `GUartStream.h` / `GUsbCdcStream.h`. GStream (GAACE's
  Stream/Print base) implementations over HAL UART and USB CDC.

In the `GAACE_Core` repo, branch **`stm32`** (already pushed to GitHub):
- Command processor, RingBuffer, charAllocate: Arduino-free (String removed →
  strtol/strtof/sscanf; uses `GStream*` not Arduino `Stream*`). Fixed a real
  `getValue(uint32_t*)` format-specifier bug.
- New: `GStream.h` (own Stream/Print base), `GArduinoStream.h` (ONE bridge for
  all Arduino platforms), `gaace_compat.h` (dual-build switch), `GHal.h`
  (optional project hook for debug's pin/analog commands).
- `Button` (dual-build: STM32 port+pin ctor + Arduino int-pin ctor) and `debug`
  (portable core + H7 branches for RESET/UUID/CPUTEMP; pin/analog via GHal).
- `library.json`: `frameworks: "*"`. `DiagnosticDemo` example updated to wrap
  Serial in GArduinoStream.
- **Verified:** compiles + runs BOTH ways (Arduino and pure-Cube) from identical
  sources. **User will hardware-test the Arduino DiagnosticDemo on a Feather M0**
  (a SAMD21 — exercises the common code + SAMD paths, NOT the H7 branches).
- Still Arduino-only on the branch (not yet ported): `Devices`, `WireHelper`,
  `SerialBuffer`, `WireServer`. `AtomicBlock` (Cortex-M generic) and `Errors`
  (pure enum) need no changes.

## Immediate next action

**Run CubeMX**, generate into `cubemx/`, following `TODO.md` Phase 0 and
`docs/MIPS_Rev6_PinPlanning_Worksheet.docx`. The project is set up to build out
of `cubemx/Core/Src` (see `platformio.ini`). Nothing builds until CubeMX runs.

## Key hardware facts (from reverse-engineering Rev 5.4)

- Module bus J4/J5/J6: 16 outputs A–P (74HC595 via SPI → level shifters),
  8 inputs Q–X (direct GPIO). Q/R are 32-bit timer capture/trigger → TIM2/TIM5.
- Analog/signal-conditioning circuits (design spec §2.2.6): IC11 gate/pulse
  (LMV358 + FDN340P PMOS off PWM13 — intent still to be confirmed by Gordon),
  IC3/IC5 LDAC timing, IC1/IC2 Schmitt input conditioning, U3 TFT/SD level shift.
- Pulse engine (Table.cpp): STM32PulseTimer must reproduce its exact LDAC timing;
  scope-validate before migrating pulse-dependent modules.

## Open items needing Gordon's input

- IC11 gate/pulse circuit intent (design spec §2.2.6) before Rev 6.0 schematic.
- Whether the numbered pin/analog `debug` commands are wanted on STM32 (a project
  `GHal` implementation is the hook if so).
- 3.3 V power architecture for the custom board (design spec §11.2.2).

## Working style notes

- Gordon values correctness and being told when something is uncertain rather
  than glossed. Verify (compile/test) rather than assert. Flag real design forks
  instead of silently choosing. He pushes to GitHub himself via the PlatformIO
  Git GUI (Claude cannot push).
