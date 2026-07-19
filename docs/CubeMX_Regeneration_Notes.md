# CubeMX — Punch List and Regeneration Notes

Two things live here:

1. **§1 Punch list** — changes still to make in CubeMX (do these, regenerate, rebuild).
2. **§2 Regeneration procedure** — what to check/do EVERY time CubeMX regenerates.

Companion to `MIPS_Rev6_PinMap.md` (the as-built map). Update both together.

---

## 1. Punch list — pending CubeMX changes

### 1.1 RTC + battery backup  *(new capability)*

| Item | Setting |
|---|---|
| Timers & Clocks → **RTC** | Activate Clock Source ✔, Activate Calendar ✔ |
| Clock tree → RTC mux | **LSE** (32.768 kHz already configured on PC14/PC15) |

- Costs **no signal pins** — VBAT is a dedicated power pin on the LQFP-144.
- Gives a real timestamp for logged data plus **32 backup registers**
  (battery-retained words: last state, run counters, crash breadcrumbs).
- **Schematic:** CR2032 direct to VBAT + 100 nF close to the pin. The H7 has
  internal automatic VDD→VBAT switchover; ST reference designs use no series
  diode. **VBAT must never float** — if the holder may be unpopulated, provide a
  solder-jumper / 0 Ω selecting VBAT = cell or VBAT = 3V3.

### 1.2 `VIN_SENSE` — 12 V supply monitor  *(restores a Rev 5.4 feature)*

| Item | Setting |
|---|---|
| Analog → **ADC1** | add **IN2**, single-ended |
| Pin | **PF11** (`ADC1_INP2`) |
| Label | `VIN_SENSE` |

**Not PC2.** On the H743, PC2 is `PC2_C` — a dual-mode direct-analog pin routed
to **ADC3**, not ADC1; there is no ADC1_INP12 on this part (that is the F4-family
mapping). PF11 keeps the noisy power-entry sense trace away from the
PC0/PC1/PC4/PC5 measurement corner, and Port F has room to route it cleanly.

Rev 5.4 monitored the incoming 12 V through an R6/R7 divider into AD8 — a fifth
channel, separate from the four ADC-connector inputs (AD0–AD3). It was missed in
the first pass because only the connector channels were mapped.

**Schematic:** VIN max 12 V, zener clamped. Divider **30 kΩ / 10 kΩ** → 3.0 V at
12 V in (margin under the 3.3 V reference), Thévenin ≈ 7.5 kΩ which the H7
samples comfortably. Add **100 nF** at the ADC node.

Keep **PC2_C / PC3_C** in reserve as the low-impedance direct-analog channels
if a high-accuracy measurement is ever added.

### 1.3 USB VBUS sensing  *(re-enable — now a requirement)*

| Item | Setting |
|---|---|
| USB_OTG_FS → Parameter Settings | **Activate_VBUS = Enabled** |
| Pin | **PA9** (currently free) |

Originally disabled to free pins when USB was console-only. Now that the board
can be **self-powered from 12 V**, this is a spec requirement: a self-powered USB
device must not present its D+ pull-up when VBUS is absent.

`VBUS` + `VIN_SENSE` together resolve all four power states:

| VIN | VBUS | State |
|---|---|---|
| yes | yes | 12 V powered, host connected |
| yes | no | 12 V powered, standalone |
| no | yes | **USB-powered only** — inhibit 12 V-dependent functions |
| no | no | (off) |

### 1.4 DIO signals  *(from `DIO_Port_Review_and_Plan.md`)*

| Change | Pin | Setting |
|---|---|---|
| Add `LDAC_CTRL` | **PA3** | GPIO_Output — overrides the LDAC edge detector |
| Add `AUX_TRGOUT` | **PD15** | GPIO_Output — pulse width timed by TIM7. Inverted in hw |
| Label `TRG_OUT` | **PC6** | already TIM8_CH1; add the user label. Inverted in hw |
| **Free PG0 / PG1** | — | remove `RCK_DO_AH` / `RCK_DO_IP` — the DO 595s are latched by **LDAC**, not separate RCK lines |

Boot output levels matter here: both trigger outputs are **inverted in
hardware**, so set the GPIO Output Level so the lines idle *inactive* at reset.

### 1.5 Power-related pins

| Change | Pin | Setting |
|---|---|---|
| `PA9` → **plain GPIO_Input** | PA9 | was `USB_OTG_FS_VBUS`. Label `VBUS_SENSE`. Set USB `vbus_sensing_enable = DISABLE` and drive the D+ pull-up in firmware. **5 V must not reach PA9 directly** — see checklist §2.3.1 |
| Add `PWR_SRC` | **PF3** | GPIO_Input — TPS2116 `ST` pin, low when running from USB. External 10 kΩ pull-up |

### 1.6 ADC reference — VREFBUF must be disabled

VREF+ is driven by an **external REF3033 3.0 V reference**. The H743's internal
**VREFBUF must be disabled** or it will fight the external part.

Check in CubeMX under the ADC / VREFBUF settings that no internal reference
buffer is enabled. Symptom if missed: wrong and drifting ADC readings that do
not obviously point at the reference.

ADC full scale is therefore **3.0 V**, which firmware must use in all ADC
scaling maths — including `VIN_SENSE` (`Vin = Vadc × 4.9`, 39k/10k divider).

### 1.7 Still to decide — not yet actionable

- **USB-only inhibit control.** What must firmware hold off when running on USB
  power alone (HV, analog, module bus power)? If a hardware inhibit line is
  needed, that is another GPIO to allocate.
- **USB-C:** schematic only, no CubeMX/firmware impact. CC1 and CC2 each need a
  **5.1 kΩ pull-down**; double D+ to A6/B6 and D− to A7/B7 for reversibility.
  Plain Rd gives the 500 mA default — verify the USB-only current budget.
- **Power OR-ing:** USB 5 V and the 12 V buck output must never fight. Use a
  P-FET power mux / ideal-diode controller, not plain Schottkys. **USB 5 V must
  never back-feed the 12 V rail.** Rev 5.4's D2/D3/USBVCC circuit is the
  starting point.

### 1.8 Interrupts — current state and policy

**Nothing but core exceptions and SysTick is enabled.** No TIM, USART, I2C, SPI
or USB interrupts. That is deliberate; enable per phase:

| Peripheral | Where to enable | When |
|---|---|---|
| **TIM2** | **firmware** — `STM32PulseTimer` calls `HAL_NVIC_EnableIRQ(TIM2_IRQn)` and defines its own handler (LL peripheral, driver owns it) | Phase 4 |
| **EXTI 0/2/3/4, 9_5, 15_10** | **firmware** — bus-I/O module, one line-indexed dispatch table | Phase 2 |
| **USB OTG_FS** | **CubeMX** — HAL needs the generated handler calling `HAL_PCD_IRQHandler` | Phase 3 |
| TIM7 | decide at use | Phase 1/2 |
| UART / I2C / SPI | probably never — DeferredQueue drains blocking HAL calls from PendSV | — |

Default policy: **enable in firmware, not CubeMX** (survives regeneration, keeps
config beside the code). Exception: peripherals whose HAL stack requires the
generated ISR body — USB above all.

---

## 2. Regeneration procedure

Run through this EVERY time CubeMX regenerates.

### 2.1 The recurring edit — and how to eliminate it

CubeMX preserves code inside `USER CODE BEGIN/END` blocks but regenerates
everything else. `PendSV_Handler` in `Core/Src/stm32h7xx_it.c` is generated
**outside** those blocks, so the empty stub returns after every generation and
collides with the DeferredQueue's own definition (duplicate symbol at link).

**Permanent fix — do this once, then the edit never recurs:**

1. In the bus_queue library, rename the handler from `PendSV_Handler` to
   `DeferredQueue_PendSV()` and declare it in the header.
2. In the generated `stm32h7xx_it.c`, inside the PendSV stub's USER CODE block:

```c
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */
  DeferredQueue_PendSV();
  /* USER CODE END PendSV_IRQn 0 */
  ...
}
```

Because the call sits inside a USER CODE block, CubeMX keeps it. **Requires
"Keep user code when re-generating" to stay checked** in Code Generator settings.

Until that refactor: delete/comment the generated `PendSV_Handler` body by hand
after each regeneration.

### 2.2 Post-generation checklist

- [ ] `cubemx/Core/Src` contains per-peripheral files (`tim.c`, `spi.c`, `i2c.c`,
      `gpio.c` …). If the tree nested as `cubemx/cubemx/Core/...`, fix
      `src_dir`/`include_dir` in `platformio.ini`.
- [ ] **PendSV** — §2.1 handled (call present, or stub deleted).
- [ ] **Voltage scale** — `grep VOLTAGE_SCALE cubemx/Core/Src/main.c` must show
      `PWR_REGULATOR_VOLTAGE_SCALE0`. **SCALE1 = stop**: 440 MHz needs VOS0 and
      the PLL setup that follows assumes it.
- [ ] **Clock tree** — PLL1 M=5 N=176 P=2 Q=8; `RCC_HCLK_DIV2`; all four APB
      prescalers DIV2. Should match `MIPS_Rev6_PinMap.md` §1.
- [ ] **PA2 / TIM2_CH3 AF** — confirm `GPIO_AF1_TIM2` on `GPIO_PIN_2` is emitted.
      CH3 generates in *frozen* mode, so CubeMX may skip the pin config. Not
      fatal (the driver configures PA2 itself) but confirm.
- [ ] **Labels** — `grep _Pin cubemx/Core/Inc/main.h`; every user label should
      appear as a `_Pin` / `_GPIO_Port` pair. Bad identifiers surface here.
- [ ] `pio run` clean.

### 2.3 Build configuration (in `platformio.ini` — persists, not regenerated)

```ini
build_flags =
    -D USE_HAL_DRIVER
    -D USE_FULL_LL_DRIVER      ; REQUIRED - see below
    -D HSE_VALUE=25000000
    -Wall
```

**`USE_FULL_LL_DRIVER` is mandatory.** The LL init functions and their typedefs
(`LL_TIM_InitTypeDef`, `LL_GPIO_Init`, `LL_TIM_OC_Init` …) are wrapped in
`#if defined(USE_FULL_LL_DRIVER)` inside the ST headers. CubeMX puts this in its
Makefile, but PlatformIO does not read that — without the flag, `tim.c` fails to
compile with "unknown type name 'LL_TIM_InitTypeDef'". This is the cost of the
hybrid LL/HAL split.

**`lib_ignore = GAACE_Core`** is currently set. `SerialBuffer`, `Devices`,
`WireHelper` and `WireServer` on the `stm32` branch still `#include <Arduino.h>`
and break a pure-Cube build. **Permanent fix (Phase 1):** guard those four
modules' `.h`/`.cpp` bodies with `#if defined(ARDUINO) ... #endif`, consistent
with the existing `gaace_compat.h` dual-build approach. Then remove `lib_ignore`.

Minor: `RingBuffer.cpp` has a `-Wreorder` warning — `Commands` and `EOLchars`
are initialised in a different order than declared. Harmless now; worth fixing.
