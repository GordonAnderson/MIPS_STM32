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
