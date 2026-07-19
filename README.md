# MIPS_STM32

Firmware for the **MIPS Rev 6.0 controller** — representing the migration of GAA Custom Electronics' MIPS modular intelligent power sources from the Arduino Due (Atmel SAM3X8E, Cortex-M3, 84 MHz) to the **STM32H743** (Cortex-M7, running at 440 MHz). Built on **native STM32Cube (HAL/LL)**, not Arduino.

## Status

**Phase 0 complete; Phase 1 in progress.** CubeMX generated into `cubemx/`,
**88 pins** assigned and verified against the `.ioc`. The cooperative scheduler
is written and compiles. The as-built pin, clock and EXTI map is
`docs/MIPS_Rev6_PinMap.md` — **that file is authoritative** where it disagrees
with the older `.docx` design spec or pin-planning worksheet.

> ⚠ **Nothing has run on hardware yet.** The Rev 6.0 board is still being
> designed and the WeAct bring-up board was dropped, so all firmware to date is
> **compile-verified only**.

Hardware design is the current focus — see `docs/Hardware_Design_Checklist.md`.

## Hardware target

**STM32H743ZIT6, LQFP-144** — the custom MIPS Rev 6.0 PCB. The CubeMX project
targets the final part directly; the 100-pin WeAct bring-up board was dropped as
an unnecessary second step (the pin budget does not fit 100 pins once the UI,
module bus and EXT connectors are placed).

Core 440 MHz / HCLK 220 MHz / USB 48.000 MHz. See `docs/MIPS_Rev6_PinMap.md`.

## Key architecture decisions (see `docs/`)

- **No preemptive RTOS.** Bare-metal + cooperative scheduler + a PendSV-driven
  deferred bus-transaction queue. The queue *uses* PendSV, which a preemptive
  RTOS would claim — so the two are mutually exclusive, and the queue is the
  concrete reason bare-metal is correct here. (Firmware Plan §1, §4.)
- **Hybrid LL/HAL.** LL for timing-critical code (the pulse sequence generator);
  HAL for USB, QSPI, and comms housekeeping.
- **GAACE_Core command processor**, pulled from the `stm32` branch on GitHub
  (Arduino-free build). Serial.cpp monolith decomposes into per-module tables.
- **Ethernet = external RS-232-to-Ethernet module on a UART** (original MIPS
  approach). **No** native LwIP/MAC/PHY stack — a large simplification: no LwIP
  libraries, no custom linker script, no RAM_D2 DMA-descriptor placement.
- **Config storage:** GAACE FlashFS on QSPI NOR (replaces DueFlashStorage).
- **Bus I/O naming:** `DO_A`–`DO_P` (outputs), `IN_Q`–`IN_X` (inputs),
  `BIO1`–`BIO7` (seven generic EXT signals, runtime input/output/interrupt).
- **Cooperative scheduler** replaces ArduinoThread and owns SUSPEND plus its own
  GAACE command table — the first module to own its commands.
- **Application code lives in `lib/`**, not `cubemx/`. `main.c` calls
  `appSetup()` / `appLoop()` from USER CODE blocks, so regeneration is safe.
- **USB:** composite CDC (virtual COM) + MSC (thumb drive), HAL in non-RTOS mode.

## Layout

```
platformio.ini      Build config (env mips_rev6_h743zit; framework=stm32cube)
docs/               MIPS_Rev6_PinMap.md            <- AUTHORITATIVE as-built pin/clock/EXTI map
                    Hardware_Design_Checklist.md   <- everything affecting the schematic
                    MIPS_Bus_Signal_Map.md         <- EXT1/EXT2 connectors + BIO signals
                    CubeMX_Regeneration_Notes.md   <- punch list + per-regeneration procedure
                    DIO_Port_Review_and_Plan.md    <- DIO review + revised phase plan
                    (.docx design spec / worksheet / firmware plan - STALE, see PinMap section 6)
lib/
  app/              appSetup() / appLoop() - C++ application entry point
  scheduler/        taskScheduler - cooperative ms scheduler + TASK* commands
  bus_queue/        DeferredQueue + I2CQueue + SPIQueue (PendSV bus layer, HAL)
  pulse_timer/      STM32PulseTimer (TIM2 pulse sequence generator, LL)
  transports/       GUartStream + GUsbCdcStream (GStream HAL transports)
cubemx/             CubeMX-generated project lands here (empty until generated)
TODO.md             Sequenced next steps (CubeMX first)
CLAUDE_HANDOFF.md   Context to resume work in a new session
```

`lib/` modules are auto-discovered by PlatformIO's library dependency finder.
`GAACE_Core` is a `lib_deps` dependency (GitHub `stm32` branch), not vendored.

## Building

`pio run -e mips_rev6_h743zit` builds out of the generated `cubemx/` tree.
Flash/debug via an external ST-Link.

`build_flags` must include **`-D USE_FULL_LL_DRIVER`** — the LL typedefs used by
the generated `tim.c` are behind that macro and PlatformIO does not read
CubeMX's Makefile.

After every CubeMX regeneration, follow `docs/CubeMX_Regeneration_Notes.md` §2.

## Related repositories

- `GordonAnderson/GAACE_Core` (branch `stm32`) — the Arduino-free core library.
- `GordonAnderson/MIPS` — the original Arduino Due firmware (migration source).
