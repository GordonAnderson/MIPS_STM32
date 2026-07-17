#pragma once
//
// SPIQueue.h  (STM32H743 / native STM32Cube HAL port)
//
// SPI-specific adapter for DeferredQueue.h. See that file for the underlying
// queue mechanics (fast path, PendSV draining, thread-mode self-drain).
//
// Ported from the Arduino SPI version to HAL. SPI transfers are full-duplex
// and in-place: SPITransaction::data holds what you're sending on enqueue and
// what was received once run() (or its callback) fires.
//
// DIFFERENCE FROM THE ARDUINO VERSION:
//   The Arduino original carried a per-transaction SPISettings (clock, mode,
//   bit order) so multiple devices could share one bus at different speeds by
//   passing different settings per call. In HAL, clock/mode/bit-order live in
//   the SPI_HandleTypeDef and changing them per-transfer means reconfiguring
//   the peripheral. To keep the queue fast and predictable, this port assumes
//   ONE HAL SPI handle per bus with a fixed configuration, and selects devices
//   by CS pin only. If you genuinely need per-device clock/mode on a shared
//   bus, either (a) use separate SPI_HandleTypeDef instances pointing at the
//   same SPIx with different Init, swapped in run(), or (b) reconfigure in a
//   dedicated transaction type. See the optional reconfig hook below.
//
// CS is driven with HAL_GPIO_WritePin (active-low assumed). Pass csPort=nullptr
// to disable automatic CS handling (manage it yourself).
//
// USAGE
//   extern SPI_HandleTypeDef hspi2;   // module-bus SPI, from CubeMX
//   SPIBus moduleSPI(&hspi2);
//   moduleSPI.queueTransfer(GPIOB, GPIO_PIN_12, buf, len, cb, ctx);

#include "DeferredQueue.h"
#include "stm32h7xx_hal.h"

#ifndef SPI_QUEUE_DEPTH
#define SPI_QUEUE_DEPTH 16        // must be a power of 2
#endif

#ifndef SPI_QUEUE_MAX_BYTES
#define SPI_QUEUE_MAX_BYTES 32    // largest single transfer held inline
#endif

#ifndef SPI_QUEUE_TIMEOUT_MS
#define SPI_QUEUE_TIMEOUT_MS 10UL
#endif

struct SPITransaction {
  SPI_HandleTypeDef *spi   = nullptr;
  GPIO_TypeDef      *csPort = nullptr;     // nullptr = no automatic CS
  uint16_t           csPin  = 0;
  uint8_t  data[SPI_QUEUE_MAX_BYTES] = {0};   // in-place: TX in, RX out
  uint8_t  len              = 0;
  void   (*callback)(bool ok, uint8_t *data, uint8_t len, void *ctx) = nullptr;
  void    *ctx              = nullptr;

  bool run() {
    // Full-duplex in-place: HAL needs separate TX/RX pointers, so use a small
    // stack copy of the outgoing bytes as the TX source while receiving back
    // into data[]. (SPI_QUEUE_MAX_BYTES is small, so the copy is cheap.)
    uint8_t tx[SPI_QUEUE_MAX_BYTES];
    memcpy(tx, data, len);

    if (csPort) HAL_GPIO_WritePin(csPort, csPin, GPIO_PIN_RESET);   // assert (active low)

    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(
        spi, tx, data, len, SPI_QUEUE_TIMEOUT_MS);

    if (csPort) HAL_GPIO_WritePin(csPort, csPin, GPIO_PIN_SET);     // deassert

    bool ok = (s == HAL_OK);
    // SPI shifts always "succeed" at the bus level absent a HAL error;
    // inspect data yourself for protocol-level errors.
    if (callback) callback(ok, data, len, ctx);
    return ok;
  }
};

class SPIBus {
public:
  explicit SPIBus(SPI_HandleTypeDef *spi) : _spi(spi) {}

  // csPort=nullptr disables automatic CS handling.
  bool queueTransfer(GPIO_TypeDef *csPort, uint16_t csPin,
                     const uint8_t *buf, uint8_t len,
                     void (*cb)(bool, uint8_t *, uint8_t, void *) = nullptr, void *ctx = nullptr) {
    if (len > SPI_QUEUE_MAX_BYTES) return false;
    SPITransaction t;
    t.spi = _spi; t.csPort = csPort; t.csPin = csPin; t.len = len;
    t.callback = cb; t.ctx = ctx;
    memcpy(t.data, buf, len);
    return _queue.enqueue(t);
  }

  void service() { _queue.service(); }
  bool full()  const { return _queue.full(); }
  bool empty() const { return _queue.empty(); }
  bool busy()  const { return _queue.busy(); }

private:
  SPI_HandleTypeDef *_spi;
  TransactionQueue<SPITransaction, SPI_QUEUE_DEPTH> _queue;
};
