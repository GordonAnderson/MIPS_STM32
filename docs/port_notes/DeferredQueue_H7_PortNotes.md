# DeferredQueue — STM32H743 (native STM32Cube HAL/LL) Port Notes

Port of `DeferredQueue.h` / `I2CQueue.h` / `SPIQueue.h` from the Arduino-framework
SAMD version to native STM32Cube. **The queue mechanics are unchanged** — same
fast-path / PendSV-drain / thread-mode self-drain behavior. Only the environment
couplings changed.

## What changed and why

### DeferredQueue.h (the generic core)
- **Dropped `<Arduino.h>`** → now depends only on `stm32h7xx.h` (CMSIS core +
  device), which the HAL pulls in. This is what makes it consistent with the
  hybrid HAL/LL project instead of requiring the Arduino core.
- **Replaced `noInterrupts()`/`interrupts()`** with a small RAII critical
  section (`DQCritical`) that does **PRIMASK save/restore**. This is safer than
  the Arduino pair, which unconditionally re-enables — the save/restore version
  won't prematurely re-enable interrupts if a critical section is entered while
  already masked (matters once HAL ISRs are in the mix).
- **Added `__DMB()`** after `run()` completes / before releasing `_busy`, so the
  transaction's memory writes are ordered before the release on the M7. Belt-
  and-suspenders correctness; cheap.
- **`(1u << __NVIC_PRIO_BITS) - 1u`** auto-adapts: SAMD had 3 priority bits (→7),
  the H7 has 4 (→15). No change needed — the expression is portable.

Everything else — the registry, `serviceFromPendSV()`, `enqueue()`, `service()`,
`drainAll()`, the power-of-2 ring, the `__get_IPSR()` thread/ISR check,
`SCB->ICSR = SCB_ICSR_PENDSVSET_Msk` — is byte-for-byte the same logic.

### I2CQueue.h
- `TwoWire*` → `I2C_HandleTypeDef*`.
- Arduino `Wire` calls → HAL: `HAL_I2C_Master_Transmit` / `_Receive`.
- **WriteRead (register-read w/ repeated start):** uses `HAL_I2C_Mem_Read`, which
  is the blocking HAL API that does a true repeated start. (The `HAL_I2C_Master_
  Seq_*` functions are interrupt/DMA-only and do **not** block — an easy mistake;
  the initial draft used them and was corrected.) 1-byte command → 8-bit mem
  addr; 2-byte → 16-bit. For **>2 command bytes**, it falls back to
  transmit-then-receive with a STOP between (fine for virtually all devices; a
  device that strictly needs repeated-start with a multi-byte command should be
  promoted to a dedicated interrupt-driven Seq_* handler).
- Addresses are **7-bit unshifted** in the API; HAL's `<<1` shift is applied
  internally.
- Timeout is now a HAL millisecond timeout (default 5 ms) rather than the
  micros() busy-loop. A DWT microsecond clock (`DWT_InitMicros`/`dwt_micros`) is
  provided for anything that wants finer timing.

### SPIQueue.h
- `SPIClass*` → `SPI_HandleTypeDef*`; `SPISettings` removed.
- Arduino `SPI.transfer(buf,len)` (in-place) → `HAL_SPI_TransmitReceive(tx, rx,
  len)`, with a small stack copy of the outgoing bytes as the TX source while
  receiving back into `data[]`.
- CS: `digitalWrite(csPin,...)` → `HAL_GPIO_WritePin(csPort, csPin, ...)`,
  active-low. Pass `csPort = nullptr` to manage CS yourself.
- **Behavioral note / difference:** the Arduino version carried per-transaction
  `SPISettings` so multiple devices could share one bus at different clock/mode.
  In HAL those live in the `SPI_HandleTypeDef`. This port assumes **one HAL SPI
  handle per bus at a fixed configuration**, selecting devices by CS only. If you
  need per-device clock/mode on a shared bus, use separate `SPI_HandleTypeDef`
  instances over the same `SPIx` (swapped in `run()`), or a dedicated reconfig
  transaction. Flagged inline in the file.

## Required one-time setup (in exactly one .cpp)

```c
// After HAL_Init() / SystemClock_Config():
NVIC_SetPriority(PendSV_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);  // lowest priority
DWT_InitMicros();                                             // if using dwt_micros()

extern "C" void PendSV_Handler(void) {
  DeferredQueueBase::serviceFromPendSV();
}
```

## ⚠ The one thing to watch during bring-up: PendSV ownership

CubeMX-generated `stm32h7xx_it.c` may already define `PendSV_Handler`. Because
this project runs **no preemptive RTOS**, PendSV is free — but there must be
exactly **one** `PendSV_Handler`. Either remove the generated stub or put the
`DeferredQueueBase::serviceFromPendSV()` call inside it. This is the single
coexistence check the whole no-RTOS architecture depends on (per the firmware
plan): if a preemptive RTOS is ever added later, it claims PendSV and this
mechanism must move to a dedicated software-triggered IRQ.

## D-cache note (H7)

The ring buffer and transaction copies live in normal SRAM and are only touched
by the CPU — **no cache maintenance needed here**. If a transaction's `run()`
later uses **DMA** to/from cached SRAM (e.g. a future DMA-based SPI/I2C transfer),
cache clean/invalidate belongs in that transfer code, not in the queue. The
current port uses blocking HAL transfers (no DMA), so this doesn't arise yet.

## Verification done

The generic core was compile-checked (`-Wall -Wextra`, clean) and functionally
tested off-target with CMSIS stubs: fast-path immediate run, ISR-context
handling, and PendSV drain all behave as expected. The HAL adapters can only be
fully validated on hardware (or against the real HAL headers) — that's Phase 2
of the firmware plan.
