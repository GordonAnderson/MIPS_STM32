# DIO Port Review + Plan Update

Review of `github.com/GordonAnderson/MIPS` (Rev 5.4 Arduino Due firmware) for the
Rev 6.0 STM32 port, focused on DIO as the first module, plus display and
menu/dialog. Read alongside `MIPS_Rev6_PinMap.md`.

---

## 1. Pin map gaps found — action required

### 1.1 TRGOUT / AUXTRGOUT are missing

`include/Hardware.h`:

```c
#define TRGOUT      A10   // Trigger output, this signal is inverted on controller
#define AUXTRGOUT   A9    // Aux Trigger output, this signal is inverted on controller
```

Neither exists in the Rev 6.0 pin map. Both are **controller-local** signals, so
they were missed by a pin-planning pass driven from the EXT/J connector tables.

**Both are inverted in hardware** — `TRIGOUT,HIGH` writes the pin LOW. Preserve
that inversion (or invert in hardware and document the change), because host
software depends on the existing polarity.

### 1.2 TRGOUT must be timer-capable — it IS the burst output

`src/Hardware.cpp`:

```c
MIPStimer FreqBurst(TMR_TrigOut);   // Variants.h: "generate freq source on trigger output line"
```

So TRGOUT serves three modes:

| Mode | Driven by | Command |
|---|---|---|
| Static level | GPIO | `TRIGOUT,HIGH` / `,LOW` |
| Single pulse (µs) | GPIO + delay (today) | `TRIGOUT,<uS>` |
| Frequency burst of N cycles | **timer** | `BURST,<n>` at `SFREQ` |

**Recommendation: TRGOUT = PC6 (TIM8_CH1).** The pin map currently lists PC6 as
"burst clock" as if it were a separate signal — it is not, it is the same net.
Assigning TRGOUT there gives the timer path for free and needs no new pin.

**AUXTRGOUT** is a plain GPIO pulse (`AuxTrigger()` uses `PulseWidth`), so any
free pin works — suggest **PD15** or **PD3** (both spare).

Same runtime AF↔GPIO mode-switch pattern as LDAC (PA2) applies to TRGOUT.

### 1.3 LDAC capture/release already exists in Rev 5.4

```c
#define LDACrelease  {pinMode(LDACctrl,INPUT);}
#define LDACcapture  {pinMode(LDACctrl,OUTPUT); digitalWrite(LDACctrl,HIGH);}
```

This is exactly the runtime "make LDAC a GPIO output" capability requested for
Rev 6.0 — it is not a new feature, it is an existing one to carry across. On
Rev 6.0 it is PA2 switching between `GPIO_AF1_TIM2` and `GPIO_MODE_OUTPUT_PP`.

### 1.4 Question — DO latch: RCK or LDAC?

`SetImageRegs()` does:

```c
DOrefresh;    // DigitalOut(DOmsb, DOlsb) -> SPI to shift registers
PulseLDAC;    // then pulses LDAC
```

So on Rev 5.4 the digital-output latch appears to be **LDAC-driven**. But the
Rev 6.0 pin map defines separate `RCK_DO_AH` (PG0) and `RCK_DO_IP` (PG1) latch
lines. **Confirm which Rev 6.0 uses** — if the RCK lines latch the 595s, the
`PulseLDAC` in `SetImageRegs()` drops out and DO no longer touches LDAC at all
(which is cleaner: DO updates stop disturbing DAC timing).

---

## 2. What DIO.cpp actually contains

Not just digital I/O — seven subsystems in 797 lines:

| # | Subsystem | Notes |
|---|---|---|
| 1 | 16 digital outputs A–P | image registers (`DOmsb`/`DOlsb`) → SPI shift registers |
| 2 | 8 digital inputs Q–X | **polled** every 100 ms in `DIO_loop` |
| 3 | Trigger out / Aux trigger out | `TriggerOut()`, `AuxTrigger()`, `TRIGOUT`/`AUXOUT` |
| 4 | Pulse counter | input ISR, count threshold, auto-trigger/reset |
| 5 | DIO mirroring + change reporting | `DIOmirror`, `DIOreport`, `DIOmonitor`, `DIOchangeReport` |
| 6 | `TriggerFollowS` | S input directly drives TRGOUT from an ISR |
| 7 | Queued trigger functions | 5-slot function-pointer list played after each trigger |

Plus two dialog boxes (DI and DO) wired into the menu — the display coupling.

**Gordon is right that this is the correct first module**: it is controller
hardware, not an optional board, and bringing it up exercises SPI2 + the 595
chain, the Q–X inputs and their EXTI paths, TIM8, the trigger outputs, and the
LDAC/latch mechanism. That is most of the controller validated in one module.

---

## 3. Concrete porting issues

### 3.1 Raw SAM3X PIO register access — pervasive

```c
static Pio *pio = g_APinDescription[TRGOUT].pPort;
pio->PIO_SODR = pin;    // set
pio->PIO_CODR = pin;    // clear
dwReg = pio->PIO_ODSR;  // read output state
pioS->PIO_PDSR & pinS   // read input
```

Maps directly to STM32: `GPIOx->BSRR = pin` (set), `GPIOx->BSRR = pin<<16`
(clear), `GPIOx->ODR` (read output), `GPIOx->IDR` (read input). Mechanical, but
it appears in `TriggerOut`, `AuxTrigger`, `FollowSisr`, `TriggerFollowS`.

Worth wrapping in small inline helpers rather than scattering register writes.

### 3.2 `delayMicroseconds()` blocks the cooperative loop

`TriggerOut(int microSec)` busy-waits for the pulse width; the `PULSE` command
uses a **1000 µs** delay. In a cooperative scheduler that is 1 ms of dead loop,
and it happens inside a command handler.

**Improve on the port:** generate trigger pulses with a timer in **one-pulse
mode** — set width, fire, return immediately. The hardware makes the pulse while
the loop keeps running. This also makes the pulse width accurate rather than
interrupt-jitter dependent. TIM8 is already there for TRGOUT.

Keep the blocking path only as a fallback for very short pulses where setup
overhead exceeds the pulse.

### 3.3 Polled inputs → EXTI

`DIO_loop` polls all 8 inputs every 100 ms with `digitalRead`. Rev 6.0 gives all
eight their own EXTI line with programmable edge sensitivity.

- Keep the 100 ms poll for **display refresh** (cheap, no latency requirement).
- Move **change detection**, **pulse counting** and **mirroring** to EXTI — they
  currently either poll or use the `DIhandler` ISR layer.

`lib/DIhandler` is the Rev 5.4 abstraction over the 8 input interrupts (attach /
detach / priority / mode). Its API is a reasonable model for the Rev 6.0 bus-I/O
interrupt layer, but the implementation is entirely SAM3X (`Pio`, `IRQn_Type`
arrays). **Rewrite against the EXTI dispatch table**, keeping a similar API.

### 3.4 Arduino `String` must go

```c
String Cmd; Cmd = cmd; uS = Cmd.toInt();
```

GAACE_Core already removed `String` in favour of `strtol`/`sscanf`. Same here.

### 3.5 Burst generation gets simpler in hardware

Rev 5.4 counts cycles in software:

```c
void FreqBurstISR() { ... if(CurrentCount >= BurstCount) FreqBurst.stop(); }
```

STM32 advanced timers have a **repetition counter (RCR)** — load N, start, and
the timer emits exactly N cycles then stops, no ISR. TIM8 is an advanced timer
and already allocated. This removes an ISR and the associated jitter/race.

### 3.6 `DOrefresh` / `PulseLDAC` macros

`DigitalOut(msb,lsb)` → SPI2 write of two bytes + latch. Should go through the
`SPIQueue`/`DeferredQueue` rather than a direct blocking SPI call, so DO updates
do not contend with the TFT on SPI1... (note: DO is SPI2, TFT is SPI1 — they are
already separated, which was the point of splitting the buses).

---

## 4. Display / menu / dialog assessment

| Component | Size | Dependency |
|---|---|---|
| `Menu.cpp` | 400 lines | Adafruit_GFX |
| `Dialog.cpp` | 559 lines | Adafruit_GFX |
| `Adafruit_ILI9340` | library | Adafruit_GFX + Arduino SPI |
| `Adafruit_GFX_Library` | library | Arduino (`Print`, `pgmspace`) |

The Menu/Dialog code itself is **mostly portable** — it is table-driven structs
(`MenuEntry`, `DialogBoxEntry`) with `void*` value pointers and function-pointer
callbacks. No Arduino dependency in the logic; the dependency is entirely in the
drawing calls into GFX.

**The real work is the graphics layer**, in order of effort:

1. **ILI9340 driver over HAL SPI1** — small: init sequence, `setAddrWindow`,
   pixel/rect/fill writes. Mechanical to port from the Adafruit driver.
2. **GFX layer** — text rendering, fonts, primitives. Either port Adafruit_GFX
   (it needs `Print` and `pgmspace` shims — GAACE_Core already has `GStream`
   which provides the `Print` equivalent), or write a minimal replacement, since
   Menu/Dialog only use a small subset: `fillScreen`, `drawRect`, `fillRect`,
   `setCursor`, `setTextColor`, `setTextSize`, `print`.
3. **Menu/Dialog** — port largely as-is once the GFX subset exists.

**Recommendation:** implement a **minimal GFX subset** rather than porting
Adafruit_GFX wholesale. Inventory the calls Menu/Dialog actually make first —
likely under 15 functions. That avoids dragging Arduino compatibility shims into
a clean Cube project for features that are never used.

The dialogs are also where the `esi/esich/esidata` triple-bookkeeping disease in
the TODO comes from — dialog entries point at parallel display copies of module
state. The Module-class refactor should let dialogs point at the module's own
members instead.

---

## 5. Proposed phase plan

Gordon's proposed order is scheduler → display → menu/dialog → DIO. One
adjustment recommended: **split DIO into hardware-first and display-later.**

### Rationale

DIO's hardware functions (DO, DI, triggers, burst, counters) need **no display**
— they are all reachable over serial commands. Bringing DIO hardware up
immediately after the scheduler validates SPI2 + the 595 chain, all 8 EXTI
inputs, TIM8 burst, both trigger outputs and the LDAC/latch path **before**
investing in ~1500 lines of graphics work. If something is wrong in the pin map
or the bus queue, that is when to find out.

It also means the display work has a real, working module to display.

### Revised sequence

| Phase | Work | Depends on |
|---|---|---|
| **1** | Command processor, `GUartStream` on USART3, **cooperative scheduler** | — |
| **2** | Bus queue (`DeferredQueue`/`I2CQueue`/`SPIQueue`) + PendSV | 1 |
| **2b** | **Bus I/O layer** — BIO1-7 + Q–X, EXTI dispatch table, runtime mode switching | 2 |
| **3** | Storage (FlashFS on QSPI) + USB CDC/MSC | 2 |
| **4** | Timer subsystem — `STM32PulseTimer` on TIM2, TIM1 PWM, **TIM8 burst** | 2 |
| **5a** | **DIO core** — DO A–P, DI Q–X, triggers, burst, pulse counter, mirroring, all serial commands. **No display.** | 2b, 4 |
| **5b** | ILI9340 driver over SPI1 + minimal GFX subset | 2 |
| **5c** | Menu / Dialog framework port | 5b |
| **5d** | DIO dialogs re-attached; encoder + `ENC_SW` wired to menu navigation | 5a, 5c |
| **6** | DCbias — first *module board*, `Module` base class validation | 5d |
| **7+** | Remaining modules per firmware plan §8–9 | 6 |

### On the `Module` base class

The handoff says to discover the base class contract via DCbias. DIO landing
first does not change that, because **DIO is not a typical module**: no board
address, no EEPROM config, no `present()` detection, no per-board instances. It
is fixed controller hardware.

**Recommendation:** implement DIO as a class with the same lifecycle shape
(`begin` / `registerCommands` / `loop` / `saveConfig` / `loadConfig`) but treat
the `Module` base as **provisional** until DCbias lands. DIO proves the
lifecycle and the command-table-per-module seam; DCbias proves board detection,
addressing and per-board config. The base class should be refactored after
**both**, not after DIO alone — otherwise it gets shaped by a module that lacks
the very features the base exists to standardise.

---

## 6. Answers — decisions taken

| # | Question | Decision |
|---|---|---|
| 1 | DO latch: RCK or LDAC? | **LDAC** — puts DO under the pulse sequence generator. PG0/PG1 freed. Plus `LDAC_CTRL` (PA3) to override the edge detector for software-generated LDAC. |
| 2 | TRGOUT = PC6/TIM8_CH1? | **Yes** — same net as the burst output. |
| 3 | AUXTRGOUT pin | **PD15**, plain GPIO, pulse width timed by **TIM7**. Every free timer-capable pin shared a timebase with something needing its own rate (PD15/TIM4_CH4 vs the continuous clock on PD14; PA3/TIM5_CH4 vs PA1; TIM8 is TRGOUT itself). TIM7 is the pinless delay generator — no contention, and the same pattern serves any GPIO needing a precise one-shot. |
| 4 | Trigger polarity | **Keep inverted** — host software depends on it. |
| 5 | `TriggerFollowS` | **Generalise** — any Q–X input may drive TRG_OUT, not just S. |
| 6 | Pulse counters | **Support several** — up to one per input. |

### 6.1 Consequences of the LDAC decision

DO updates now ride the pulse sequence generator, which is the point — but
**every LDAC edge latches the shift registers**, including edges meant only for
DACs. So:

- The 595 contents must be **valid at all times**; never leave a partial or
  stale pattern between updates.
- A DO write is "shift 16 bits over SPI2, then the next LDAC latches them."
- **`SPIQueue` ordering rule:** the shift transfer must complete *before* any
  pulse-engine LDAC edge. This is a real race to design against, not a
  theoretical one — the pulse engine fires from a timer independent of the
  cooperative loop that issues DO writes.
- `SetImageRegs()` no longer needs its own explicit latch step; it queues the
  shift and the next LDAC does the work. For an immediate, out-of-sequence
  update, use `LDAC_CTRL` to generate LDAC directly.

### 6.2 Generalised follow + multiple counters

Both fall out of the EXTI dispatch table cheaply:

- **Follow:** a per-input "follow target" — on an edge of input *n*, mirror its
  level to TRG_OUT (or any DO). Replaces the hard-coded `FollowSisr`.
- **Counters:** replace the single `PulseCounter*` with an array indexed by
  input, each with its own count, threshold, `resetOnTcount`,
  `triggerOnTcount`. The Rev 5.4 command set (`definePulseCounter`, etc.)
  extends naturally with an input argument.

Both want the same shared ISR dispatch, so build the bus-I/O interrupt layer
(Phase 2b) with per-input callback slots from the start rather than retrofitting.

## 7. Remaining open items

1. **`TriggerFollowS` deglitch.** Rev 5.4 enables the SAM3X fast input
   deglitcher (`PIO_SCIFSR`/`PIO_IFER`). The STM32 has no equivalent per-pin
   glitch filter on EXTI — if input deglitching matters, it needs either an RC
   on the input or a software minimum-width check in the ISR.
2. **Pulse counter maximum rate.** With EXTI per input and several counters
   active, confirm the expected maximum input frequency — a shared ISR at high
   rates could starve the loop. If rates are high, a hardware timer in external
   counter mode is the better implementation for at least one input.
