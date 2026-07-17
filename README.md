# MIPS_STM32

Firmware for the **MIPS Rev 6.0 controller** — the migration of GAA Custom
Electronics' MIPS mass-spectrometry / ion-processing controller from the
Arduino Due (Atmel SAM3X8E, Cortex-M3, 84 MHz) to the **STM32H743**
(Cortex-M7, 480 MHz), built on **native STM32Cube (HAL/LL)** — not Arduino.

## Status

Project skeleton. CubeMX has **not** been run yet — that's the next step
(see `TODO.md`). The reusable firmware pieces developed ahead of CubeMX are
already here under `lib/`, and the full design/planning docs are under `docs/`.

## Hardware targets

| | Board | Part | Pins | Role |
|---|---|---|---|---|
| Bring-up | WeAct MiniSTM32H743VITX | STM32H743**VIT6** | 100 | De-risk firmware on real H7 silicon |
| Final | Custom MIPS Rev 6.0 PCB | STM32H743**ZIT6** | 144 | Production controller |

Firmware validated on the WeAct board transfers to the custom board (same
silicon family/peripherals); the **final pinout** (per
`docs/MIPS_Rev6_PinPlanning_Worksheet.docx`, which targets the 144-pin ZIT6) is
proven later on the custom board.

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
- **USB:** composite CDC (virtual COM) + MSC (thumb drive), HAL in non-RTOS mode.

## Layout

```
platformio.ini      Build config (WeAct board; framework=stm32cube; GAACE_Core via git)
docs/               Design spec, pin-planning worksheet, firmware plan, port notes
lib/
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

Not yet buildable — `cubemx/` is empty. After running CubeMX (see `TODO.md`),
`pio run -e weact_mini_h743vitx` builds out of the generated tree. Flash/debug
via an external ST-Link (the WeAct board has no onboard probe).

## Related repositories

- `GordonAnderson/GAACE_Core` (branch `stm32`) — the Arduino-free core library.
- `GordonAnderson/MIPS` — the original Arduino Due firmware (migration source).
