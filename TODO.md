# MIPS_STM32 — TODO / Next Steps

Sequenced to match the phased plan in `docs/MIPS_Rev6_Firmware_Plan.docx`.
Each phase leaves a working system. Do them in order.

---

## Phase 0 — CubeMX project generation  ← YOU ARE HERE (next action)

Run CubeMX and generate into `cubemx/`. Use
`docs/MIPS_Rev6_PinPlanning_Worksheet.docx` as the step-by-step guide.

- [ ] New CubeMX project. Select the MCU. **Note:** the worksheet targets the
      final **STM32H743ZIT6 (144-pin)**. For the WeAct bring-up board select the
      **STM32H743VIT6 (100-pin)** instead — do the worksheet's *peripheral/clock*
      steps, but expect some pin numbers to differ on the 100-pin package.
- [ ] Clock tree: 25 MHz HSE → 480 MHz core, **exact 48 MHz** USB clock, VOS0.
      Confirm rev V silicon (`DBGMCU_IDC` REV_ID = 0x2003); if rev Y, use 400 MHz
      / VOS1 and update `platformio.ini` `board_build.f_cpu`.
- [ ] Enable, per the worksheet: debug (SWD), USB OTG FS (device, composite
      CDC+MSC), the UARTs (incl. the AUX2 UART for the **external RS-232-to-
      Ethernet** module — NOT native Ethernet), SPI1/SPI2, I2C1/I2C2, QSPI,
      and the timers (TIM2 pulse gen, TIM1 PWM, TIM8 burst, TIM3/4/5, TIM6/7).
- [ ] **Do NOT enable** the Ethernet MAC / LwIP. Ethernet is the external UART
      module. Keeping LwIP off avoids the linker-script / RAM_D2 complexity.
- [ ] Driver selector (per-peripheral): **LL** for TIM2 (and the PWM/clock
      timers if desired); **HAL** for USB, QSPI, SPI, I2C, UART. (Hybrid, per
      the firmware plan.)
- [ ] Generate as a **Makefile / STM32CubeIDE** project into `cubemx/`, matching
      the `src_dir = cubemx/Core/Src` convention in `platformio.ini`.
- [ ] Resolve the 6 pin conflicts flagged in the worksheet appendix as you go.
- [ ] Confirm the generated linker-script filename and set
      `board_build.ldscript` in `platformio.ini` if PlatformIO doesn't auto-find
      it. (Stock script — no hand edits needed, since no Ethernet DMA.)
- [ ] `pio run -e weact_mini_h743vitx` — get a clean build of the bare generated
      project before adding application code.

## Phase 1 — Command processor bring-up (blink + console)

- [ ] Confirm GAACE_Core pulls from the `stm32` branch (`lib_deps`) and compiles
      in the Cube project.
- [ ] Wire a `GUartStream` (lib/transports) to a console UART; register it with a
      `commandProcessor`. Bring up a minimal system command table.
- [ ] Prove host comms: `GVER`, `GCMDS`, a get/set command, ACK/NAK.
- [ ] Add the `debug` module; provide a project `GHal` (lib/, to write) for its
      pin/analog commands + `readInternalTempRaw()` for `CPUTEMP`.

## Phase 2 — Bus transaction layer

- [ ] Bring up `DeferredQueue`/`I2CQueue`/`SPIQueue` (lib/bus_queue) on HAL I2C/SPI.
- [ ] Wire the single shared `PendSV_Handler` + one-time NVIC priority set.
      **Critical:** ensure CubeMX's generated `stm32h7xx_it.c` does not also
      define `PendSV_Handler` (see docs/port_notes/DeferredQueue_H7_PortNotes.md).
- [ ] Scope-verify deferred draining; run the module-EEPROM I2C scan through it.

## Phase 3 — Storage + USB MSC

- [ ] FlashFS on QSPI (W25Q80DV board config) for MIPS config save/restore.
- [ ] USB composite: confirm CDC (console) + MSC (SD on display module) enumerate.

## Phase 4 — Timer subsystem / pulse engine

- [ ] Bring up `STM32PulseTimer` (lib/pulse_timer) on TIM2. Pass the real TIM2
      kernel clock into `begin()`. Route CH1 to an LDAC pin; wire ETR for trigger.
- [ ] **Scope-validate** immediate mid-period compare + passed-compare hazard
      against a real long-period / short-pulse sequence (this is the key risk).
- [ ] Bring up TIM1 4-ch PWM; confirm 12-bit at 50 kHz (needs ≥204.8 MHz clock).

## Phase 5+ — Module migration (DCbias template first)

- [ ] Migrate DCbias end-to-end onto the core as the reference template.
- [ ] Then DC/analog family, RF family, timing-critical family, remaining modules
      (firmware plan §8–9). Decompose Serial.cpp into per-module tables as you go.

---

## Standing notes / gotchas

- **Ethernet is external UART, not native.** No LwIP, no custom linker script,
  no RAM_D2 descriptor sections. Don't reintroduce that.
- **Bring-up board ≠ final board.** WeAct = 100-pin VIT6; final = 144-pin ZIT6.
  Firmware transfers; final pinout is proven on the custom board.
- **PendSV is owned by the DeferredQueue** (no RTOS). Any future preemptive-RTOS
  proposal must first rework the queue's deferral mechanism.
- **Still-unported GAACE modules** on the `stm32` branch: `Devices`, `WireHelper`,
  `SerialBuffer`, `WireServer`. Port onto that same branch when needed.
