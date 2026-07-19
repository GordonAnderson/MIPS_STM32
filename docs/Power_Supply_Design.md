# MIPS Rev 6.0 — Power Supply Design

Complete power train definition for the Rev 6.0 controller. Companion to
`Hardware_Design_Checklist.md` §2.

---

## 1. Topology

```
  12 V IN ──[F1]──[D_rev]──[TVS]──┬──> Recom R-78E5.0-1.0 ──> +5V_SYS ──> VIN1 ─┐
                                  │       (module, 5 V 1 A)      (priority)     │
                                  └──> VIN_SENSE divider ──> PF11               │
                                                                    TPS2116     ├─> VOUT = +5V_RAIL
  USB-C VBUS ─────────────────────────────────────────────────────> VIN2       │
       │                                                                        │
       └──> VBUS_SENSE divider ──> PA9              PR1 -> VIN1 (priority mode) │
                                                    ST  -> PF3 (which source)  ─┘

  +5V_RAIL ──┬──> LDO1 (AP2114H) ──> +3V3_MCU     -> STM32 VDD / VDDA (FB) / VREF+
             │                                     -> QSPI flash, level shifters, LEDs
             │
             └──> LDO2 (AP2114H) ──> +3V3_PERIPH  -> display module: TFT, backlight, SD

  +5V_RAIL ──[PTC]──> +5V_BUS ──> EXT1 pin 2, EXT2 pin 2, level shifter VCCB
```

Two independent 3.3 V rails, both fed from a **power-mux'd** 5 V rail. Splitting by
domain (not paralleling regulators) spreads the heat across two packages and
isolates SD-card write bursts and display backlight transients from the MCU
supply.

> **Never parallel LDO outputs onto one rail.** Whichever has the marginally
> higher setpoint sources everything until current limit, then the other picks
> up — no thermal sharing, unpredictable behaviour.

---

## 2. Current budget

| Rail | Load | Est. current |
|---|---|---|
| `+3V3_MCU` | STM32H743 @ 440 MHz, VOS0 | ~300 mA (verify vs datasheet for peripheral mix) |
| `+3V3_PERIPH` | TFT + backlight | ~100 mA |
| | SD card (write bursts) | ~100 mA peak |
| | QSPI flash | ~15 mA |
| | misc / level shifter A-side | ~30 mA |
| `+5V_BUS` | 74HC595 / '3245 VCCB | ~50 mA |
| **Total from +5V_RAIL** | | **~600–650 mA** |

### ⚠ 2.1 This exceeds the USB budget

Plain 5.1 kΩ Rd resistors get the **USB default of 500 mA**. At ~650 mA the
board cannot run its full feature set from USB alone. Three ways out — decide
before layout:

1. **Reduce load in USB-only mode.** Firmware already knows the power state
   (`VBUS_SENSE` + `VIN_SENSE`). Blank or heavily dim the TFT backlight and skip
   SD access when running on USB. Cheapest option, no extra hardware.
2. **CC voltage sensing or a PD controller** to negotiate more than 500 mA.
   Significant design change; only worth it if 1 is insufficient.

> ### ❌ The internal SMPS is NOT an option on this package
>
> The STM32H743 die has an internal SMPS, but **it is not bonded out on the
> LQFP144 package.** The LQFP144 power pins are VDD, VSS, **VCAP × 2**, VBAT,
> VDDA, VSSA, VREF+, PDR_ON, NRST, BOOT0 — there is no `VLXSMPS`, `VDDSMPS`,
> `VSSSMPS` or `VFBSMPS`.
>
> The **VCAP pins are the tell**: they are the internal LDO's output decoupling.
> A package with SMPS exposes the switching node instead, and ST denotes such
> packages with an `_SMPS` suffix in the datasheet pinout figures.
>
> `SupplySource = PWR_LDO_SUPPLY` in the `.ioc` is therefore not just correct,
> it is the only possibility. Nothing to design in, nothing to decide.

**Recommended: implement 1 (firmware load-shedding), and measure real current at
bring-up before assuming it is needed at all.** The ~650 mA estimate above is
deliberately conservative — realistic steady-state is likely 350–450 mA, which
fits inside the USB default with margin.

---

## 3. Stage detail

### 3.1 12 V input protection

| Ref | Part | Purpose |
|---|---|---|
| F1 | 1 A slow-blow fuse or PTC | fault current limit |
| D_rev | Schottky series **or** P-FET | reverse-polarity protection (P-FET preferred — no drop) |
| TVS | SMAJ15A or similar | transient clamp, 15 V standoff |
| C_in | 100 µF electrolytic + 100 nF | bulk + HF |

Input never exceeds 12 V by design (zener clamped upstream).

### 3.2 DC/DC 12 V → 5 V — bought module

Linear regulation is not an option here: 12→5 V at 700 mA dissipates ~4.9 W.
Rather than lay out a switcher, use an encapsulated module — no inductor to
place, no feedback network, switching noise contained inside the can.

| Item | Value |
|---|---|
| Part | **Recom R-78E5.0-1.0** |
| Package | SIP-3, **7805-compatible pinout** (through-hole) |
| In / Out | 7–28 V in (12 V nominal) / **5.0 V, 1 A** |
| Efficiency | ~90% |
| Cost | ~$5–7 |

Alternatives with the same footprint: **Traco TSR 1-2450**, or **Murata
OKI-78SR-5/1.5-W36-C** for 1.5 A.

Input/output caps per the datasheet. For a scientific instrument add an **LC
post-filter** (2.2 µH + 22 µF) on the module output to keep switching residue
off the analog rails — five ADC channels justify it.

> **Note:** the module is a fixed 5.0 V output, so the earlier "set the buck to
> 5.1 V so it wins the diode-OR" trick is unavailable. The TPS2116 below solves
> source priority explicitly instead, which is a better answer anyway.

### 3.3 Power mux — TPS2116

Replaces the diode-OR entirely. An N-channel MOSFET mux with **explicit
priority**, not a passive OR that depends on which source happens to be higher.

| Item | Value |
|---|---|
| Part | **TPS2116DRLR** (TI) |
| Package | SOT-563, 8-pin (DRL) |
| Range / current | 1.6–5.5 V, **2.5 A max** |
| On-resistance | **40 mΩ typ** |
| Cost | ~$0.90 |

**Connections:**

| Pin | Connect to |
|---|---|
| VIN1 | **+5V_SYS** (Recom output) — the **priority** source |
| VIN2 | **USB VBUS** |
| PR1 | tie to **VIN1** → selects automatic priority mode |
| VOUT | **+5V_RAIL** |
| ST | **PF3** (GPIO) — open drain, needs a pull-up |
| GND | GND |

In automatic priority mode the device prioritises VIN1 and switches to VIN2 only
when VIN1 drops. So 12 V always wins when present; USB is used only when the
instrument is not otherwise powered. Deterministic, and no diode drops.

**Three things this buys beyond priority:**

1. **Reverse-current blocking when VOUT > VINx** — this enforces the hard rule
   that *USB 5 V must never back-feed the 12 V rail*, in silicon.
2. **The ST pin is a free power-source indicator.** Open-drain, pulled low when
   VIN1 is not in use — i.e. it tells firmware directly whether the board is
   running from 12 V or USB. Route it to a GPIO (**PF3**, spare) with a 10 kΩ
   pull-up to +3V3. This is the authoritative "what is actually powering me"
   signal, more direct than inferring it from `VIN_SENSE` + `VBUS_SENSE`.
3. **Controlled output slew rate / soft start** when the output is below 1 V,
   limiting inrush into the bulk capacitance at plug-in. Check the datasheet for
   the minimum output capacitance needed for linear soft-start behaviour;
   22 µF is a reasonable starting point.

### 3.4 ⚠ The dropout budget — LDO selection still matters

The mux is a large improvement over diode-OR here. At 40 mΩ and ~650 mA the drop
is **~26 mV**, versus ~350 mV through a Schottky:

```
  4.75 V   USB VBUS (spec minimum at the port)
 −0.30 V   cable + connector drop
 −0.03 V   TPS2116 (40 mΩ)
 ────────
  4.42 V   at the LDO input
 −3.30 V
 ────────
  1.12 V   available dropout
```

That is far healthier than the 0.80 V a diode-OR would have left. **Still use a
true low-dropout part** — 1.12 V leaves no usable margin over a 1.1–1.2 V
dropout regulator (AMS1117, LD1117, NCP1117, AZ1117, TLV1117), and those would
fail *only* on USB power, so the board would work perfectly on the bench from
12 V and die when someone runs it from a laptop.

| Ref | Part | Package | Dropout | Rail |
|---|---|---|---|---|
| LDO1 | **AP2114H-3.3TRG1** | SOT-223 | ~350 mV @ 1 A | `+3V3_MCU` |
| LDO2 | **AP2114H-3.3TRG1** | SOT-223 | ~350 mV @ 1 A | `+3V3_PERIPH` |

With the TPS2116 there is ~770 mV of margin at worst case — comfortable.

At 350 mV dropout there is ~450 mV of margin in the worst case. Both are 1 A
parts against ~300 mA loads, so dropout in practice is well under the 1 A figure.

**Dissipation** (5.0 V in, 3.3 V out): LDO1 ~0.54 W, LDO2 ~0.44 W. SOT-223 with
a decent copper pour on the tab handles this — allow ~1 cm² per regulator.

Each LDO: 10 µF in, 22 µF out (check the datasheet for ESR/stability
requirements), plus 100 nF close to the pins.

### 3.5 Rail allocation — what goes where

The two LDOs are fed from the same `+5V_RAIL`; their **outputs are separate
rails that never connect**. The split is by load, at the controller/display
boundary:

| Rail | Powers | Est. current | Dissipation |
|---|---|---|---|
| **`+3V3_MCU`** (LDO1) | STM32 (all VDD pins, VDDA via ferrite, VREF+, VDD33USB), QSPI flash, level-shifter 3.3 V side, LEDs, pull-ups | ~375 mA | ~0.64 W |
| **`+3V3_PERIPH`** (LDO2) | Display module across its connector — TFT, backlight, SD card | ~200 mA | ~0.34 W |

**Why this boundary.** The display module holds the bursty loads (backlight
switching, SD write bursts) and is the one group that crosses a connector, so it
separates naturally. QSPI flash and the level shifters stay on the MCU rail
despite being "peripherals" — they are tightly coupled to MCU signals and draw
very little.

**Why split at all — thermal, primarily.** ~600 mA at a 1.7 V drop is ~1 W. In a
single SOT-223 that is roughly a 55 °C rise over ambient; inside an enclosure at
40 °C the junction approaches 95 °C — uncomfortable for an instrument running
continuously. Split, each package sees ~30 °C rise and sits near 70 °C.
Transient isolation of the SD/backlight bursts is a bonus, not the main reason.

> ### ⚠ The two rails must track each other
>
> Every load on `+3V3_PERIPH` has signal connections back to the MCU on
> `+3V3_MCU`. If the rails diverge during ramp-up or ramp-down, current flows
> through I/O protection diodes on whichever side is lower. This is routine and
> safe **provided**:
>
> - **Same LDO part for both** — identical response
> - **Same input, no independent enables** — they come up and down together
> - **Similar output capacitance** on each so ramp rates match
>
> **Do not stagger them or add sequencing logic** — that creates the very rail
> delta you are trying to avoid.

Note that **GPIO drive current comes from VDD**, so LEDs and everything the MCU
drives load `+3V3_MCU` regardless of grouping. Already included above.

### 3.6 STM32H743 power pin connections

```
+3V3_MCU ─┬──────────────────────────────────► VDD (all pins), 100 nF each
          │
          ├──[FB1: ferrite 600 Ω @ 100 MHz]──┬─► VDDA
          │                                   ├─ 1 µF
          │                                   └─ 100 nF
          │
          ├──[FB2: ferrite]──────────────────┬─► VDD33USB
          │                                   ├─ 1 µF
          │                                   └─ 100 nF
          │
          └──────────────────────────────────► VREF+ (via VDDA, or see below)
```

| Pin | Supply | Decoupling |
|---|---|---|
| VDD (×~8) | `+3V3_MCU` direct | **100 nF at every pin** + 4.7 µF bulk |
| VDDA | via ferrite bead FB1 | 1 µF ‖ 100 nF |
| VREF+ | **REF3033 3.0 V series reference** — see below | 1 µF ‖ 100 nF |
| VDD33USB | via ferrite FB2 | 1 µF ‖ 100 nF |
| VBAT | CR2032, **no series diode** | 100 nF |
| VCAP1, VCAP2 | **internal LDO output** — nothing external drives these | ~2.2 µF X7R each, hard against the pins |
| VSSA | ground plane | — |
| PDR_ON | tie to VDD (normal operation) | — |

**Do not give VDDA its own regulator.** The H7 constrains how far VDDA may drift
from VDD during power-up and operation; two independent regulators will violate
that at startup however carefully matched. The correct isolation is **filtering,
not separate regulation** — hence the ferrite bead.

**Ground: do not split the plane.** Use one solid ground plane with the analog
parts physically grouped over their own region. A split plane creates
return-path discontinuities that usually make noise worse. Keep the VDDA and
VREF+ decoupling tight to their pins with short returns.

#### VREF+ — dedicated 3.0 V reference  ✅ DECIDED

**ADC full scale is 3.0 V, not 3.3 V.** VREF+ is driven by a dedicated series
voltage reference rather than tied to VDDA, so ADC accuracy is set by the
reference (~0.1%) instead of the LDO's tolerance (~±2%). VREF+ may legitimately
sit below VDDA, so 3.0 V against a 3.3 V VDDA is a valid configuration.

| Item | Value |
|---|---|
| Part | **REF3033** (TI, 3.0 V, SOT-23-3) — or ADR3430 / MAX6033 |
| Supply | `+3V3_MCU` (needs ≥ ~3.2 V input; 3.3 V is adequate) |
| Decoupling | 1 µF ‖ 100 nF at VREF+, close to the pin |

Use a **series** (buffered bandgap) reference, not a shunt type — VREF+ sources
current to the SAR ADC in transient bursts during conversion, and a series part
handles that without the sizing exercise a shunt reference's series resistor
needs.

> **⚠ Disable VREFBUF.** The STM32H743 contains an internal reference buffer
> (VREFBUF) that can drive VREF+ internally. It **must be disabled** when an
> external reference drives the pin, or the two will fight. This is a CubeMX /
> firmware setting, not a hardware one — but it is easy to miss and the symptom
> (wrong, drifting ADC readings) does not obviously point at it.

**⚠ Two knock-on consequences of a 3.0 V full scale:**

1. **`VIN_SENSE` divider re-scaled** — see §3.9.
2. **The four external ADC inputs** (PC0, PC1, PC4, PC5 on the ADC connector)
   now clip at **3.0 V, not 3.3 V**. Any input conditioning or scaling on those
   channels must be designed against 3.0 V full scale. Check this against the
   Rev 5.4 ADC input circuitry before carrying it over.

### 3.9 VIN_SENSE divider — scaled for 3.0 V reference

| Item | Value |
|---|---|
| R1 (VIN → pin) | **39 kΩ** |
| R2 (pin → GND) | **10 kΩ** |
| Voltage at 12 V in | **2.449 V** — 81.6% of the 3.0 V full scale |
| Thévenin impedance | ~8.0 kΩ — comfortable for H7 sampling |
| Current draw | ~245 µA at 12 V |
| Filter | **100 nF** at the ADC node |
| Scale factor (firmware) | `Vin = Vadc × 4.9` |

Replaces the earlier 30k/10k, which gave exactly 3.0 V at 12 V input — precisely
full scale, with no headroom for tolerance or an input running slightly high.
39k/10k leaves ~18% margin.

Goes to **PF11** (`VIN_SENSE`, ADC1_INP2).

> Verify pin counts and the reference power schematic in **ST AN4938**,
> "Getting started with STM32H74xI/G and STM32H75xI/G MCU hardware development"
> (Rev 7, Oct 2024) — the definitive hardware design guide for this family.
> Also the source for the VCAP values and the VBAT startup caveat.

### 3.7 VBAT

CR2032 direct to VBAT, 100 nF close to the pin, **no series diode** (the H7 has
internal VDD→VBAT switchover).

> **VBAT must never float.** Provide a solder jumper / 0 Ω selecting VBAT = cell
> **or** VBAT = 3V3, so an unpopulated holder still boots.

### 3.8 Bus 5 V

Modules do not draw from `+5V_BUS` by design, but EXT1 pin 2 and EXT2 pin 2
still present it. **Fit a PTC resettable fuse (~500 mA)** so a shorted or faulty
module cannot pull down the controller.

---

## 4. Sequencing and decoupling

- The two 3.3 V rails come up together (same input, no independent enables) — no
  sequencing logic needed. See §3.5 for why staggering them would be actively
  harmful.
### 4.1 ⚠ VCAP capacitors — mandatory

The LQFP144 has **two VCAP pins**, the output decoupling for the internal core
LDO. **The part will not run reliably without them** — this is not optional
decoupling, it is the regulator's compensation network.

| Item | Value |
|---|---|
| C_VCAP1, C_VCAP2 | ~2.2 µF X7R ceramic each |
| Placement | as close to the pins as physically possible |
| Return | short, wide path to VSS |

**Confirm the exact value and ESR window in ST AN4938**, "Getting started with
STM32H74xI/G and STM32H75xI/G MCU hardware development" — the authoritative
document for this family, worth having open during schematic capture.

### 4.2 General decoupling

- **100 nF per VDD pin**, placed at the pin.
- Bulk: 22 µF per 3.3 V rail near the load, in addition to the LDO output caps.

---

## 5. Test points

Worth fitting, cheap now and invaluable at bring-up:

`+12V_IN`, `+5V_SYS` (module out, before the mux), `+5V_RAIL` (mux output),
`+3V3_MCU`, `+3V3_PERIPH`, `VDDA`, `VBAT`, `GND` (several).

---

## 6. Decisions still open

1. **External ADC input scaling** — with a 3.0 V reference the four ADC-connector
   inputs (PC0/PC1/PC4/PC5) clip at 3.0 V, not 3.3 V. Verify the Rev 5.4 input
   conditioning still suits before carrying it over.
2. **TFT backlight supply and current** — verify against the actual display
   module; it may want 5 V or a constant-current drive, which changes the
   `+3V3_PERIPH` budget.
3. **Reverse-polarity protection: Schottky or P-FET** — P-FET avoids the drop and
   the dissipation, at slightly more complexity.
4. **Measure, then confirm.** The MCU current figure is an estimate. Take a real
   measurement at bring-up before declaring the USB-only mode viable.
