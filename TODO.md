# MIPS_STM32 — TODO / Next Steps

Sequenced to match the phased plan in `docs/MIPS_Rev6_Firmware_Plan.docx`.
Each phase leaves a working system. Do them in order.

---

## Phase 0 — CubeMX project generation  ✅ COMPLETE

Done. Generated into `cubemx/` (Makefile toolchain, per-peripheral .c/.h).
**As-built result: `docs/MIPS_Rev6_PinMap.md`** — that file supersedes the
worksheet and design spec wherever they disagree (see its section 6).

Post-generation checks still to confirm:
- [ ] `SystemClock_Config()` contains `PWR_REGULATOR_VOLTAGE_SCALE0`
- [ ] Delete the generated `PendSV_Handler()` stub in `stm32h7xx_it.c`
- [ ] PA2 emits AF1 config (CH3 generates in frozen mode - may be skipped)
- [ ] `pio run` clean build of the bare generated project

- [x] New CubeMX project. Select the MCU. **Note:** the worksheet targets the
      final **STM32H743ZIT6 (144-pin)**. For the WeAct bring-up board select the
      **STM32H743VIT6 (100-pin)** instead — do the worksheet's *peripheral/clock*
      steps, but expect some pin numbers to differ on the 100-pin package.
- [x] Clock tree: 25 MHz HSE → 480 MHz core, **exact 48 MHz** USB clock, VOS0.
      Confirm rev V silicon (`DBGMCU_IDC` REV_ID = 0x2003); if rev Y, use 400 MHz
      / VOS1 and update `platformio.ini` `board_build.f_cpu`.
- [x] Enable, per the worksheet: debug (SWD), USB OTG FS (device, composite
      CDC+MSC), the UARTs (incl. the AUX2 UART for the **external RS-232-to-
      Ethernet** module — NOT native Ethernet), SPI1/SPI2, I2C1/I2C2, QSPI,
      and the timers (TIM2 pulse gen, TIM1 PWM, TIM8 burst, TIM3/4/5, TIM6/7).
- [x] **Do NOT enable** the Ethernet MAC / LwIP. Ethernet is the external UART
      module. Keeping LwIP off avoids the linker-script / RAM_D2 complexity.
- [x] Driver selector (per-peripheral): **LL** for TIM2 (and the PWM/clock
      timers if desired); **HAL** for USB, QSPI, SPI, I2C, UART. (Hybrid, per
      the firmware plan.)
- [x] Generate as a **Makefile / STM32CubeIDE** project into `cubemx/`, matching
      the `src_dir = cubemx/Core/Src` convention in `platformio.ini`.
- [x] Resolve the 6 pin conflicts flagged in the worksheet appendix as you go.
- [x] Confirm the generated linker-script filename and set
      `board_build.ldscript` in `platformio.ini` if PlatformIO doesn't auto-find
      it. (Stock script — no hand edits needed, since no Ethernet DMA.)
- [x] `pio run -e weact_mini_h743vitx` — get a clean build of the bare generated
      project before adding application code.

## Phase 0b — CubeMX punch list  ✅ COMPLETE

- [x] Enable **RTC** — clock source **LSE** (verified; was defaulting to LSI)
- [x] Add **`VIN_SENSE`** on **PF11** (ADC1_INP2) — 12 V monitor, 30k/10k divider
- [x] Re-enable **USB VBUS sense** on PA9
- [x] Add `LDAC_CTRL` (PA3), `AUX_TRGOUT` (PD15), `TRG_OUT` label on PC6
- [x] Regenerate, run §2.2 checklist, `pio run` clean
- [x] `docs/MIPS_Rev6_PinMap.md` regenerated and **verified against the .ioc** (88 pins)

Open: PG0/PG1 still assigned `RCK_DO_AH`/`RCK_DO_IP`. LDAC latches the 595s, so
decide whether to drop them or keep them as a jumper-selectable latch source.

## Phase 1 — Command processor bring-up (blink + console)  ← IN PROGRESS

- [x] GAACE_Core pulls from the `stm32` branch and compiles. Required fixing the
      four Arduino-only modules (`Devices`, `WireHelper`, `SerialBuffer`,
      `WireServer`) with `#if defined(ARDUINO)` guards — **pushed**.
- [x] `USE_FULL_LL_DRIVER` added to `build_flags` (required or `tim.c` will not
      compile — the LL typedefs are behind that macro).
- [x] **Application entry point established**: `lib/app/app.cpp` + `app.h` with
      `appSetup()` / `appLoop()` called from `main.c` USER CODE blocks. All
      application code is C++ in `lib/`, outside CubeMX's territory.
- [x] **Cooperative scheduler written** — `lib/scheduler/taskScheduler.h/.cpp`.
      Compiles. See "scheduler notes" below.
- [ ] Wire a `GUartStream` (lib/transports) to a console UART; register it with a
      `commandProcessor`. Bring up a minimal system command table.
- [ ] Prove host comms: `GVER`, `GCMDS`, a get/set command, ACK/NAK.
- [ ] Add the `debug` module; provide a project `GHal` (lib/, to write) for its
      pin/analog commands + `readInternalTempRaw()` for `CPUTEMP`.
- [ ] **Hardware test everything below — no board exists yet.** All firmware to
      date is compile-verified ONLY. The WeAct bring-up board was dropped, so
      nothing has run on silicon.
- [ ] Confirm the heartbeat task blinks `LED_ON` (PE8) at 500 ms.
- [ ] Verify `GUartStream(&huart3)` construction — the constructor signature was
      assumed, not checked against `lib/transports/GUartStream.h`.
- [ ] Exercise the scheduler commands: `TASKS`, `TASK`, `TASKENA`, `TASKINT`,
      `RUNNOW`, `RESTART`, `SUSPEND`, `TASKCLR`.

### Scheduler notes (built, untested)

- Runtime measured in **microseconds** via the DWT cycle counter, not ms — task
  bodies mostly run well under 1 ms so ms resolution would report zero.
- **Rev 5.4 command aliases included** (`THREADS`, `THREAD`, `STHRDENA`,
  `STHRDINT`, `THRDRESTART`, `SSPND`) for host compatibility. Clearly marked
  block in `taskScheduler.cpp` — delete if not wanted.
- Resume-from-SUSPEND and re-enable both re-arm the task, otherwise a long-idle
  task fires immediately and every task bunches on one `run()` call.
- Tick comparison is **wrap-safe** (signed subtraction) — survives the
  `HAL_GetTick()` rollover at ~49 days.
- ⚠ **The pulse/table engine must call `sched.isSuspended()` itself.** It runs
  from TIM2, not from `run()`, so `SUSPEND` will not stop it otherwise. In
  Rev 5.4 the global `Suspend` flag gated both.

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

## Phase 5 — DIO first, then display  (REVISED — see docs/DIO_Port_Review_and_Plan.md)

DIO is controller hardware, not an optional module, and its hardware functions
need no display. Bringing it up first validates SPI2 + the 595 chain, all 8 EXTI
inputs, TIM8 burst, both trigger outputs and the LDAC path *before* investing in
~1500 lines of graphics.

- [ ] **5a — DIO core.** DO A–P, DI Q–X, trigger out / aux trigger out, burst,
      pulse counters, mirroring, change reporting, all serial commands. No display.
- [ ] **5b — Display.** ILI9340 driver over HAL SPI1 + a **minimal GFX subset**
      (Menu/Dialog use under ~15 GFX calls; porting all of Adafruit_GFX drags
      Arduino shims in for nothing).
- [ ] **5c — Menu / Dialog.** Mostly portable already — table-driven structs with
      `void*` value pointers; the Arduino dependency is only in the draw calls.
- [ ] **5d — DIO dialogs** re-attached; encoder + `ENC_SW` wired to navigation.

### DIO port notes

- Rev 5.4 uses raw SAM3X PIO registers (`PIO_SODR`/`CODR`/`ODSR`/`PDSR`)
  throughout — maps to STM32 `BSRR`/`ODR`/`IDR`. Wrap in inline helpers.
- `TriggerOut()` busy-waits with `delayMicroseconds()` — 1 ms on `TRIGOUT,PULSE`.
  **Use timer one-pulse mode instead**; the loop keeps running.
- Burst: use TIM8's **repetition counter** — emits exactly N cycles in hardware,
  removing Rev 5.4's `FreqBurstISR` cycle counting.
- `lib/DIhandler` is the Rev 5.4 input-interrupt abstraction. Good API model,
  entirely SAM3X implementation — rewrite against the EXTI dispatch table.
- Arduino `String` use in `TriggerOut(const char*)` must go.

## Phase 6+ — Module migration (DCbias template first)

- [ ] Draft the minimal `Module` base class (begin / registerCommands / loop /
      saveConfig / loadConfig — see CLAUDE_HANDOFF.md "Module architecture").
      **Keep it provisional until DCbias lands** — DIO is not a typical module
      (no board address, no EEPROM, no detection), so shaping the base around it
      alone would omit the very features the base exists to standardise.
- [ ] Migrate DCbias end-to-end onto the core AND the Module base, as the
      reference template. Then refactor the base class based on what DCbias
      actually needed (discover the contract, don't design it in a vacuum).
- [ ] Set up the module registry (Module* modules[]) + uniform boot loop
      (begin / registerCommands / scheduler.addTask(loop)).
- [ ] Then DC/analog family, RF family, timing-critical family, remaining modules
      (firmware plan §8–9). Decompose Serial.cpp into per-module tables as you go.

---

## Standing notes / gotchas

- **Every CubeMX regeneration needs post-generation checks** — see
  `docs/CubeMX_Regeneration_Notes.md` §2. The PendSV stub is the recurring one;
  §2.1 has a permanent fix that removes the manual edit for good.
- **`USE_FULL_LL_DRIVER` must stay in `build_flags`** or `tim.c` will not compile.
- **`lib_ignore = GAACE_Core`** is temporary — remove once the four Arduino-only
  modules are guarded with `#if defined(ARDUINO)`.

- **Ethernet is external UART, not native.** No LwIP, no custom linker script,
  no RAM_D2 descriptor sections. Don't reintroduce that.
- **Bring-up board ≠ final board.** WeAct = 100-pin VIT6; final = 144-pin ZIT6.
  Firmware transfers; final pinout is proven on the custom board.
- **PendSV is owned by the DeferredQueue** (no RTOS). Any future preemptive-RTOS
  proposal must first rework the queue's deferral mechanism.
- **Still-unported GAACE modules** on the `stm32` branch: `Devices`, `WireHelper`,
  `SerialBuffer`, `WireServer`. Port onto that same branch when needed.
