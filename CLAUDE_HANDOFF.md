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

1. **MCU:** STM32H743ZIT6 (144-pin). The WeAct 100-pin bring-up board was
   DROPPED — the pin budget does not fit 100 pins, and it was an unnecessary
   second step. Rev V silicon, VOS0, **440 MHz** (not 480 — CubeMX enforces a
   derated AXI ceiling ~225 MHz; HCLK 220, timer clocks 220).
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

## Cooperative scheduler — SETTLED DESIGN (build fresh, do not port ArduinoThread)

Decision: write a **fresh minimal cooperative scheduler**, not port ArduinoThread.
Rationale: MIPS's task-management commands are genuinely useful and must be kept,
and integrating them with the scheduler (as a first-class feature, using the
GAACE command processor) is cleaner than MIPS's current approach (dumb scheduler
+ scattered free-functions in Serial.cpp). This becomes the first example of a
module owning its own command table — directly serving the Serial.cpp
decomposition goal. Aim: a clean solution that could replace ArduinoThread.

**Granularity:** millisecond only (via HAL_GetTick()). No sub-ms — the real-time-
critical work lives in hardware timers (STM32PulseTimer), not the scheduler.

**What MIPS does today (the behavior to preserve):** every module does
`thread.onRun(cb); thread.setInterval(100); control.add(&thread);` at init, and
the main loop runs `if (!Suspend) control.run();`. Threads are added once at boot,
never removed — only enabled/disabled. Task-management commands (THREADS / THREAD
/ STHRDENA / STHRDINT / RUNNOW / THRDRESTART / SSPND) let you inspect and control
the running scheduler over serial. THRDRESTART staggers restarts by 25ms×index.

**New scheduler — per-task metadata:**
- name, ID (assigned at add — centralize this, don't leave it ordering-implicit),
  interval (ms), enabled, callback
- last runtime, **max runtime**, **run count** (all three kept/added — cheap and
  diagnostic; runtime is explicitly required)
- **overrun flag** when a task's runtime exceeds its own interval (the single most
  useful health signal in a cooperative system — a task running longer than its
  period is starving the loop)

**New scheduler — API:**
- `addTask(name, callback, intervalMs)` → returns handle/ID (replaces the clunky
  3-line onRun/setInterval/add dance)
- `run()` — called from the main loop; runs due tasks, measures runtime
- `suspendAll(bool)` — **the scheduler OWNS suspend** (first-class), not an
  external main-loop flag. SUSPEND becomes a command in the scheduler's own table.
- `registerCommandTable(commandProcessor&)` — scheduler registers its OWN GAACE
  CommandList; the task-management commands travel with the scheduler as one unit

**Commands (scheduler owns this GAACE table):** TASKS (list: name,ID,interval,
enabled,last runtime,max runtime,count), TASK <name> (details), TASKENA <name>,<T/F>,
TASKINT <name>,<ms> (1–10000), RUNNOW <name>, RESTART (staggered 25ms×index),
SUSPEND <T/F>. (Names are suggestions; keep the MIPS command spellings if host-
compat matters — confirm with Gordon.)

**Missed-deadline policy (make explicit):** run-once-and-reschedule-from-now
(drift-tolerant), NOT catch-up/fixed-rate. Avoids a spiral-of-death if the loop
stalls. This is effectively what MIPS does via setNextRunTime(millis()+interval).

**Deliberately OUT of scope:** priorities (cooperative has no real priorities —
would imply preemption we don't have), dynamic task deletion (tasks live for the
program; enable/disable is enough), sub-ms intervals.

**⚠ Suspend subtlety (do not lose):** in MIPS, `Suspend` also gates *table-mode
real-time control*, not just the cooperative threads (Table.cpp / pulse engine).
Since suspend now lives IN the scheduler, the table/pulse engine still needs to
honor suspend — it is NOT a cooperative task, so it must check the scheduler's
suspend state (or the scheduler exposes an `isSuspended()` the pulse/table code
queries). Make sure real-time control still stops on SUSPEND.

**Build it in the fresh chat** (CubeMX gives the real HAL_GetTick environment);
this is designed but not yet written.

## Module architecture — base class / template (SETTLED DIRECTION)

Decision: **yes, each hardware module becomes a class, and a lean `Module` base
class defines the lifecycle contract.** But the base is an *interface*, not a
framework — its value is enforcing a uniform contract and centralizing lifecycle,
NOT sharing implementation code.

**Why classes (vs. today's .cpp-of-globals + free functions):**
- Encapsulate per-module state — kills the `esi/esich/esidata` triple-bookkeeping
  disease (see MIPS TODO) that module-as-bag-of-globals breeds.
- Natural owner of the module's GAACE CommandList (`Module::registerCommands(cp)`)
  — the seam for Serial.cpp decomposition.
- Clean instance-per-detected-board, enabling the dynamic per-module allocation
  the TODO wants (vs. today's static arrays).

**The base class (lean, near-pure-virtual):**
```cpp
class Module {
public:
  virtual void begin() = 0;                              // hw init, detect board
  virtual void registerCommands(commandProcessor&) = 0;  // add my command table
  virtual void loop() = 0;                               // scheduler task body
  virtual bool saveConfig() = 0;                         // FlashFS
  virtual bool loadConfig() = 0;
  const char* name() const;
  // maybe: bool present(); uint8_t boardAddr();
};
```
Gives a **registry pattern**: `Module* modules[]`; boot = "for each detected
module: begin(); registerCommands(cp); scheduler.addTask(name, ()->loop(), interval)."
Replaces the 41 scattered `control.add(&XThread)` + init calls with ONE uniform
loop. The module's `loop()` is exactly what the new scheduler's addTask registers;
its `registerCommands()` is exactly the Serial.cpp-decomposition seam.

**Guardrails (do NOT over-engineer):**
- **No deep inheritance.** Resist `Module→AnalogModule→DCBiasLike→DCbias`. The
  modules (DCbias, RF, FAIMS, ARB, Twave) are genuinely heterogeneous; a deep
  hierarchy to share a few lines creates fragile cross-coupling. ONE shallow base
  + **composition**: shared mechanism (DeferredQueue, FlashFS, config-blob helper)
  are members/utilities modules USE, not superclasses.
- **Interface, not framework.** Keep it near-pure-virtual. Real behavior that
  subclasses must override-around is the smell.
- **Embedded C++ discipline:** no RTTI, no exceptions. Virtual dispatch for
  once-per-loop module ticks is fine (negligible). Keep virtuals OUT of the
  pulse-timer hot path (those stay direct/free-function calls).

**⚠ Design the base class by DISCOVERY, not up front.** Do NOT fully design the
abstract base in a vacuum — you'll guess wrong about what belongs in it. Sequence:
1. Draft the minimal `Module` interface (the ~6 virtuals above).
2. Migrate **DCbias** onto it concretely (Phase 5).
3. Refactor the base based on what DCbias actually needed — now it's proven.
4. That validated base + DCbias become the template modules 6–41 follow.
This is the real reason DCbias-as-template (Phase 5) is the linchpin.

## Read these first

| Doc | What it is |
|---|---|
| `docs/MIPS_Rev6_PinMap.md` | **AUTHORITATIVE** as-built pin/clock/EXTI map, verified against the .ioc (88 pins) |
| `docs/Hardware_Design_Checklist.md` | Everything affecting the Rev 6.0 schematic |
| `docs/MIPS_Bus_Signal_Map.md` | EXT1/EXT2 connectors, `BIO1`-`BIO7` generic signals |
| `docs/CubeMX_Regeneration_Notes.md` | Punch list + what to check after every regeneration |
| `docs/DIO_Port_Review_and_Plan.md` | DIO review, revised phase plan |

The three `.docx` files are **stale** — see `MIPS_Rev6_PinMap.md` §6 for the list
of what they get wrong (FreeRTOS, 480 MHz, LDAC on CH1/PA0, narrow-pulse LDAC,
TIM3 as a clock output, I2C2 on PB10/PB11).

## Current state

**Phase 0 complete. Phase 1 in progress. Hardware design is the active work.**

- CubeMX generated (Makefile toolchain, per-peripheral .c/.h), 88 pins verified.
- GAACE_Core `stm32` branch builds in pure Cube. Needed `#if defined(ARDUINO)`
  guards on `Devices`, `WireHelper`, `SerialBuffer`, `WireServer` — pushed.
- Application entry point: **`lib/app/app.cpp`** with `appSetup()`/`appLoop()`
  called from `main.c` USER CODE blocks. All application code is C++ under
  `lib/`, so CubeMX regeneration never touches it.
- **Cooperative scheduler written** — `lib/scheduler/taskScheduler.{h,cpp}`.

> ⚠ **NOTHING HAS RUN ON HARDWARE.** The Rev 6.0 board does not exist yet and the
> WeAct bring-up board was dropped. Everything is **compile-verified only**.
> Treat all firmware as unproven until the board arrives.

Two known loose ends:
- `GUartStream(&huart3)` — the constructor signature was assumed, not verified
  against `lib/transports/GUartStream.h`.
- The `DeferredQueue_PendSV()` call in `stm32h7xx_it.c` is declared but the
  library still defines `PendSV_Handler`. Rename it in `bus_queue` at Phase 2
  (see `CubeMX_Regeneration_Notes.md` §2.1) — that removes the recurring
  post-regeneration edit permanently.

## Immediate next action

Finish the Rev 6.0 hardware design — `docs/Hardware_Design_Checklist.md` §8
lists the open items. Three need Gordon specifically: the IC11 gate/pulse
circuit intent (§2.2.6), the 3.3 V power architecture (§11.2.2), and whether the
Q-X inputs get an RC deglitch (the STM32 has **no** per-pin glitch filter, unlike
the SAM3X, so this is hardware-only and cannot be fixed in firmware later).

Then Phase 1 hardware bring-up, then **DIO first** (not DCbias) per the revised
plan in `docs/DIO_Port_Review_and_Plan.md`.

## Decisions made during the CubeMX session (do not relitigate)

- **440 MHz core**, not 480 — derated AXI/AHB ceiling. TIM1/TIM2 kernel clock is
  **220 MHz**; pass 220000000 into `STM32PulseTimer::begin()`.
- **LDAC is a TOGGLE, not a shaped pulse.** The board has an edge detector that
  fires a pulse on every edge. LDAC is **TIM2_CH3 on PA2** — not CH1, because on
  TIM2 CH1 and ETR are the same silicon resource and ETR is needed for R.
  CubeMX generates CH3 in *frozen* mode deliberately; the LL driver sets
  toggle + preload-off in begin(). PA2 must also be runtime-switchable to plain
  GPIO output.
- **Q and R are TIM2 signals**: Q = external clock (PB3, CH2), R = external
  trigger (PA0, ETR, slave reset). They are NOT the same nets as EXT1 pins 7/8.
- **EXT1 pins 5,6,7,8,13,14 + EXT2 pin 14 are now generic `BIO1`–`BIO7`** —
  runtime configurable as input, output or interrupt.
- **EXTI is the tight resource.** 16 lines total, 15 needed (IN_Q–IN_X + BIO1-7),
  all on distinct pin numbers. Lines 5–9 and 10–15 share NVIC vectors → ONE
  line-indexed dispatch table. EXTI configured in firmware, not CubeMX.
- **Encoder uses TIM3 hardware quadrature decode** (PB4/PB5) — no EXTI, no missed
  counts. ENC_SW (PD1) is polled. This is what makes 15 signals fit in 16 lines.
- **TIM2 is the only LL peripheral**; everything else is HAL.
- Digital outputs A–P are **74HC595 bit positions, not STM32 pins**.
- **The DO 595s are latched by LDAC**, not separate RCK lines. This puts digital
  outputs under the pulse sequence generator. Consequence: EVERY LDAC edge
  latches the shift registers, including DAC-only edges, so the 595 contents
  must always be valid and the SPIQueue must order the shift BEFORE any
  pulse-engine LDAC edge. This is a real race, not a theoretical one.
- `LDAC_CTRL` (PA3) overrides the on-board edge detector for software-generated
  LDAC — carried over from Rev 5.4's `LDACcapture`/`LDACrelease`.
- **`TRG_OUT` (PC6/TIM8_CH1) and the burst generator are the same net.** Both
  trigger outputs are **inverted in hardware**; host software depends on it.
- **RTC on LSE** with VBAT battery backup — gives timestamps plus 32 retained
  backup registers.
- **DIO is the first module**, not DCbias. But DIO is not a typical module (no
  board address, no EEPROM, no detection), so keep the `Module` base class
  PROVISIONAL until DCbias lands.

## Scheduler — built, untested

`lib/scheduler/taskScheduler.{h,cpp}`, in GAACE_Core house style (class +
module-level statics + `CommandList`, matching `debug`).

- Runtime in **microseconds** via DWT, not ms — task bodies mostly run under 1 ms.
- Commands: `TASKS`, `TASK`, `TASKENA`, `TASKINT`, `RUNNOW`, `RESTART`,
  `SUSPEND`, `TASKCLR`, plus **Rev 5.4 aliases** (`THREADS`, `THREAD`,
  `STHRDENA`, `STHRDINT`, `THRDRESTART`, `SSPND`) for host compatibility —
  a clearly marked block that can be deleted.
- Resume-from-suspend and re-enable both **re-arm** the task, or everything
  bunches onto one `run()` call.
- Tick comparison is **wrap-safe** — survives the ~49-day `HAL_GetTick()` rollover.
- ⚠ **The pulse/table engine must query `isSuspended()` itself** — it runs from
  TIM2, not from `run()`, so SUSPEND will not stop it otherwise.

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
