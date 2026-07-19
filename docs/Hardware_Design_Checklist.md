# MIPS Rev 6.0 — Hardware Design Checklist

Everything decided during the firmware/CubeMX work that affects the schematic.
Consolidated so schematic capture does not have to re-read the whole design
history. Pin assignments: `MIPS_Rev6_PinMap.md` (authoritative).

✅ **Pin map verified against the current `.ioc`** — 88 pins, RTC on LSE, TIM2 on LL.

---

## 1. MCU and clocks

| Item | Value |
|---|---|
| Part | **STM32H743ZIT6**, LQFP-144 |
| Silicon revision | **Rev V required** — Rev Y cannot reach VOS0 |
| Core clock | 440 MHz (VOS0), HCLK 220 MHz |
| HSE | **25 MHz crystal** → PH0 / PH1 |
| LSE | **32.768 kHz crystal** → PC14 / PC15 (RTC) |
| Debug | SWD only — PA13 (SWDIO), PA14 (SWCLK) |

Power pin connections, rail allocation and decoupling: **`Power_Supply_Design.md`
§3.5–3.6.** Summary: all VDD on `+3V3_MCU` with 100 nF per pin; VDDA and
VDD33USB via ferrite beads from the same rail (**never a separate regulator**);
VCAP capacitors ~2.2 µF X7R hard against the pins; single solid ground plane,
not split.

Reference: **ST AN4938** — the hardware design guide for this MCU family.

---

## 2. Power

### 2.1 Sources and OR-ing

Two 5 V sources that must never fight:

- **USB VBUS** (bus-powered operation)
- **12 V input → buck → 5 V** (self-powered operation)

**Full power train definition: `Power_Supply_Design.md`.**

| Stage | Part |
|---|---|
| 12 V → 5 V | **Recom R-78E5.0-1.0** module (SIP-3, 7805 pinout, 1 A) |
| Source select | **TPS2116DRLR** power mux — VIN1 = 12 V-derived (**priority**), VIN2 = USB VBUS |
| 3.3 V ×2 | **AP2114H-3.3TRG1** (SOT-223) → `+3V3_MCU`, `+3V3_PERIPH` |

The TPS2116 replaces a diode-OR and does three jobs at once: explicit source
**priority** (12 V always wins when present), **reverse-current blocking** —
which enforces "USB 5 V must never back-feed the 12 V rail" in silicon — and its
**ST pin** is a free power-source indicator to a GPIO (**PF3**).

Still watch **LDO dropout.** The mux costs only ~26 mV (vs ~350 mV for a
Schottky), leaving ~1.12 V at worst case — but common 1.1–1.2 V dropout parts
(AMS1117, LD1117, NCP1117, TLV1117) still have no margin, and they fail *only*
on USB power, so the board works on the bench and dies on a laptop.

> **Hard rule: USB 5 V must never back-feed the 12 V rail.**

The board runs from USB power (5 V → 3.3 V regulator) but the **12 V input is
never powered from USB**.

### 2.2 12 V input monitoring

| Item | Value |
|---|---|
| Max input | **12 V**, zener clamped |
| Divider | **39 kΩ / 10 kΩ** → 2.449 V at 12 V in |
| Thévenin | 8.0 kΩ — fine for H7 sampling |
| Scale factor | firmware: `Vin = Vadc × 4.9` |
| Filter | **100 nF** at the ADC node |
| Goes to | **PF11** (`VIN_SENSE`, ADC1_INP2) |

Restores a Rev 5.4 feature (R6/R7 → AD8). Combined with VBUS sense it resolves
all four power states — see `CubeMX_Regeneration_Notes.md` §1.3.

Scaled for a **3.0 V ADC full scale** (see §2.6), landing at 81.6% FS with
headroom. Do not use the earlier 30k/10k — that gave exactly 3.0 V, i.e. full
scale with no margin.

### 2.6 ⚠ ADC reference is 3.0 V, not 3.3 V

VREF+ is driven by a **dedicated REF3033 3.0 V series reference**, not tied to
VDDA — ADC accuracy is then ~0.1% rather than the LDO's ~±2%.

**Consequences that affect the schematic:**

- **ADC full scale is 3.0 V.** The four ADC-connector inputs (PC0, PC1, PC4,
  PC5) clip at 3.0 V. Any input conditioning carried over from Rev 5.4 must be
  checked against 3.0 V, not 3.3 V.
- `VIN_SENSE` divider is 39k/10k (§2.2), scaled for this.
- **VREFBUF must be disabled in firmware** — the H743's internal reference
  buffer would otherwise fight the external reference. CubeMX setting, easy to
  miss, and the symptom (wrong/drifting ADC readings) does not point at it.

Full detail: `Power_Supply_Design.md` §3.6.

### 2.3 USB-C

| Item | Requirement |
|---|---|
| CC1, CC2 | **5.1 kΩ pull-down each** to GND (identifies as sink — without these a USB-C host never enables VBUS) |
| Data | D+ → **A6 and B6**; D− → **A7 and B7** (reversible cable, USB 2.0 only, no mux needed) |
| VBUS sense | → **PA9 via a divider** — see §2.3.1. **Do NOT connect 5 V directly.** |
| Current | Plain Rd = 500 mA default. **Verify the USB-only current budget** — more needs CC sensing or a PD controller |

### 2.3.1 ⚠ VBUS sense — divider is MANDATORY

**Do not connect VBUS directly to PA9.** PA9 is 5 V tolerant, but the absolute
maximum rating for a 5 V-tolerant pin is **VDD + 4 V**. With the board unpowered
(VDD = 0) that ceiling is 4 V, and 5 V on the pin violates AMR — ST states this
can cause permanent damage. This board is self-powered from 12 V and can sit
unpowered with USB still plugged in, so the condition WILL occur in normal use.

A second problem rules out the obvious workaround: the **native** VBUS detector
threshold is 4.25 V, but any divider low enough to be safe at VDD = 0 sits below
that threshold. You cannot satisfy both.

**Solution — divider into a plain GPIO, native VBUS sensing disabled:**

| Item | Value |
|---|---|
| R1 (VBUS → pin) | **100 kΩ** |
| R2 (pin → GND) | **200 kΩ** |
| Pin voltage at 5 V VBUS | ~3.33 V — under the 4 V AMR |
| Injection current at VDD = 0 | ~15 µA — far below the ±5 mA limit |
| Filter | 100 nF at the pin |

**CubeMX:** PA9 becomes plain `GPIO_Input` labelled `VBUS_SENSE`, **not**
`USB_OTG_FS_VBUS`. Poll it (plug events are slow — the 100 ms UI task is fine).
No EXTI: PA9 would be EXTI9, already owned by `IN_T` on PG9.

**Firmware:** set `vbus_sensing_enable = DISABLE` and drive the D+ pull-up with
`HAL_PCD_DevConnect()` / `DevDisconnect()` from the GPIO state. Required anyway
for a self-powered device — it must not present its pull-up without VBUS.

Reference: ST **AN4879**, "Introduction to USB hardware and PCB guidelines using
STM32 MCUs".

### 2.4 RTC battery backup

| Item | Requirement |
|---|---|
| Cell | CR2032 **direct to VBAT**, no series diode (H7 has internal VDD→VBAT switchover) |
| Decoupling | **100 nF** close to the VBAT pin |
| Unpopulated case | **VBAT must never float** — solder-jumper or 0 Ω selecting VBAT = cell **or** VBAT = 3V3 |

Backup-domain draw with LSE running is ~µA; cell life is shelf-limited.
Provides battery-retained calendar plus 32 backup registers.

### 2.5 Open

- **3.3 V power architecture** — now defined in `Power_Supply_Design.md`.
- ⚠ **Estimated load (~650 mA) may exceed the 500 mA USB default** — but the
  estimate is conservative; realistic steady state is likely 350–450 mA.
  **Measure at bring-up.** If over, mitigate in firmware (dim/blank backlight,
  defer SD access in USB-only mode).
- ❌ **The internal SMPS is NOT available** on the LQFP144 package (no VLXSMPS /
  VDDSMPS pins — the VCAP pins confirm LDO-only). `PWR_LDO_SUPPLY` is the only
  option. See `Power_Supply_Design.md` §2.1.
- ⚠ **VCAP capacitors are mandatory** — ~2.2 µF X7R on each of the two VCAP
  pins, placed hard against the package. The core LDO is unstable without them.
- **USB-only inhibit:** what must be held off when running without 12 V (HV,
  analog, module bus power)? If a hardware inhibit line is needed it is another
  GPIO to allocate — decide before layout.

---

## 3. Module bus

### 3.1 Digital outputs A–P — **latched by LDAC**

16 outputs via 74HC595 chain on **SPI2**, level shifted.

> **The 595 latch is LDAC, not separate RCK lines.** This puts the digital
> outputs under the pulse sequence generator — DO bits change synchronously
> with DAC updates at hardware timing precision.

**Decide before layout:** PG0/PG1 are still assigned as `RCK_DO_AH`/`RCK_DO_IP`
in the current CubeMX project. Either drop them (LDAC latches; two pins freed)
or keep them deliberately as a **jumper-selectable latch source** — RCK or LDAC.
The second is a reasonable hedge on a first-spin board, since it costs two pins
that are otherwise spare and de-risks the LDAC-latch assumption.

Firmware consequence (noted here because it constrains the hardware intent):
every LDAC edge latches the shift registers, including DAC-only edges, so the
595 contents must be valid at all times.

### 3.2 LDAC and its edge detector

| Signal | Pin | Role |
|---|---|---|
| `LDAC` | **PA2** (TIM2_CH3) | Toggles; on-board edge detector emits a pulse **on every edge** |
| `LDAC_CTRL` | **PA3** | Overrides the edge detector so firmware can drive LDAC directly |

`LDAC_CTRL` carries over Rev 5.4's `LDACcapture` / `LDACrelease` (A11). PA2 also
switches between timer AF and plain GPIO output at runtime — route PA3 close to
PA2 and the edge-detector circuit.

### 3.3 Bus control

| Signal | Pin | Notes |
|---|---|---|
| `ADDR0-2` | PG2 / PG3 / PG4 | Board address → selects which board the CS applies to |
| `SPI_CS` | PB12 | EXT2 pin 6 |
| `BRDSEL` | PD10 | Board **bank** select 0/1 → 8 addresses × 2 banks = **16 boards** |
| `OE_DO_AH` | PG5 | Buffer OE, outputs A–H |
| `OE_DO_IP` | PG6 | Buffer OE, outputs I–P |
| `OE_DI` | PG7 | Buffer OE, inputs Q–X |

### 3.4 SPI2 is full duplex — MISO required

`SPI2_MISO` (PB14) goes to **EXT2 pin 8**. The bus is read/write, not
write-only as the older design spec describes.

> **Open:** what drives MISO on EXT2 pin 8 — a '165-style input register, a
> readback path off the '595 chain, or an external device plugged into EXT2?
> Determines the CS/latch arrangement.

### 3.5 Digital inputs Q–X

Schmitt conditioned, buffered through the '3245. Q and R additionally carry
TIM2 duty:

| Signal | Pin | Extra role |
|---|---|---|
| `IN_Q` | PB3 | **TIM2_CH2** — TIM2 external clock |
| `IN_R` | PA0 | **TIM2_ETR** — TIM2 external trigger |
| `IN_S`–`IN_X` | PG8–PG13 | — |

All eight are interrupt-capable with programmable edge sensitivity.

> **Open — input deglitching.** Rev 5.4 used the SAM3X per-pin fast input
> deglitcher (`PIO_SCIFSR`/`PIO_IFER`) for `TriggerFollowS`. **The STM32 has no
> equivalent on EXTI.** If real installations see noisy edges, add an **RC on
> the input** — a hardware decision that must be made now.

---

## 4. Trigger outputs — both inverted

| Signal | Pin | Modes |
|---|---|---|
| `TRG_OUT` | **PC6** (TIM8_CH1) | GPIO level, pulse, **and TIM8 frequency burst of N cycles** |
| `AUX_TRGOUT` | **PD15** (GPIO) | level, pulse (width timed by TIM7) |

> **Both signals are inverted on the controller**, carried over from Rev 5.4
> (`TRIGOUT,HIGH` drives the pin LOW). Host software depends on this polarity.

`TRG_OUT` and the burst generator are the **same net** — PC6 switches between
GPIO and TIM8_CH1 alternate function.

Set boot output levels so both lines idle **inactive** at reset, remembering
the inversion.

---

## 5. Connectors

### EXT1 / EXT2

Pinouts and the `BIO1`–`BIO7` generic signals: **`MIPS_Bus_Signal_Map.md`**.

Seven generic bus signals (EXT1 pins 5, 6, 7, 8, 13, 14 and EXT2 pin 14) are
runtime-configurable as input, output or interrupt. EXT2 pins 9–12 are the RF
drive PWMs (TIM1, PE9/PE11/PE13/PE14).

### Other

| Connector | Signals |
|---|---|
| J4 | Digital outputs A–H |
| J5 | Digital outputs I–P |
| J6 | Digital inputs Q–X |
| ADC | 4 analog inputs → PC0, PC1, PC4, PC5 |
| AUX2 | External **RS-232-to-Ethernet module** → UART4 (PC10/PC11) |

> **No native Ethernet.** No MAC, no PHY, no RMII — Ethernet is the external
> UART module. This is a deliberate, large simplification.

---

## 6. Display, SD and UI

| Item | Pins |
|---|---|
| SPI1 (TFT + SD) | PA5 SCK, PA6 MISO, PA7 MOSI |
| TFT | PE0 `TFT_CS`, PE1 `TFT_DC`, PE3 `TFT_RST` |
| SD card | PE4 `SD_CS` |
| Encoder | PB4 `ENC_A`, PB5 `ENC_B` (TIM3 hardware quadrature) |
| Encoder switch | PD1, **internal pull-up** |
| RGB LED | PE5 / PE6 / PE7 |
| Status LEDs | PE8 `ON`, PE10 `RX`, PE12 `TX`, PE15 `L` |

Display is the 240×320 2.2" TFT with SD (ILI9340). TFT and SD share SPI1 with
independent chip selects.

---

## 7. Config flash

**W25Q80DV** on QUADSPI — PB2 CLK, PB10 NCS, PD11 IO0, PD12 IO1, PE2 IO2,
PD13 IO3. Replaces DueFlashStorage; holds GAACE FlashFS.

Max ~104 MHz; the QSPI kernel clock is 220 MHz so firmware applies a /2 or /4
prescaler. No hardware implication beyond normal QSPI layout care.

---

## 8. Open items blocking final schematic

0. **External ADC input conditioning** — verify the Rev 5.4 ADC-connector input
   scaling suits a **3.0 V** full scale (§2.6).
1. **IC11 gate/pulse circuit intent** (design spec §2.2.6) — never resolved.
   The analog setpoint is now the on-chip **DAC on PA4**; confirm what remains
   of the original LMV358 + FDN340P circuit.
2. **3.3 V power architecture** (design spec §11.2.2).
3. **EXT2 MISO source** (§3.4).
4. **Input deglitching** — RC or none (§3.5).
5. **USB-only inhibit** — hardware line needed? (§2.5).
6. **BIO7 (EXT2 pin 14)** — still undefined. Default to input, pulled.
7. **Guard battery status / PS on/off** (now `BIO5`/`BIO6`) — does the guard
   supply feature survive into Rev 6.0, or are these pure spares?
8. **USART1** (PB6/PB7) has no bus destination. Break out to a header, or drop
   it and return the pins to spares.

---

## 9. Superseded documents

`MIPS_STM32H7_Design_Spec.docx`, `MIPS_Rev6_PinPlanning_Worksheet.docx` and
`MIPS_Rev6_Firmware_Plan.docx` predate most of the above. Known-stale items are
listed in `MIPS_Rev6_PinMap.md` §6 — notably FreeRTOS, 480 MHz, LDAC on CH1/PA0,
the narrow-pulse LDAC scheme, TIM3 as a clock output, and I2C2 on PB10/PB11.

**Trust the markdown docs over the .docx files.**
