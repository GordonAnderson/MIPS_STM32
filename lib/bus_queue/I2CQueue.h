#pragma once
//
// I2CQueue.h  (STM32H743 / native STM32Cube HAL port)
//
// I2C-specific adapter for DeferredQueue.h. See that file for the underlying
// queue mechanics (fast path, PendSV draining, thread-mode self-drain).
//
// Ported from the Arduino Wire version to HAL. The transaction still owns its
// own inline data buffer (I2C_QUEUE_MAX_BYTES) -- important for ISR safety,
// since a pointer to an ISR's local stack buffer would be dangling by the
// time the queue drains it.
//
// USAGE
//   extern I2C_HandleTypeDef hi2c1;   // from CubeMX-generated code
//   I2CBus moduleBus(&hi2c1);
//   moduleBus.queueWriteRead(addr, &reg, 1, 4, myCallback, ctx);
//
// One I2CBus per HAL I2C handle (hi2c1, hi2c2, ...); each registers itself
// and is drained by the same shared PendSV_Handler.
//
// TIMEOUT: uses a DWT cycle-counter microsecond clock (see dwt_micros())
// instead of Arduino micros(). Call DWT_InitMicros() once at startup.

#include "DeferredQueue.h"
#include "stm32h7xx_hal.h"

#ifndef I2C_QUEUE_DEPTH
#define I2C_QUEUE_DEPTH 16        // must be a power of 2
#endif

#ifndef I2C_QUEUE_MAX_BYTES
#define I2C_QUEUE_MAX_BYTES 16    // largest single transfer held inline
#endif

#ifndef I2C_QUEUE_TIMEOUT_MS
#define I2C_QUEUE_TIMEOUT_MS 5UL  // per-transfer HAL timeout (was 5000us)
#endif

// ---------------------------------------------------------------------------
// One-time DWT microsecond clock init (shared with SPIQueue if desired).
// Enables the Cortex-M7 cycle counter. Call once after clocks are configured.
// Kept inline+guarded so including both queues in different TUs is safe.
// ---------------------------------------------------------------------------
static inline void DWT_InitMicros(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;   // unlock (harmless on cores that ignore it)
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_micros(void) {
  // SystemCoreClock is the CPU frequency (e.g. 480000000 on H7).
  return DWT->CYCCNT / (SystemCoreClock / 1000000u);
}

enum class I2COp : uint8_t { Write, Read, WriteRead };

struct I2CTransaction {
  I2C_HandleTypeDef *bus   = nullptr;
  uint16_t addr7           = 0;                    // 7-bit address (unshifted)
  I2COp    op              = I2COp::Write;
  uint8_t  data[I2C_QUEUE_MAX_BYTES] = {0};        // TX in / RX out
  uint8_t  txLen           = 0;
  uint8_t  rxLen           = 0;                    // requested in, actual out
  void   (*callback)(bool ok, uint8_t *data, uint8_t len, void *ctx) = nullptr;
  void    *ctx             = nullptr;

  bool run() {
    bool ok = true;
    // HAL uses 8-bit addressing (7-bit addr << 1).
    uint16_t halAddr = (uint16_t)(addr7 << 1);

    if (op == I2COp::Write) {
      HAL_StatusTypeDef s = HAL_I2C_Master_Transmit(
          bus, halAddr, data, txLen, I2C_QUEUE_TIMEOUT_MS);
      if (s != HAL_OK) ok = false;
    }
    else if (op == I2COp::Read) {
      HAL_StatusTypeDef s = HAL_I2C_Master_Receive(
          bus, halAddr, data, rxLen, I2C_QUEUE_TIMEOUT_MS);
      if (s != HAL_OK) { ok = false; rxLen = 0; }
    }
    else { // WriteRead: write txLen "register/command" bytes, repeated-start,
           // then read rxLen bytes. The blocking HAL API that performs a true
           // repeated start is HAL_I2C_Mem_Read/Write (the Seq_* APIs are
           // interrupt/DMA-only and would not block here). We map the write
           // bytes to the HAL "memory address" field: 1 byte -> 8-bit addr
           // size, 2 bytes -> 16-bit addr size. (This covers the common
           // register-read pattern used by the module EEPROMs and sensors;
           // for >2 command bytes see the note below.)
      if (txLen == 1 || txLen == 2) {
        uint16_t memAddr    = (txLen == 1) ? data[0]
                                           : (uint16_t)((data[0] << 8) | data[1]);
        uint16_t memAddrSz  = (txLen == 1) ? I2C_MEMADD_SIZE_8BIT
                                           : I2C_MEMADD_SIZE_16BIT;
        HAL_StatusTypeDef s = HAL_I2C_Mem_Read(
            bus, halAddr, memAddr, memAddrSz, data, rxLen, I2C_QUEUE_TIMEOUT_MS);
        if (s != HAL_OK) { ok = false; rxLen = 0; }
      } else {
        // >2 command bytes with a true repeated start is not expressible via
        // the blocking Mem_ API. Fall back to write-then-read WITH a stop in
        // between (works for any device that tolerates a stop; the vast
        // majority do). If a device strictly requires repeated-start with a
        // multi-byte command, promote that specific transaction to the
        // interrupt-driven Seq_* path in a dedicated handler.
        HAL_StatusTypeDef t1 = HAL_I2C_Master_Transmit(
            bus, halAddr, data, txLen, I2C_QUEUE_TIMEOUT_MS);
        if (t1 != HAL_OK) ok = false;
        if (ok) {
          HAL_StatusTypeDef t2 = HAL_I2C_Master_Receive(
              bus, halAddr, data, rxLen, I2C_QUEUE_TIMEOUT_MS);
          if (t2 != HAL_OK) { ok = false; rxLen = 0; }
        }
      }
    }

    if (callback) callback(ok, data, rxLen, ctx);
    return ok;
  }
};

class I2CBus {
public:
  explicit I2CBus(I2C_HandleTypeDef *bus) : _bus(bus) {}

  bool queueWrite(uint16_t addr7, const uint8_t *buf, uint8_t len,
                  void (*cb)(bool, uint8_t *, uint8_t, void *) = nullptr, void *ctx = nullptr) {
    if (len > I2C_QUEUE_MAX_BYTES) return false;
    I2CTransaction t;
    t.bus = _bus; t.addr7 = addr7; t.op = I2COp::Write; t.txLen = len;
    t.callback = cb; t.ctx = ctx;
    memcpy(t.data, buf, len);
    return _queue.enqueue(t);
  }

  bool queueRead(uint16_t addr7, uint8_t len,
                 void (*cb)(bool, uint8_t *, uint8_t, void *) = nullptr, void *ctx = nullptr) {
    if (len > I2C_QUEUE_MAX_BYTES) return false;
    I2CTransaction t;
    t.bus = _bus; t.addr7 = addr7; t.op = I2COp::Read; t.rxLen = len;
    t.callback = cb; t.ctx = ctx;
    return _queue.enqueue(t);
  }

  // Write reg/cmd bytes, repeated-start, then read rlen bytes -- common
  // "register read" pattern for sensors and the module EEPROMs.
  bool queueWriteRead(uint16_t addr7, const uint8_t *wbuf, uint8_t wlen, uint8_t rlen,
                      void (*cb)(bool, uint8_t *, uint8_t, void *) = nullptr, void *ctx = nullptr) {
    if (wlen > I2C_QUEUE_MAX_BYTES || rlen > I2C_QUEUE_MAX_BYTES) return false;
    I2CTransaction t;
    t.bus = _bus; t.addr7 = addr7; t.op = I2COp::WriteRead; t.txLen = wlen; t.rxLen = rlen;
    t.callback = cb; t.ctx = ctx;
    memcpy(t.data, wbuf, wlen);
    return _queue.enqueue(t);
  }

  void service() { _queue.service(); }
  bool full()  const { return _queue.full(); }
  bool empty() const { return _queue.empty(); }
  bool busy()  const { return _queue.busy(); }

private:
  I2C_HandleTypeDef *_bus;
  TransactionQueue<I2CTransaction, I2C_QUEUE_DEPTH> _queue;
};
