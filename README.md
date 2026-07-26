# MIPS_STM32

Firmware for the **MIPS Rev 6.0 controller** — the migration of GAA Custom
Electronics' MIPS mass-spectrometry / ion-processing controller from the
Arduino Due (Atmel SAM3X8E, Cortex-M3, 84 MHz) to the **STM32H743**
(Cortex-M7, 440 MHz), built on **native STM32Cube (HAL/LL)** — not Arduino.

## Status

**Phase 0 complete; Phase 1 in progress.** CubeMX generated into `cubemx/`,
88 pins assigned and verified against the `.ioc`. The cooperative scheduler is
written and compiles. The as-built pin, clock and EXTI map is
`docs/MIPS_Rev6_PinMap.md` — **that file is authoritative**.

**Hardware design is the current focus, and it is close.** The remaining work to
close out the Rev 6.0 board is a single burn-down list:
`docs/Schematic_CubeMX_Reconciliation.md` — bring the schematic, the CubeMX
`.ioc`, and the pin map into agreement (one CubeMX regeneration, a few net
connections on the MCU sheet, and the VBUS/PA9 decision).

> ⚠ **Nothing has run on hardware yet.** The Rev 6.0 board is still being
> finished and the WeAct bring-up board was dropped, so all firmware to date is
> **compile-verified only**.

## Hardware target

**STM32H743ZIT6, LQFP-144** — the custom MIPS Rev 6.0 PCB. The CubeMX project
targets the final part directly.

Core 440 MHz / HCLK 220 MHz / USB 48.000 MHz. See `docs/MIPS_Rev6_PinMap.md`.

## Key architecture decisions (see `docs/`)

- **No preemptive RTOS.** Bare-metal + cooperative scheduler + a PendSV-driven
  deferred bus-transaction queue. The queue *uses* PendSV, which a preemptive
  RTOS would claim — so the two are mutually exclusive, and the queue is the
  concrete reason bare-metal is correct here.
- **Hybrid LL/HAL.** LL for timing-critical code (the pulse sequence generator);
  HAL for USB, QSPI, and comms housekeeping.
- **GAACE_Core command processor**, pulled from the `stm32` branch on GitHub
  (Arduino-free build). Serial.cpp monolith decomposes into per-module tables.
- **Ethernet = external RS-232-to-Ethernet module on a UART** (original MIPS
  approach). **No** native LwIP/MAC/PHY stack — a large simplification.
- **Config storage:** GAACE FlashFS on QSPI NOR (replaces DueFlashStorage).
- **Bus I/O naming:** `DO_A`–`DO_P` (outputs), `IN_Q`–`IN_X` (inputs),
  `BIO1`–`BIO7` (seven generic EXT signals, runtime input/output/interrupt).
- **Digital outputs latched via LDAC edge detector.** The CPU drives
  `LDAC_TOGGLE` (PA2); an on-board edge detector generates `LDAC`, which latches
  the DAC and the 595 shift registers. A single `OE_DO_AP` enables all outputs
  A–P; `DOSR_CLR` clears the chain; `RCK_DO_IP` is a jumper-selectable latch
  source. (Details: `docs/Hardware_Design_Checklist.md §3`.)
- **Cooperative scheduler** replaces ArduinoThread and owns SUSPEND plus its own
  GAACE command table.
- **Application code lives in `lib/`**, not `cubemx/`. `main.c` calls
  `appSetup()` / `appLoop()` from USER CODE blocks, so regeneration is safe.
- **USB:** composite CDC (virtual COM) + MSC (thumb drive), HAL in non-RTOS mode.

## Layout

```
platformio.ini      Build config (env mips_rev6_h743zit; framework=stm32cube)

docs/               MIPS_Rev6_PinMap.md                <- AUTHORITATIVE pin / clock / peripheral / EXTI map
                    MIPS_Bus_Signal_Map.md             <- EXT1/EXT2 connector pinouts + BIO signals
                    Hardware_Design_Checklist.md       <- schematic-affecting decisions + open items
                    Power_Supply_Design.md             <- power train: buck / TPS2116 mux / LDOs / REF3033
                    CubeMX_Regeneration_Notes.md       <- the every-regeneration procedure
                    Schematic_CubeMX_Reconciliation.md <- PUNCH LIST to finish the Rev 6.0 design
                    DIO_Port_Review_and_Plan.md        <- DIO port review + revised phase plan
                    TODO.md                            <- sequenced firmware phase plan
                    port_notes/
                        DeferredQueue_H7_PortNotes.md   <- bus-queue (PendSV) port notes
                        STM32PulseTimer_NOTES.md        <- TIM2 pulse-engine port notes

CLAUDE_HANDOFF.md   Firmware-design reference: settled scheduler design + Module base-class direction

lib/
  app/              appSetup() / appLoop() - C++ application entry point
  scheduler/        taskScheduler - cooperative ms scheduler + TASK* commands
  bus_queue/        DeferredQueue + I2CQueue + SPIQueue (PendSV bus layer, HAL)
  pulse_timer/      STM32PulseTimer (TIM2 pulse sequence generator, LL)
  transports/       GUartStream + GUsbCdcStream (GStream HAL transports)

cubemx/             CubeMX-generated project lands here
```

`lib/` modules are auto-discovered by PlatformIO's library dependency finder.
`GAACE_Core` is a `lib_deps` dependency (GitHub `stm32` branch), not vendored.

> **Document authority.** `MIPS_Rev6_PinMap.md` wins on any pin/clock question.
> The earlier `.docx` design spec, pin-planning worksheet and firmware plan have
> been **removed** — their few still-relevant corrections are captured in
> `MIPS_Rev6_PinMap.md §6`.

## Building

`pio run -e mips_rev6_h743zit` builds out of the generated `cubemx/` tree.
Flash/debug via an external ST-Link.

`build_flags` must include **`-D USE_FULL_LL_DRIVER`** — the LL typedefs used by
the generated `tim.c` are behind that macro and PlatformIO does not read
CubeMX's Makefile.

After every CubeMX regeneration, follow `docs/CubeMX_Regeneration_Notes.md §2`.

## Related repositories

- `GordonAnderson/GAACE_Core` (branch `stm32`) — the Arduino-free core library.
- `GordonAnderson/MIPS` — the original Arduino Due firmware (migration source).
