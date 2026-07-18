# MIPS Bus Signal Map — Rev 5.4 (as-built) → Rev 6.0 (STM32H743ZIT6)

Source of truth for the MIPS bus connector pinouts, the generic bus I/O naming
scheme, and the mapping of every bus signal to a physical STM32H743 pin.

Rev 5.4 data reverse-engineered from `MIPS_Rev5_4.pdf` (EAGLE 9.6.2 export,
sheet 1/2). Rev 6.0 assignments per the CubeMX pin-planning session.

---

## 1. Naming scheme

The MIPS bus uses three namespaces:

| Namespace | Signals | Carried on | Direction |
|---|---|---|---|
| `DO_A`–`DO_P` | 16 digital outputs | J4 (A–H), J5 (I–P) | Out (74HC595 → level shift) |
| `IN_Q`–`IN_X` | 8 digital inputs | J6 | In (Schmitt conditioned) |
| `BIO1`–`BIO7` | 7 generic bus I/O | EXT1, EXT2 | **In / Out / Interrupt — per application** |

`BIO` = **B**us **I**/**O**. These seven replace the Rev 5.4 fixed-function names
(`INT4`–`INT7`, `Guard battery status`, `Guard PS on/off`) which described one
historical use rather than the pin's capability. Each `BIO` pin must support
being configured at runtime as input, output, or interrupt source.

---

## 2. EXT1 — MIPS bus connector 1 (14-pin)

| Pin | Rev 5.4 name | Rev 6.0 name | STM32 pin | Notes |
|---|---|---|---|---|
| 1 | Ground | `GND` | — | |
| 2 | 5 volts | +5V | — | |
| 3 | Board select | `BRDSEL` | **PD10** | Selects board bank 0 or 1 |
| 4 | LDAC | `LDAC` | **PA2** | TIM2_CH3, toggle mode (edge detector on board) |
| 5 | INT4 | **BIO1** | **PD4** | Generic — EXTI4 |
| 6 | INT5 (was TXD1) | **BIO2** | **PD5** | Generic — EXTI5. TXD1 not carried on Rev 6.0 |
| 7 | INT6, external clock | **BIO3** | **PD6** | Generic — EXTI6 (timer duty moved to J6 `Q`) |
| 8 | INT7, external trigger | **BIO4** | **PD7** | Generic — EXTI7 (timer duty moved to J6 `R`) |
| 9 | SCL, TWI | — | **PF1** | I2C2_SCL |
| 10 | SDA, TWI | — | **PF0** | I2C2_SDA |
| 11 | TXD0 | — | **PD8** | USART3_TX (console) |
| 12 | RXD0 | — | **PD9** | USART3_RX (console) |
| 13 | Guard battery status | **BIO5** | **PF2** | Generic — EXTI2 |
| 14 | Guard PS on/off | **BIO6** | **PG14** | Generic — EXTI14 |

## 3. EXT2 — MIPS bus connector 2 (14-pin)

| Pin | Rev 5.4 name | Rev 6.0 name | STM32 pin | Notes |
|---|---|---|---|---|
| 1 | Ground | `GND` | — | |
| 2 | 5 volts | +5V | — | |
| 3 | Addr0 | `ADDR0` | **PG2** | Board address bit 0 |
| 4 | Addr1 | `ADDR1` | **PG3** | Board address bit 1 |
| 5 | Addr2 | `ADDR2` | **PG4** | Board address bit 2 |
| 6 | CS, SPI | `SPI_CS` | **PB12** | Chip select, addressed by ADDR0-2 |
| 7 | MOSI, SPI | — | **PB15** | SPI2_MOSI |
| 8 | MISO, SPI | — | **PB14** | SPI2_MISO |
| 9 | PWM out, RF drive CH1 | — | **PE9** | TIM1_CH1 |
| 10 | PWM out, RF drive CH2 | — | **PE11** | TIM1_CH2 |
| 11 | PWM out, RF drive CH3 | — | **PE13** | TIM1_CH3 |
| 12 | PWM out, RF drive CH4 | — | **PE14** | TIM1_CH4 |
| 13 | SCLK, SPI, out | — | **PB13** | SPI2_SCK |
| 14 | *(undefined)* | **BIO7** | **PG15** | Generic — EXTI15 |

**Note:** EXT2 pins 9–12 confirm TIM1's role — the 4-channel PWM bank is the RF
drive output. This is why TIM1 needs a kernel clock ≥204.8 MHz for true 12-bit
resolution at 50 kHz (satisfied: 220 MHz → ARR 4399 → 4400 counts).

---

## 4. BIO summary + EXTI budget

> As-built pin assignments confirmed against the generated CubeMX project.
> Full board pin map: `MIPS_Rev6_PinMap.md`.

| Name | EXT pin | STM32 | EXTI line | Notes |
|---|---|---|---|---|
| BIO1 | EXT1-5 | PD4 | 4 | individual ISR |
| BIO2 | EXT1-6 | PD5 | 5 | shared ISR (EXTI9_5) |
| BIO3 | EXT1-7 | PD6 | 6 | shared ISR (EXTI9_5) |
| BIO4 | EXT1-8 | PD7 | 7 | shared ISR (EXTI9_5) |
| BIO5 | EXT1-13 | PF2 | 2 | individual ISR |
| BIO6 | EXT1-14 | PG14 | 14 | shared ISR (EXTI15_10) |
| BIO7 | EXT2-14 | PG15 | 15 | shared ISR (EXTI15_10) |

All seven are plain GPIO — runtime configurable as input, output, or interrupt
with programmable edge sensitivity. No alternate-function duty.

### ⚠ EXTI line sharing — the governing constraint

On STM32 there are only **16 EXTI lines**, and line N is shared by pin N across
every port: PA4, PB4, PC4 and PD4 all drive EXTI4, and only one may be an active
interrupt source at a time.

The board needs **15 independently interrupt-capable signals** — Q–X (8) plus
BIO1–BIO7 (7). That fits in 16 lines with one spare, but ONLY because every one
of the 15 is assigned a distinct pin number. Any reassignment must preserve this.

| Line | Signal | Pin | NVIC vector |
|---|---|---|---|
| 0 | R (TIM2_ETR) | PA0 | EXTI0 (individual) |
| 1 | *spare* | — | EXTI1 (individual) |
| 2 | BIO5 | PF2 | EXTI2 (individual) |
| 3 | Q (TIM2_CH2) | PB3 | EXTI3 (individual) |
| 4 | BIO1 | PD4 | EXTI4 (individual) |
| 5 | BIO2 | PD5 | EXTI9_5 (shared) |
| 6 | BIO3 | PD6 | EXTI9_5 (shared) |
| 7 | BIO4 | PD7 | EXTI9_5 (shared) |
| 8 | `IN_S` | PG8 | EXTI9_5 (shared) |
| 9 | `IN_T` | PG9 | EXTI9_5 (shared) |
| 10 | `IN_U` | PG10 | EXTI15_10 (shared) |
| 11 | `IN_V` | PG11 | EXTI15_10 (shared) |
| 12 | `IN_W` | PG12 | EXTI15_10 (shared) |
| 13 | `IN_X` | PG13 | EXTI15_10 (shared) |
| 14 | BIO6 | PG14 | EXTI15_10 (shared) |
| 15 | BIO7 | PG15 | EXTI15_10 (shared) |

**Grouped NVIC vectors are not a functional limit.** Lines 0–4 get individual
ISRs; lines 5–9 share `EXTI9_5_IRQHandler` and lines 10–15 share
`EXTI15_10_IRQHandler`. Each line still has its own enable bit, its own edge
selection (rising / falling / both) and its own pending flag — the shared handler
simply reads the pending register and dispatches. Build ONE dispatch table keyed
by line number rather than per-module handlers.

**Encoder consequence:** with 15 of 16 lines committed, the encoder cannot be
interrupt-driven (one spare line won't cover A and B). Read it with **timer
encoder mode or polling** — the better implementation for a UI knob anyway.

---

## 5. J6 digital inputs Q–X

All eight are general-purpose inputs, programmable for interrupts with
configurable edge sensitivity. Q and R additionally carry TIM2 duty:

| Signal | STM32 | Alternate function | EXTI |
|---|---|---|---|
| `IN_Q` | PB3 | **TIM2_CH2** — TIM2 external clock | 3 |
| `IN_R` | PA0 | **TIM2_ETR** — TIM2 external trigger (slave reset) | 0 |
| `IN_S` | PG8 | — | 8 |
| `IN_T` | PG9 | — | 9 |
| `IN_U` | PG10 | — | 10 |
| `IN_V` | PG11 | — | 11 |
| `IN_W` | PG12 | — | 12 |
| `IN_X` | PG13 | — | 13 |

**TIM2 CH1 stays disabled.** On TIM2, CH1 and ETR are the same silicon resource
sharing pins PA0/PA5/PA15 — enabling CH1 makes ETR unreachable. Since R needs
ETR, CH1 is unused and the LDAC output lives on **CH3 (PA2)**.

Q and R switch between timer AF and GPIO/interrupt mode at runtime via the same
MODER/AFR reconfiguration used by the BIO pins.

---

## 6. Software interface (proposed)

The `BIO` pins are runtime-configurable, so they want a small dedicated API
rather than scattered `HAL_GPIO_*` calls. Suggested shape, to live alongside the
module bus driver:

```c
typedef enum { BIO_INPUT, BIO_INPUT_PU, BIO_INPUT_PD,
               BIO_OUTPUT, BIO_INTERRUPT_RISING,
               BIO_INTERRUPT_FALLING, BIO_INTERRUPT_BOTH,
               BIO_ALTFUNC } BioMode;

typedef enum { BIO1=1, BIO2, BIO3, BIO4, BIO5, BIO6, BIO7 } BioPin;

bool bioSetMode(BioPin p, BioMode m);            // reconfigure at runtime
bool bioWrite(BioPin p, bool state);
bool bioRead(BioPin p);
bool bioAttach(BioPin p, void (*cb)(BioPin));    // interrupt callback
void bioDetach(BioPin p);
```

The BIO pins have no alternate-function duty, so `BIO_ALTFUNC` applies only to
the J6 inputs — `Q` (TIM2 external clock) and `R` (TIM2 external trigger) — which
switch between timer AF and GPIO/interrupt mode at runtime. Same MODER/AFR
mechanism as the LDAC pin's manual-GPIO override.

The same API should cover the eight J6 inputs, since Q–X are equally
programmable for direction, interrupt and edge sensitivity:

```c
typedef enum { IN_Q=0, IN_R, IN_S, IN_T, IN_U, IN_V, IN_W, IN_X } BusInput;

bool busInSetMode(BusInput p, BioMode m);
bool busInRead(BusInput p);
bool busInAttach(BusInput p, void (*cb)(BusInput));
```

**One dispatch table, not per-module handlers.** Because EXTI lines 5–9 and
10–15 share NVIC vectors, both shared ISRs should walk a single line-indexed
callback table covering BIO1–7 and Q–X together.

**Command table** (a module owning its own GAACE CommandList, per the Rev 6.0
module architecture): `BIOMODE <n>,<mode>`, `BIOSET <n>,<T/F>`, `BIOGET <n>`,
`BIOLIST`.

---

## 7. Open questions for Gordon

1. **BIO7 (EXT2-14) intended function?** Currently undefined/spare. Assigning a
   default direction at boot (input, pulled) is the safe choice until defined.
2. **Guard battery / PS control (BIO5/BIO6).** Retained as generic I/O — confirm
   the guard supply feature survives into Rev 6.0, or whether these two become
   pure spares.
