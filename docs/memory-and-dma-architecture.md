# Memory and DMA architecture — MIPS Rev 6.0 (STM32H743ZIT6)

Status: design rule, applies to all firmware in this repo
Applies to: MIPS Rev 6.0 controller, and any other GAACE project on STM32H7

---

## Why this document exists

The SAM3X8E used in MIPS Rev 5 and earlier had one SRAM, no cache, and a DMA controller (PDC) that could reach all of it. A buffer was a buffer.

The STM32H743 breaks both of those assumptions. Memory is split across power and bus domains, some of it is unreachable by DMA, and the Cortex-M7 has a data cache that does not observe DMA traffic. Both problems produce silent wrong behaviour rather than compile errors or hard faults, and both look like analog or peripheral problems when you meet them at the bench.

These rules are cheap to follow from the start and expensive to retrofit once a streaming pipeline is built on top of them.

---

## 1. The memory map

| Region | Base | Size | Domain | Cached | Reachable by DMA1/DMA2 |
|---|---|---|---|---|---|
| ITCM | `0x00000000` | 64 KB | Core | No | No |
| DTCM | `0x20000000` | 128 KB | Core | No | **No** |
| AXI SRAM | `0x24000000` | 512 KB | D1 | Yes | Yes |
| SRAM1 | `0x30000000` | 128 KB | D2 | Yes | Yes |
| SRAM2 | `0x30020000` | 128 KB | D2 | Yes | Yes |
| SRAM3 | `0x30040000` | 32 KB | D2 | Yes | Yes |
| SRAM4 | `0x38000000` | 64 KB | D3 | Yes | Yes |
| Backup SRAM | `0x38800000` | 4 KB | D3 | Yes | Yes |

Two exceptions worth knowing:

- **MDMA** can reach TCM. Regular DMA1/DMA2 cannot.
- **BDMA** lives in D3 and is intended for D3 peripherals (LPUART1, I2C4, SPI6, LPTIM). Its natural buffer home is SRAM4, not D2.

---

## 2. The two failure modes

### 2.1 DMA cannot reach TCM

DTCM and ITCM hang off private ports on the Cortex-M7 itself. They are not on the AHB/AXI bus matrix. DMA1 and DMA2 are bus masters — the matrix is the only way they see memory — so a DMA transfer targeting a DTCM address simply does not happen.

What makes this trap effective is that you never write the address down. A plain global array lands wherever the linker puts `.bss`, and depending on the linker script that is frequently DTCM at `0x20000000`. The identical source that works in an ST example fails here because the two linker scripts placed the default RAM region differently.

**Symptom:** buffer stays at its initial value. `HAL_DMA_ERROR_TE` may be set if you check for it. No fault, no warning.

### 2.2 The D-cache does not observe DMA

The M7 has a 16 KB write-back data cache sitting between the core and everything reached through the bus matrix. DMA goes through the matrix directly to SRAM and never notifies the cache.

- **DMA writes, CPU reads** (ADC capture, SPI receive): the CPU serves the read from a cache line loaded earlier and returns stale data.
- **CPU writes, DMA reads** (DAC output, SPI transmit, USB TX): the CPU's write sits dirty in the cache and has not reached SRAM. DMA transmits the old contents.

**Symptom:** values are plausible but frozen, lagging, or updating in bursts. This is the dangerous one — it looks exactly like a hardware problem, and you will go probe the front end before you suspect memory.

---

## 3. The rule for this repo

> **Anything a peripheral touches lives in D2 SRAM, inside a non-cacheable MPU region.
> Anything only the CPU touches can live in DTCM or ITCM.**

That division is clean because the two memories have complementary properties:

- TCM is never cached, so it has no coherency hazard at all — but DMA cannot see it.
- D2 SRAM is DMA-local (no domain crossing to reach DMA1/DMA2 or the D2 peripherals) — but it is cached by default, so we turn caching off for it via the MPU.

We use an MPU region rather than manual cache maintenance. Manual `SCB_CleanDCache_by_Addr()` / `SCB_InvalidateDCache_by_Addr()` is faster, but it operates on 32-byte cache lines: a buffer that is not 32-byte aligned and size-rounded will also invalidate neighbouring variables, and if those held dirty CPU writes they are silently lost. With the number of DMA paths this project will grow, that is not a trade worth making.

### Placement decisions for MIPS Rev 6.0

| Data | Location | Reason |
|---|---|---|
| ADC1 circular buffer (DMA1_Stream0) | D2, non-cacheable | Live DMA write target |
| SPI2 module-bus TX/RX buffers | D2, non-cacheable | Planned DMA |
| QUADscan binary capture buffers | D2, non-cacheable | Planned DMA, high rate |
| USB CDC endpoint buffers | D2, non-cacheable | USB is a bus master |
| Scan engine state, tables, calibration | DTCM | CPU-only, zero wait state |
| Hot ISR code | ITCM | CPU-only, deterministic |
| General heap, application data | AXI SRAM | Bulk, cached, fine |

---

## 4. Linker script

Add the D2 region and a dedicated section. Uninitialised (`NOLOAD`) — DMA buffers do not need startup content and there is no reason to carry them in the image.

```ld
MEMORY
{
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  DTCMRAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2  (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3  (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
}

SECTIONS
{
  .dma_buffers (NOLOAD) :
  {
    . = ALIGN(32);
    _sdma_buffers = .;
    *(.dma_buffers)
    *(.dma_buffers*)
    . = ALIGN(32);
    _edma_buffers = .;
  } >RAM_D2
}
```

The 32-byte alignment at both ends is not strictly required once the region is non-cacheable, but it keeps the option of manual maintenance open and costs nothing.

---

## 5. MPU configuration

Must run **before** the caches are enabled — earliest point in `main()`, ahead of `HAL_Init()`, or in `SystemInit()`.

A 512 KB region at `0x30000000` covers SRAM1, SRAM2, and SRAM3 in one shot. ARMv7-M requires power-of-two size and natural alignment; `0x30000000` is a multiple of 512 KB, so this is legal.

```c
void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    HAL_MPU_Disable();

    /* D2 SRAM: normal memory, non-cacheable. All DMA buffers live here. */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x30000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    MPU_InitStruct.SubRegionDisable = 0x00;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
```

Notes on the attribute choices:

- `TEX=1, C=0, B=0` gives **Normal, non-cacheable** memory. Do not use `TEX=0, C=0, B=1` (Device memory) — Device forbids unaligned access and imposes ordering rules that will bite `memcpy` and struct access.
- `MPU_PRIVILEGED_DEFAULT` keeps the default memory map active as a background region, so everything outside `0x30000000` behaves normally.
- On ARMv7-M, **higher region numbers win** where regions overlap. If you later add regions, keep this one at a number above any broader background region you define.
- `DisableExec` is set because nothing should ever execute from a DMA buffer.

Then enable caches as usual:

```c
SCB_EnableICache();
SCB_EnableDCache();
```

---

## 6. Declaring a buffer

```c
/* mips_memory.h */
#define DMA_BUFFER  __attribute__((section(".dma_buffers"), aligned(32)))
#define DTCM_DATA   __attribute__((section(".dtcm")))
#define ITCM_FUNC   __attribute__((section(".itcm"), noinline))
```

```c
/* adc.c */
#define ADC_CHANNELS  5
#define ADC_DEPTH     256

DMA_BUFFER static volatile uint16_t adc_samples[ADC_CHANNELS * ADC_DEPTH];
```

`volatile` is still required. The MPU region solves cache coherency; it does not stop the compiler from caching the value in a register across a loop.

---

## 7. Runtime guard

Cheap insurance against the DTCM trap. Wrap every DMA start, or at minimum assert once during init.

```c
static inline bool dma_reachable(const void *p, size_t len)
{
    uintptr_t a = (uintptr_t)p;
    uintptr_t e = a + len;

    /* AXI SRAM (D1) */
    if (a >= 0x24000000u && e <= 0x24080000u) return true;
    /* SRAM1..SRAM3 (D2) */
    if (a >= 0x30000000u && e <= 0x30048000u) return true;
    /* SRAM4 (D3) */
    if (a >= 0x38000000u && e <= 0x38010000u) return true;

    return false;   /* DTCM, ITCM, flash, or off the map */
}

#define ASSERT_DMA_OK(buf) \
    configASSERT(dma_reachable((buf), sizeof(buf)))
```

Called at init, this turns a silent three-hour debugging session into an immediate assertion at a known line.

---

## 8. Symptom table

| What you see | Likely cause |
|---|---|
| Buffer never changes from its initial value | Buffer in DTCM — DMA cannot write it |
| `HAL_DMA_ERROR_TE` set on first transfer | Same |
| Values plausible but frozen or lagging | Stale D-cache read; buffer is cacheable |
| Values update only when you add a `printf` | Same — the `printf` evicted the cache line |
| DAC or SPI transmits old data | Dirty cache line never written back before DMA read |
| An unrelated variable gets corrupted | `InvalidateDCache` on an unaligned or unrounded buffer |
| Works at `-O0`, fails at `-O2` | Missing `volatile`, or a coherency bug the low optimisation level was hiding |

---

## 9. Review checklist

Before merging any change that adds or moves a DMA transfer:

- [ ] Buffer is declared with `DMA_BUFFER`
- [ ] `dma_reachable()` assertion covers it
- [ ] Buffer is `volatile` if the CPU polls it rather than using the transfer-complete callback
- [ ] For circular mode, half-transfer and full-transfer callbacks read the correct half
- [ ] If the peripheral is in D3 (LPUART1, I2C4, SPI6, LPTIM), the buffer is in SRAM4 and BDMA is used, not DMA1/DMA2
- [ ] MPU region still covers the buffer's address after any linker script change

---

## 10. Open items for MIPS Rev 6.0

- ADC1 sampling time is currently `ADC_SAMPLETIME_1CYCLE_5` on channels 1–4. Those channels are fed from 10 K / 10 K dividers (~5 K source impedance) with no capacitor at the pin. This is a separate defect from anything in this document, but it produces similar-looking wrong readings and should be fixed at the same time so the two are not confused. Channel 5 (VIN_SENSE) is at 64.5 cycles and is fine.
- ADC kernel clock is configured at 100 MHz. The H743 ADC maximum is 50 MHz. `ClockPrescaler` is not set explicitly in the `.ioc` and is sitting at the CubeMX default — set it deliberately.
- Only ADC1 currently has DMA configured. SPI2, USART, and QSPI will need it for the scan engine. Apply the rules above when they are added.

---

## References

- RM0433, STM32H742/743/753/750 reference manual — sections on the bus matrix, MPU, and cache
- AN4838 — Managing memory protection unit in STM32 MCUs
- AN4839 — Level 1 cache on STM32F7 and STM32H7
- PM0253 — Cortex-M7 programming manual
