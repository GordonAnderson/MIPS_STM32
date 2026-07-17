# lib/ — project-local libraries

PlatformIO auto-discovers each subfolder here as a library. These are the
board/HAL-specific pieces developed for MIPS_STM32 (they are NOT in GAACE_Core,
which stays board-agnostic and is pulled via `lib_deps`).

- **bus_queue/** — `DeferredQueue` + `I2CQueue` + `SPIQueue`. Interrupt-safe,
  non-RTOS bus transaction queue (PendSV-driven). See
  `../docs/port_notes/DeferredQueue_H7_PortNotes.md`.
- **pulse_timer/** — `STM32PulseTimer`. The MIPS pulse sequence generator on
  TIM2 (LL). See `../docs/port_notes/STM32PulseTimer_NOTES.md`.
- **transports/** — `GUartStream` / `GUsbCdcStream`. GStream (GAACE Stream/Print
  base) implementations over HAL UART and USB CDC. The project-side complement
  to GAACE_Core's `GArduinoStream`.

A project `GHal` implementation (for GAACE `debug`'s pin/analog commands and
`CPUTEMP`) will also live here once the board pin map is known.
