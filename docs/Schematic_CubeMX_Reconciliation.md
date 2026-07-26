# MIPS Rev 6.0 — Schematic ↔ CubeMX ↔ Docs Reconciliation Punch List

The single burn-down list to bring the three artifacts into agreement and close
out the Rev 6.0 hardware design. When every box here is checked, the schematic,
the CubeMX `.ioc`, and `MIPS_Rev6_PinMap.md` are consistent.

**Rule of authority for this pass:** the **schematic is authoritative** for the
digital-output (595) subsystem and the power train (the board reflects decisions
the older docs had not caught up to). CubeMX and the docs are updated to match.

**How to use it:** do section A (two decisions) first, then batch all of
section B into a **single** CubeMX regeneration, then the schematic edits (C),
then regenerate the docs (E). Sections D/F are reference.

---

## A. Decisions to make first

- [ ] **A1 — JP2 default latch strap: LDAC or RCK?**
  The 595 storage-clock (RCLK) is jumper-selectable (JP2) between the
  edge-detector `LDAC` and the CPU line `RCK_DO_IP`.
  **Recommendation: default to LDAC.** It puts the 16 digital outputs under the
  pulse sequence generator (DO latches synchronously with DAC updates at hardware
  timing) — the capability the design was aiming for. Keep `RCK_DO_IP` populated
  as the documented hedge. *Firmware consequence of LDAC strap:* the 595 contents
  must be valid at all times, and `SPIQueue` must complete the shift **before**
  any pulse-engine LDAC edge (never race it). If RCK is ever strapped instead,
  the DO latch becomes independent of the pulse engine and that ordering rule
  relaxes.

- [ ] **A2 — Does `AUX1_IO` need to be interrupt-capable?**
  **Recommendation: place it on PB1** (`GPIO_Input`). PB1 is the only spare pin
  on the last free EXTI line (line 1), so this keeps the interrupt option open
  and lets the bus-I/O layer promote it at runtime like a BIO pin. If `AUX1_IO`
  is strictly a read/write line, any spare works and line 1 can stay open —
  but PB1 costs nothing to reserve now. *This list assumes PB1.*

---

## B. CubeMX (`.ioc`) changes — apply ALL, then regenerate once

Digital-output control (Port G — same three pins, re-assigned):

- [ ] **PG5**: rename `OE_DO_AH` → **`OE_DO_AP`** (single output-enable, all DO A–P). GPIO_Output.
- [ ] **PG6**: was `OE_DO_IP` → **`DOSR_CLR`** (595 shift-register clear, ~SRCLR). GPIO_Output. Idle **high** (inactive) at reset.
- [ ] **PG7**: was `OE_DI` → **`RCK_DO_IP`** (595 latch source, jumper-selectable per A1). GPIO_Output.
- [ ] Confirm **PG0 / PG1 stay unassigned** (RCK is now on PG7; the old PG0/PG1 RCK lines are gone).

LDAC:

- [ ] **PA2**: add GPIO label **`LDAC_TOGGLE`** (pin already assigned TIM2_CH3; it drives the edge detector, not LDAC directly). PA3 = `LDAC_CTRL` already correct.

New signals from sheet 1 that had no CPU pin:

- [ ] Add **PF3** — `GPIO_Input`, label **`PWR_SRC`** (TPS2116 `ST`; already wired on the schematic, missing in CubeMX). External 10 kΩ pull-up on the board.
- [ ] Add **PF6** — **`TIM16_CH1`** PWM, label **`TFT_BR`** (TFT backlight brightness). **Enable TIM16.** Low-frequency PWM; uses a currently-free timer so it is not tied to any rate-critical timebase.
- [ ] Add **PB1** — `GPIO_Input`, label **`AUX1_IO`** (per A2). Sits on EXTI line 1 (last free line).

Power / USB (see D1):

- [ ] **PA9 / VBUS** — resolve per **section D**. Recommended: change from
      `USB_OTG_FS_VBUS` (native, `Activate_VBUS`) to plain **`GPIO_Input`**,
      label **`VBUS_SENSE`**, and set `vbus_sensing_enable = DISABLE`.

ADC (only 1 of 5 channels is currently in the conversion sequence):

- [ ] Add regular-conversion ranks for **PC1 (INP11), PC4 (INP4), PC5 (INP8), PF11 (INP2 = `VIN_SENSE`)**. Today only PC0/INP10 converts, so `VIN_SENSE` and three external inputs are never sampled as generated.
- [ ] Confirm **VREFBUF is disabled** (external REF3033 drives VREF+; the internal buffer must be off). Default is off, but verify — the failure symptom is drifting ADC readings that don't point at the cause.

Then:

- [ ] Regenerate, and run the **CubeMX regeneration procedure** (`CubeMX_Regeneration_Notes.md §2`): PendSV handling, `PWR_REGULATOR_VOLTAGE_SCALE0`, clock tree, PA2/TIM2 AF, label check, clean `pio run`.

---

## C. Schematic changes

### Sheet 2 (MCU + power) — `MIPS_Rev6_0_2.kicad_sch`

- [ ] **Connect the dangling sheet-1 control nets to their new MCU pins.** These
      nets exist at the peripherals on sheet 1 but have **no MCU-side global
      label**, so they are currently unconnected across the two flat sheets. Add
      matching global labels on the MCU pins:
  - [ ] `OE_DO_AP` → PG5
  - [ ] `DOSR_CLR` → PG6
  - [ ] `RCK_DO_IP` → PG7
  - [ ] `TFT_BR` → PF6
  - [ ] `AUX1_IO` → PB1
  - [ ] (verify `PWR_SRC`/`ST` on PF3 and `LDAC_TOGGLE`/`LDAC_CTRL` on PA2/PA3 are already labelled)
- [ ] **VBUS sense divider** (per section D): add **100 kΩ (VBUS→pin) / 200 kΩ (pin→GND) + 100 nF** into PA9. Do **not** connect 5 V VBUS directly to PA9.
- [ ] **Config flash P/N**: schematic is `W25Q32JVSS` (32 Mbit); docs say `W25Q80DV` (8 Mbit). Pin-compatible QSPI — pick one and make the BOM + `Hardware_Design_Checklist §7` agree. (32 Mbit is a fine choice; just align the docs.)

### Sheet 1 (bus / DIO / display) — `MIPS_Rev6_0.kicad_sch`

- [ ] Confirm `OE_DO_AP` (single) drives both DO level shifters and there is no DI output-enable — **already correct**, no change.
- [ ] Confirm the TFT backlight pin (DIS1) is on the `TFT_BR` net so the PF6 PWM reaches it.
- [ ] Confirm `AUX1_IO` routes to its intended destination.
- [ ] Both trigger outputs (`TRG_OUT` PC6, `AUX_TRGOUT` PD15) idle **inactive** at reset — remember both are inverted in hardware.

---

## D. Cross-cutting hardware decision — VBUS sensing on PA9

This is the last real hardware question. Today the `.ioc` uses native
`USB_OTG_FS_VBUS` on PA9, and the schematic has **no VBUS divider** — VBUS only
reaches the fuse → TPS2116 VIN2.

The board is self-powered from 12 V and can sit **unpowered with USB still
plugged in**. In that state 5 V on a 5 V-tolerant pin exceeds the VDD+4 V
absolute-max (ceiling is 4 V when VDD = 0) — ST states this can permanently
damage the pin. The native VBUS threshold (4.25 V) can't be satisfied by any
divider low enough to be safe at VDD = 0, so you can't use both.

- [ ] **Resolve:** add the **100 kΩ / 200 kΩ divider (+100 nF) into PA9 as plain
      `GPIO_Input` `VBUS_SENSE`**, native sensing disabled, D+ pull-up driven in
      firmware via `HAL_PCD_DevConnect/DevDisconnect()` from the GPIO state.
      (Full rationale: `Hardware_Design_Checklist §2.3.1`.) Poll it — no EXTI
      (line 9 is `IN_T`). This is required for a self-powered device anyway.
- [ ] Alternative, only if VBUS state is genuinely never needed in firmware: drop
      VBUS sensing entirely and free PA9. Not recommended — the power-state logic
      wants it.

---

## E. Doc updates (after CubeMX regenerates clean)

- [ ] **Regenerate `MIPS_Rev6_PinMap.md` from the corrected `.ioc`.** It will then
      carry: PG5 `OE_DO_AP`, PG6 `DOSR_CLR`, PG7 `RCK_DO_IP`; PA2 `LDAC_TOGGLE`;
      new PF3 `PWR_SRC`, PF6 `TFT_BR` (TIM16), PB1 `AUX1_IO`; PA9 `VBUS_SENSE`;
      no PG0/PG1. While regenerating, fix the pre-existing text bugs: **§2 Port F
      VIN divider 30k → 39k** (matches `Vin = Vadc × 4.9`), and the **§5 spare
      list** (drop PA9/PA3, remove the duplicate PA8/PA10/PA15, remove PB1/PF3/PF6
      now in use).
- [ ] **`Hardware_Design_Checklist.md §3`**: single `OE_DO_AP` (delete the AH/IP
      split), no DI enable, add `DOSR_CLR`, RCK now on PG7 (not PG0/PG1), add
      `TFT_BR` (PF6/TIM16 backlight PWM) and `AUX1_IO` (PB1).
- [ ] **`MIPS_Bus_Signal_Map.md §4`**: EXTI line 1 = `AUX1_IO` (if PB1). Note
      TIM16 is now allocated (TFT backlight); TIM6/13/14/17 remain free.
- [ ] **Delete the three stale `.docx`** planning files: `MIPS_Rev6_Firmware_Plan.docx`,
      `MIPS_Rev6_PinPlanning_Worksheet.docx`, `MIPS_STM32H7_Design_Spec.docx`.
      (`CLAUDE_HANDOFF.md` is **kept** — it holds the settled scheduler design and
      Module base-class direction, which live nowhere else. Optionally trim it to
      just those firmware-design sections and rename to `FIRMWARE_DESIGN_NOTES.md`.)

---

## F. Reference — allocation after these changes

**Port G control trio (unchanged pin count):** PG5 `OE_DO_AP`, PG6 `DOSR_CLR`, PG7 `RCK_DO_IP`. PG0/PG1 free.

**Timers:** TIM1 RF PWM · TIM2 pulse engine (LL) · TIM3 encoder · TIM4/TIM5 continuous clocks · TIM7 delay/AUX_TRGOUT · TIM8 TRG_OUT burst · **TIM16 TFT backlight (new)**. Free: TIM6, TIM13, TIM14, TIM17.

**EXTI (16/16 after AUX1_IO on PB1):** line 1 = `AUX1_IO`; the other 15 = Q–X + BIO1–7 unchanged. Encoder stays on TIM3 quadrature (no EXTI). After this the interrupt controller is full — any new interrupt-capable signal must share a vector or displace one.

**Pin count:** ~88 → ~91 (add PF3, PF6, PB1; PA9 changes mode, not count).

---

## G. "Design done" gate

- [ ] A1 and A2 decided
- [ ] Section B applied, CubeMX regenerates clean (`pio run`)
- [ ] Section C schematic edits done; **KiCad ERC clean** (no dangling `OE_DO_AP` / `DOSR_CLR` / `RCK_DO_IP` / `TFT_BR` / `AUX1_IO`)
- [ ] VBUS/PA9 resolved (D)
- [ ] `MIPS_Rev6_PinMap.md` regenerated; stale 30k / spare-list bugs gone
- [ ] Flash P/N aligned across schematic + docs
- [ ] Stale `.docx` + handoff deleted from the repo
