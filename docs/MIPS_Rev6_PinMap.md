# MIPS Rev 6.0 — STM32H743ZIT6 Pin Map (as built)

**Verified against the generated CubeMX project** (`cubemx.ioc`). This file is
the authoritative pin reference — it reflects what was actually generated, not
what was planned. Where it disagrees with `MIPS_Rev6_PinPlanning_Worksheet.docx`
or `MIPS_STM32H7_Design_Spec.docx`, this file wins (see §6).

- MCU: **STM32H743ZIT6**, LQFP-144
- Pins assigned: **88** of ~114 available I/O
- Core 440 MHz / HCLK 220 MHz / APB timer clocks 220 MHz / USB 48.000 MHz
- Verified: RTC on LSE, TIM2 on LL (all other peripherals HAL), no FreeRTOS

---

## 1. Clock configuration

| Node | Value | Notes |
|---|---|---|
| HSE | 25 MHz crystal | PH0 / PH1 |
| LSE | 32.768 kHz crystal | PC14 / PC15 — **RTC clock source** |
| PLL1 | /5 x176 -> VCO 880 MHz | DIVP1 /2 -> **SYSCLK 440 MHz** |
| PLL1Q | /8 -> **110 MHz** | SPI1/2/3 kernel clock |
| PLL2 | /5 x80 -> 400 MHz, all /4 -> 100 MHz | unused, legal values |
| PLL3 | /5 x96 -> 480 MHz, DIVQ3 /10 | **USB exactly 48.000 MHz** |
| HPRE | /2 | HCLK / AXI = **220 MHz** |
| APB1-4 prescalers | /2 | peripheral 110 MHz, **timer 220 MHz** |
| Voltage scale | **VOS0** | required above 400 MHz; **needs Rev V silicon** |
| CSS | enabled | |
| RTC | 32.768 kHz from LSE | battery-backed via VBAT |

**440 MHz, not 480.** CubeMX enforces a derated AXI/AHB ceiling (~225 MHz) below
the nominal 240. DIVN1 = 176 puts HCLK at 220, under the cap, and still clears
every requirement: TIM1 at 220 MHz gives 4400 counts at 50 kHz (true 12-bit),
and TIM2 at 220 MHz is the kernel clock passed to `STM32PulseTimer::begin()`.

**HAL time base is SysTick** (no RTOS). TIM6 is free and unassigned.

---

## 2. Pin map by port

### Port A

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PA0 | TIM2_ETR | TriggerSource_ETR | `IN_R` | J6 input R — TIM2 external trigger (slave reset) |
| PA1 | TIM5_CH2 | Output Compare2 CH2 |  | Continuous clock output |
| PA2 | TIM2_CH3 | Output Compare3 CH3 |  | LDAC toggle — latches DACs **and DO shift registers** |
| PA3 | GPIO | Output | `LDAC_CTRL` | Overrides LDAC edge detector — software LDAC |
| PA4 | DAC1_OUT1 | DAC_OUT1 |  | Gate / setpoint analog out (§2.2.6) |
| PA5 | SPI1_SCK |  |  | SPI1 clock — TFT / SD |
| PA6 | SPI1_MISO |  |  | SPI1 MISO — TFT / SD |
| PA7 | SPI1_MOSI |  |  | SPI1 MOSI — TFT / SD |
| PA9 | USB_OTG_FS_VBUS |  |  | USB VBUS sense — required for self-powered device |
| PA11 | USB_OTG_FS_DM |  |  | USB D− |
| PA12 | USB_OTG_FS_DP |  |  | USB D+ |
| PA13 | JTMS-SWDIO |  |  | SWDIO |
| PA14 | JTCK-SWCLK |  |  | SWCLK |

### Port B

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PB2 | QUADSPI_CLK |  |  | QSPI clock — config flash |
| PB3 | TIM2_CH2 | Input_Capture2_from_TI2 | `IN_Q` | J6 input Q — TIM2 external clock |
| PB4 | TIM3_CH1 | Encoder_Interface |  | Encoder A — hardware quadrature |
| PB5 | TIM3_CH2 | Encoder_Interface |  | Encoder B — hardware quadrature |
| PB6 | USART1_TX |  |  | Spare UART TX — not on EXT1 (see §4a) |
| PB7 | USART1_RX |  |  | Spare UART RX — not on EXT1 (see §4a) |
| PB8 | I2C1_SCL |  |  | I2C1 SCL — module bus TWI |
| PB9 | I2C1_SDA |  |  | I2C1 SDA — module bus TWI |
| PB10 | QUADSPI_BK1_NCS |  |  | QSPI chip select |
| PB12 | GPIO | Output | `SPI_CS` | Bus SPI chip select — EXT2 pin 6 |
| PB13 | SPI2_SCK |  |  | SPI2 clock — EXT2 pin 13 |
| PB14 | SPI2_MISO |  |  | SPI2 MISO — EXT2 pin 8 |
| PB15 | SPI2_MOSI |  |  | SPI2 MOSI — EXT2 pin 7 |

### Port C

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PC0 | ADC1_INP10 | IN10-Single-Ended |  | Analog input 1 |
| PC1 | ADC1_INP11 | IN11-Single-Ended |  | Analog input 2 |
| PC4 | ADC1_INP4 | IN4-Single-Ended |  | Analog input 3 |
| PC5 | ADC1_INP8 | IN8-Single-Ended |  | Analog input 4 |
| PC6 | TIM8_CH1 | Output Compare1 CH1 | `TRG_OUT` | **Trigger out** — GPIO level/pulse + TIM8 burst. **Inverted in hw** |
| PC10 | UART4_TX |  |  | AUX2 TX — RS-232-to-Ethernet module |
| PC11 | UART4_RX |  |  | AUX2 RX — RS-232-to-Ethernet module |
| PC12 | UART5_TX |  |  | UART3 TX |
| PC14 | OSC32_IN |  |  | LSE 32.768 kHz — RTC |
| PC15 | OSC32_OUT |  |  | LSE 32.768 kHz — RTC |

### Port D

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PD1 | GPIO | Input, pull-up | `ENC_SW` | Encoder push button (pull-up) |
| PD2 | UART5_RX |  |  | UART3 RX |
| PD4 | GPIO | Input | `BIO1` | Generic bus I/O — EXT1 pin 5 |
| PD5 | GPIO | Input | `BIO2` | Generic bus I/O — EXT1 pin 6 |
| PD6 | GPIO | Input | `BIO3` | Generic bus I/O — EXT1 pin 7 |
| PD7 | GPIO | Input | `BIO4` | Generic bus I/O — EXT1 pin 8 |
| PD8 | USART3_TX |  |  | Console TX — EXT1 pin 11 |
| PD9 | USART3_RX |  |  | Console RX — EXT1 pin 12 |
| PD10 | GPIO | Output | `BRDSEL` | Board bank select 0/1 — EXT1 pin 3 |
| PD11 | QUADSPI_BK1_IO0 |  |  | QSPI IO0 |
| PD12 | QUADSPI_BK1_IO1 |  |  | QSPI IO1 |
| PD13 | QUADSPI_BK1_IO3 |  |  | QSPI IO3 |
| PD14 | TIM4_CH3 | Output Compare3 CH3 |  | Continuous clock output |
| PD15 | GPIO | Output | `AUX_TRGOUT` | Aux trigger out — pulse timed by TIM7. **Inverted in hw** |

### Port E

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PE0 | GPIO | Output | `TFT_CS` | Display chip select |
| PE1 | GPIO | Output | `TFT_DC` | Display data/command |
| PE2 | QUADSPI_BK1_IO2 |  |  | QSPI IO2 |
| PE3 | GPIO | Output | `TFT_RST` | Display reset |
| PE4 | GPIO | Output | `SD_CS` | SD card chip select |
| PE5 | GPIO | Output | `LED_R` | RGB LED red |
| PE6 | GPIO | Output | `LED_G` | RGB LED green |
| PE7 | GPIO | Output | `LED_B` | RGB LED blue |
| PE8 | GPIO | Output | `LED_ON` | Status LED — power |
| PE9 | TIM1_CH1 | PWM Generation1 CH1 |  | RF drive PWM 1 — EXT2 pin 9 |
| PE10 | GPIO | Output | `LED_RX` | Status LED — receive |
| PE11 | TIM1_CH2 | PWM Generation2 CH2 |  | RF drive PWM 2 — EXT2 pin 10 |
| PE12 | GPIO | Output | `LED_TX` | Status LED — transmit |
| PE13 | TIM1_CH3 | PWM Generation3 CH3 |  | RF drive PWM 3 — EXT2 pin 11 |
| PE14 | TIM1_CH4 | PWM Generation4 CH4 |  | RF drive PWM 4 — EXT2 pin 12 |
| PE15 | GPIO | Output | `LED_L` | Status LED — activity |

### Port F

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PF0 | I2C2_SDA |  |  | I2C2 SDA — EXT1 pin 10 |
| PF1 | I2C2_SCL |  |  | I2C2 SCL — EXT1 pin 9 |
| PF2 | GPIO | Input | `BIO5` | Generic bus I/O — EXT1 pin 13 |
| PF11 | ADC1_INP2 |  | `VIN_SENSE` | **12 V supply monitor** — 30k/10k divider, 100nF |

### Port G

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PG0 | GPIO | Output | `RCK_DO_AH` | 595 latch A–H — **see §7, LDAC also latches** |
| PG1 | GPIO | Output | `RCK_DO_IP` | 595 latch I–P — **see §7, LDAC also latches** |
| PG2 | GPIO | Output | `ADDR0` | Board address 0 — EXT2 pin 3 |
| PG3 | GPIO | Output | `ADDR1` | Board address 1 — EXT2 pin 4 |
| PG4 | GPIO | Output | `ADDR2` | Board address 2 — EXT2 pin 5 |
| PG5 | GPIO | Output | `OE_DO_AH` | Buffer OE — outputs A–H |
| PG6 | GPIO | Output | `OE_DO_IP` | Buffer OE — outputs I–P |
| PG7 | GPIO | Output | `OE_DI` | Buffer OE — inputs Q–X |
| PG8 | GPIO | Input | `IN_S` | J6 input S |
| PG9 | GPIO | Input | `IN_T` | J6 input T |
| PG10 | GPIO | Input | `IN_U` | J6 input U |
| PG11 | GPIO | Input | `IN_V` | J6 input V |
| PG12 | GPIO | Input | `IN_W` | J6 input W |
| PG13 | GPIO | Input | `IN_X` | J6 input X |
| PG14 | GPIO | Input | `BIO6` | Generic bus I/O — EXT1 pin 14 |
| PG15 | GPIO | Input | `BIO7` | Generic bus I/O — EXT2 pin 14 |

### Port H

| Pin | Peripheral | Mode | Label | Function |
|---|---|---|---|---|
| PH0 | OSC_IN |  |  | HSE 25 MHz |
| PH1 | OSC_OUT |  |  | HSE 25 MHz |

---

## 3. Peripheral allocation

| Peripheral | Driver | Role |
|---|---|---|
| TIM2 (32-bit) | **LL** | Pulse engine — CH3 LDAC toggle, CH2 ext clock (Q), ETR ext trigger (R) |
| TIM1 | HAL | 4-ch PWM, RF drive (EXT2 pins 9–12) |
| TIM3 | HAL | Encoder interface — hardware quadrature decode |
| TIM4 | HAL | Continuous clock output |
| TIM5 (32-bit) | HAL | Continuous clock output |
| TIM7 | HAL | Delay generator (no pins) — also times `AUX_TRGOUT` pulses |
| TIM8 | HAL | `TRG_OUT` — burst of N cycles via repetition counter |
| TIM6 | — | **unused / free** |
| SPI1 | HAL | TFT display + SD card |
| SPI2 | HAL | Module bus — 74HC595 chain + EXT2 (full duplex, MISO required) |
| I2C1 | HAL | Module bus TWI |
| I2C2 | HAL | EXT1 TWI (pins 9/10) |
| QUADSPI | HAL | Config flash (W25Q80DV) — GAACE FlashFS |
| USART3 | HAL | Console (GAACE command processor) |
| USART1 | HAL | UART1 — EXT1 pins 11/12 |
| UART4 | HAL | AUX2 — external RS-232-to-Ethernet module |
| UART5 | HAL | UART3 |
| USB_OTG_FS | HAL | Device — CDC + MSC composite (Phase 3) |
| ADC1 | HAL | 5 single-ended inputs (4 external + `VIN_SENSE`) |
| DAC1 | HAL | OUT1 — gate/setpoint |
| RTC | HAL | Calendar + 32 backup registers, LSE clocked, VBAT retained |

**TIM2 is the only LL peripheral.** The pulse engine needs register-level
control that HAL abstracts away.

---

## 4. EXTI allocation

There are only 16 EXTI lines, and line N is shared by pin N across every port.
The board needs **15 independently interrupt-capable signals** — Q–X (8) plus
BIO1–BIO7 (7) — so every one is on a distinct pin number. **Do not reassign any
of these without re-checking this table.**

| Line | Signal | Pin | NVIC vector |
|---|---|---|---|
| 0 | `IN_R` | PA0 | EXTI0 (individual) |
| 1 | *spare* | — | EXTI1 (individual) |
| 2 | `BIO5` | PF2 | EXTI2 (individual) |
| 3 | `IN_Q` | PB3 | EXTI3 (individual) |
| 4 | `BIO1` | PD4 | EXTI4 (individual) |
| 5 | `BIO2` | PD5 | EXTI9_5 (shared) |
| 6 | `BIO3` | PD6 | EXTI9_5 (shared) |
| 7 | `BIO4` | PD7 | EXTI9_5 (shared) |
| 8 | `IN_S` | PG8 | EXTI9_5 (shared) |
| 9 | `IN_T` | PG9 | EXTI9_5 (shared) |
| 10 | `IN_U` | PG10 | EXTI15_10 (shared) |
| 11 | `IN_V` | PG11 | EXTI15_10 (shared) |
| 12 | `IN_W` | PG12 | EXTI15_10 (shared) |
| 13 | `IN_X` | PG13 | EXTI15_10 (shared) |
| 14 | `BIO6` | PG14 | EXTI15_10 (shared) |
| 15 | `BIO7` | PG15 | EXTI15_10 (shared) |

Lines 0–4 have individual ISRs; 5–9 share `EXTI9_5_IRQHandler` and 10–15 share
`EXTI15_10_IRQHandler`. Each line keeps its own enable, edge selection and
pending flag — the shared handlers dispatch from one line-indexed callback table.

**No peripheral interrupts are enabled in CubeMX** — only core exceptions and
SysTick. TIM2 and EXTI are enabled from firmware; USB will be enabled in CubeMX
at Phase 3 because the HAL stack needs the generated ISR body. See
`CubeMX_Regeneration_Notes.md` §1.5.

**EXTI is configured in firmware, not CubeMX.** All BIO and Q–X pins generate as
plain `GPIO_Input`; the bus-I/O module promotes them to interrupt mode at runtime
and enables the NVIC vectors itself. This keeps the config next to
`bioSetMode()` and survives regeneration.

**The encoder uses no EXTI line** — TIM3 hardware quadrature decode. `ENC_SW`
(PD1) is polled. This is why 15 signals fit in 16 lines.

---

## 4a. USART1 — spare UART (resolved)

On Rev 5.4, `TXD1` appeared on **EXT1 pin 6**. That pin is now `BIO2` and stays
that way — TXD1 is **not** carried on EXT1 in Rev 6.0.

USART1 (PB6/PB7) therefore remains enabled but has no bus destination. It is
available as a general-purpose UART for a header, debug port, or future
peripheral. If it goes unused at schematic capture, PB6/PB7 can be returned to
the spare pool and USART1 disabled in CubeMX.

EXT1 pins 11/12 are `TXD0`/`RXD0`, the **console** — USART3 on PD8/PD9.

---

## 5. Spare pins

Unassigned and available on the 144-pin package:

PA3, PA8, PA9, PA10, PA15, PB0, PB1, PB11, PC2, PC3, PC7, PC8, PC9, PC13,
PA8, PA10, PA15, PB0, PB1, PB11, PC2, PC3, PC7, PC8, PC9, PC13, PD0, PD3,
PF3–PF10, PF12–PF15, plus **TIM6** as a free timer.

Notes:
- **PC2_C / PC3_C** are direct-analog pins with a lower-impedance path to ADC3 —
  reserve these if a high-accuracy measurement is added later.
- **PA15** was freed when TIM2_ETR moved to PA0. It is a TIM2_CH1/ETR pin, so
  keep it clear if the pulse engine ever needs a second timer signal.
- **PD0 / PD3** were freed when the encoder moved to TIM3 hardware decoding.

---

## 6. Supersedes earlier documents

These earlier decisions are **obsolete** — the docs still contain them:

| Stale item | Where | Correct value |
|---|---|---|
| Enable FreeRTOS | Pin-planning worksheet §8.1, design spec §5.4.5 | **No RTOS.** PendSV belongs to the DeferredQueue |
| HAL time base on TIM6 | worksheet §8.1 | SysTick (only needed moving for FreeRTOS) |
| 480 MHz core | worksheet, design spec, `platformio.ini` | **440 MHz** (derated AXI ceiling) |
| LDAC on TIM2 CH1, PA0 | worksheet | **CH3 on PA2** — CH1 and ETR are the same silicon resource |
| LDAC narrow-pulse generation | worksheet, port notes | **Toggle only** — board has an edge detector |
| Q/R as "capture/trigger, TIM2/TIM5" | handoff | Q = TIM2 ext clock (PB3), R = TIM2 ETR (PA0) |
| TIM3 as continuous clock on PB4 | worksheet | TIM3 is the **encoder**; continuous clock moved to TIM5/PA1 |
| EXT1 pins 5–8, 13–14 fixed names | Rev 5.4 schematic | **BIO1–BIO6**, generic runtime-configurable |
| I2C2 on PB10/PB11 | worksheet | **PF0/PF1** (PB10 is QSPI NCS) |

---

## 7. Firmware notes

**Pulse engine (`STM32PulseTimer`)** — TIM2 is LL, so the driver sets these in
`begin()` rather than relying on generated init:

```c
LL_TIM_OC_SetMode(TIM2, LL_TIM_CHANNEL_CH3, LL_TIM_OCMODE_TOGGLE);
LL_TIM_OC_DisablePreload(TIM2, LL_TIM_CHANNEL_CH3);
LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH3);
LL_TIM_SetSlaveMode(TIM2, LL_TIM_SLAVEMODE_RESET);
LL_TIM_SetTriggerInput(TIM2, LL_TIM_TS_ETRF);
```

CubeMX generates CH3 in **frozen** mode — deliberately, so PA2 stays inert until
`begin()` runs. Preload MUST stay disabled: immediate mid-period compare writes
are the core of the pulse engine.

Kernel clock to pass into `begin()`: **220000000**.

**TIM1 RF PWM:** ARR = 4399, PSC = 0 → 50 kHz, 4400 counts (true 12-bit).

**Encoder:** read `TIM3->CNT`, period `0xFFFF`, x4 counting, compute deltas.
`ENC_SW` (PD1) polled with software debounce in the UI task.

**LDAC manual override:** PA2 can be reconfigured to `GPIO_MODE_OUTPUT_PP` at
runtime and back to `GPIO_AF1_TIM2`. Disable the CH3 compare output before
going manual so the timer and manual writes do not contend.

**QSPI:** kernel clock is HCLK3 = 220 MHz. The W25Q80DV tops out near 104 MHz —
set a **/2 or /4 prescaler** in Phase 3.

**Digital outputs A–P are not STM32 pins.** They are bit positions in the
74HC595 chain clocked out over SPI2 — a separate namespace from the pin defines.

---

## 7. LDAC latches the digital outputs

**The DO 595 shift registers are latched by LDAC**, not by separate RCK lines.

> **As-built note:** PG0/PG1 still carry the `RCK_DO_AH` / `RCK_DO_IP` labels in
> the current CubeMX project. They are harmless GPIO outputs, but confirm the
> intent: either **delete them** (LDAC does the latching, freeing two pins), or
> **keep them deliberately** — e.g. to give the board a jumper-selectable
> latch source (RCK *or* LDAC) as a hardware hedge.

**Why:** it puts the 16 digital outputs under the pulse sequence generator. DO
bits can change synchronously with DAC updates, at hardware timing precision —
a capability the separate-RCK arrangement could not provide.

**Constraint this creates:** every LDAC edge latches the shift registers,
including edges intended only for DACs. The 595 contents must therefore be
valid at all times.

- A DO update is "shift 16 bits over SPI2, then the next LDAC latches them".
- The shift **must complete before** the LDAC edge — the `SPIQueue` must order
  the transfer ahead of any pulse-engine LDAC, never race it.
- Never leave a partial or stale pattern in the chain between updates.

`LDAC_CTRL` (PA3) overrides the on-board edge detector so firmware can generate
LDAC directly, carried over from Rev 5.4's `LDACcapture`/`LDACrelease`. PA2
itself also switches between `GPIO_AF1_TIM2` and `GPIO_MODE_OUTPUT_PP` at
runtime.

## 8. Trigger outputs

| Signal | Pin | Modes |
|---|---|---|
| `TRG_OUT` | PC6 (TIM8_CH1) | GPIO level, GPIO/timer pulse, **TIM8 burst of N cycles** |
| `AUX_TRGOUT` | PD15 (GPIO) | level, pulse (width timed by TIM7) |

**Both are inverted in hardware** — `TRIGOUT,HIGH` drives the pin LOW. Preserved
from Rev 5.4; host software depends on this polarity.

TRG_OUT and the burst clock are the **same net** (Rev 5.4: `MIPStimer
FreqBurst(TMR_TrigOut)`), so PC6 switches between GPIO and TIM8_CH1 AF the same
way PA2 does for LDAC.

TIM8's **repetition counter** emits exactly N cycles in hardware — no cycle
counting ISR (Rev 5.4 used `FreqBurstISR`).
