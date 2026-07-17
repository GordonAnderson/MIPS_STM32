#pragma once
//
// GUartStream.h  -  GStream transport over a HAL UART (interrupt RX).
//
// Provides non-blocking available()/read() via a small RX ring filled from the
// UART RX interrupt, and blocking-ish write() via HAL_UART_Transmit. This is
// the console/command transport for GAACE_Core's commandProcessor on a UART.
//
// INTEGRATION (per instance):
//   1. Construct with the CubeMX UART handle:  GUartStream dbg(&huart3);
//   2. Start reception once at init:            dbg.begin();
//   3. In HAL_UART_RxCpltCallback(), route the byte:
//         if (huart->Instance == USART3) dbg.onRxByte();
//      (or call dbg.onRxByte() from your own RX ISR path).
//
// The class re-arms single-byte interrupt reception after each byte, which is
// simple and robust for a command console (low data rate). For high-rate use,
// swap to circular DMA RX and feed the ring from the DMA idle/half/complete
// callbacks -- the ring API below stays the same.

#include "GStream.h"
#include "stm32h7xx_hal.h"

#ifndef GUART_RX_RING
#define GUART_RX_RING 256          // power of 2
#endif

class GUartStream : public GStream {
public:
  explicit GUartStream(UART_HandleTypeDef *huart) : _huart(huart) {}

  // Arm interrupt reception of the first byte.
  void begin() {
    HAL_UART_Receive_IT(_huart, &_rxByte, 1);
  }

  // Call from HAL_UART_RxCpltCallback for this UART. Pushes the received byte
  // into the ring and re-arms reception.
  void onRxByte() {
    uint16_t next = (uint16_t)((_head + 1) & (GUART_RX_RING - 1));
    if (next != _tail) {           // drop on overflow (console; acceptable)
      _ring[_head] = _rxByte;
      _head = next;
    }
    HAL_UART_Receive_IT(_huart, &_rxByte, 1);
  }

  int available() override {
    return (int)((_head - _tail) & (GUART_RX_RING - 1));
  }

  int read() override {
    if (_head == _tail) return -1;
    uint8_t b = _ring[_tail];
    _tail = (uint16_t)((_tail + 1) & (GUART_RX_RING - 1));
    return (int)b;
  }

  size_t write(uint8_t b) override {
    HAL_UART_Transmit(_huart, &b, 1, HAL_MAX_DELAY);
    return 1;
  }

  size_t write(const uint8_t *buf, size_t len) override {
    HAL_UART_Transmit(_huart, const_cast<uint8_t *>(buf), (uint16_t)len, HAL_MAX_DELAY);
    return len;
  }

private:
  UART_HandleTypeDef *_huart;
  uint8_t  _rxByte = 0;
  volatile uint8_t  _ring[GUART_RX_RING];
  volatile uint16_t _head = 0, _tail = 0;
};
