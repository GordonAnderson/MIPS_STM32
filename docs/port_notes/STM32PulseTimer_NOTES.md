# STM32PulseTimer — Port Notes

Port of `SAMD51Timer` → STM32H743 **TIM2**, for the MIPS pulse sequence generator.
Written against the **STM32 LL (Low-Layer) driver**, not Arduino.

## What this driver is

The single dedicated pulse sequence generator. Per step:

1. Firmware loads DAC values (AD5668) / digital output word.
2. Timer fires a **narrow active-low LDAC pulse** → all outputs update on the LDAC falling edge.
3. Firmware prepares the next step (often a new compare position and/or new period).
4. Repeat.

## The critical design decision — immediate, mid-period compare updates

The SAMD51 original used **buffered** compare/period updates (`CCBUF`/`PERBUF`) that
apply at the *next* overflow. MIPS needs the **opposite**: the LDAC pulse position must be
settable **on the fly, mid-period, effective immediately in the current period** — because a
single period can be hundreds of milliseconds long and the firmware moves the pulse position
while that period is still running.

Therefore: **output-compare preload is DISABLED** (`LL_TIM_OC_DisablePreload`). CCR writes go
live immediately. This is the single most important line in the driver and is called out in
comments in both files.

## The passed-compare hazard (handled)

With preload off and a long period, if firmware moves the compare to a value the counter has
**already passed**, a normal match won't fire again until the next period → a **missed pulse**.

`setComparePosition()` detects this (writes the compare, then re-reads the live counter — the
write-then-read order is deliberate so a count observed `>=` the target is authoritative) and
applies a **`CompareHazardPolicy`**:

- `HAZARD_FIRE_NOW` *(default, recommended for the sequence engine)* — force one LDAC pulse
  immediately via a software CC1 event, so the step's pulse is never lost.
- `HAZARD_SKIP` — accept a missed pulse this period; compare stays set for next period.
- `HAZARD_WRAP` — intentionally treat the request as "next period."

It reports back via `CompareResult` (`COMPARE_SCHEDULED` vs `COMPARE_HAZARD_HANDLED`) so the
sequence engine knows what happened.

## Why TIM2

32-bit counter (essential: µs-wide pulse inside a 100s-of-ms period is a huge dynamic range),
and a hardware **ETR** pin for the external trigger. **TIM5** is the identical 32-bit drop-in
alternative — `#define STM32_PULSE_TIMER_USE_TIM5` at the top of the header retargets it.

## Improvements over the SAMD51 version

- **Hardware external trigger.** The SAMD51 faked the trigger with `attachInterrupt()` + a
  software `start()` in the ISR. This version uses TIM2 **slave-mode RESET on ETRF** — an
  external edge resets and restarts the counter in silicon, zero interrupt latency. This is the
  correct realization of the MIPS **Q/R** trigger inputs from the design spec (§2.2.3 / §5.2).
- **Finer prescaler.** STM32's prescaler is a full 16-bit integer divider (1–65536), vs. the
  SAMD51's 8 fixed power-of-two steps.

## Preserved from the SAMD51 API

- Fluent/chainable `TimerType& method()` style.
- All **three callbacks**: `onStart` (CH3, start-of-period), `onMid` (CH1, the LDAC instant),
  `onTerm` (update/rollover — where the sequence engine typically loads the next step).
- `setFrequency` / `setPeriod` / multishot cycle count with auto-stop / start / stop.

## LDAC pulse generation

The pulse is built as a **lead-edge (CH1) / trail-edge (CH2) pair** so its width is defined
precisely regardless of period length. Default width ~1 µs (`setLDACPulseWidthNs`), well above
the AD5668's ~20 ns minimum LDAC low width. Polarity is active-low (idle high, latch on falling
edge) to match the AD5668. Pin idles high via an internal pull-up until the AF drives it.

## Integration points you must fill in (from CubeMX / your board)

These are the values the driver deliberately does **not** assume:

1. **`begin(timerKernelClockHz)`** — pass the **actual TIM2 kernel clock** in Hz from your
   CubeMX clock tree (on H743 the APB1 timer clock is often 200–240 MHz depending on config).
   All frequency/period math uses this. Getting it wrong scales every pulse time.

2. **`configureLDACOutputPin(port, pin, alternate)`** — the package pin + **AF number** for the
   chosen TIM2 CH1 output, from the datasheet AF table / CubeMX. (CH1 and CH2 share the same
   physical LDAC pin — CH1 drives the falling edge, CH2 the rising edge.)

3. **`setExternalTrigger(port, pin, alternate, edge)`** — the **ETR** package pin + AF for the
   Q/R external trigger, if used.

4. **GPIO port clocks** — assumed enabled by your board init (`LL_AHB4_GRP1_EnableClock(...)`).

5. **NVIC priority** — `attachInterrupts()` enables the IRQ; set its priority in your system
   init relative to the rest of MIPS (this is a timing-critical ISR — give it appropriate
   priority for the sequence engine).

## Timer allocation reminder

MIPS uses ~8 timers total (this pulse generator + delay generators, clock generation, etc.).
This class claims **TIM2 only**. As the other timer functions are ported, keep a running map of
which STM32 `TIMx` backs which MIPS function so they don't collide. TIM5 is spoken-for only if
you retarget this driver to it.

## Not yet done / worth a review pass

- **Bring-up validation** of the immediate-compare + hazard path against the real sequence
  engine on hardware — the logic is correct by construction but the passed-compare edge case is
  exactly the kind of thing to scope-verify with a real long-period/short-pulse sequence.
- Confirm whether any sequence steps need the LDAC pulse and a **separate digital output** edge
  aligned to it (the spec mentions both analog and digital pulses) — if the digital lines need
  their own timer-driven edges, additional CH channels or a second synchronized timer may be
  wanted.
