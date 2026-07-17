#pragma once
//
// GUsbCdcStream.h  -  GStream transport over the USB composite device's CDC
// (virtual COM) interface.
//
// This is the primary MIPS host-control transport (the virtual COM port half
// of the composite CDC+MSC device). available()/read() are served from an RX
// ring fed by the CDC receive callback; write() pushes through the CDC TX path.
//
// INTEGRATION:
//   1. A single global instance (there is one CDC interface):
//         GUsbCdcStream USBstream;
//   2. In the CubeMX-generated CDC_Receive_FS() callback, feed the ring:
//         USBstream.onRx(Buf, *Len);
//         // then re-arm reception as the ST template does
//   3. write() calls the project's CDC transmit helper (CDC_Transmit_FS),
//      retrying briefly while the endpoint is BUSY.
//
// The CDC transmit function is provided by the CubeMX USB middleware
// (usbd_cdc_if.c). It is declared here as extern "C" so this header does not
// need to pull in the USB device headers.

#include "GStream.h"
#include <stdint.h>

#ifndef GCDC_RX_RING
#define GCDC_RX_RING 512           // power of 2
#endif

// Provided by CubeMX-generated usbd_cdc_if.c
extern "C" uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);
// Return codes from that function (USBD_OK=0, USBD_BUSY=3) -- we only need OK.
#ifndef GCDC_USBD_OK
#define GCDC_USBD_OK 0
#endif

class GUsbCdcStream : public GStream {
public:
  GUsbCdcStream() = default;

  // Call from CDC_Receive_FS() with the just-received bytes.
  void onRx(const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
      uint16_t next = (uint16_t)((_head + 1) & (GCDC_RX_RING - 1));
      if (next == _tail) break;    // ring full: drop remainder
      _ring[_head] = buf[i];
      _head = next;
    }
  }

  int available() override {
    return (int)((_head - _tail) & (GCDC_RX_RING - 1));
  }

  int read() override {
    if (_head == _tail) return -1;
    uint8_t b = _ring[_tail];
    _tail = (uint16_t)((_tail + 1) & (GCDC_RX_RING - 1));
    return (int)b;
  }

  size_t write(uint8_t b) override {
    return write(&b, 1);
  }

  size_t write(const uint8_t *buf, size_t len) override {
    // CDC_Transmit_FS returns USBD_BUSY if the previous transfer is still in
    // flight. Retry a bounded number of times so a slow host doesn't wedge us
    // but a transiently-busy endpoint still gets the data out.
    const uint32_t kMaxSpins = 100000;
    uint32_t spins = 0;
    while (CDC_Transmit_FS(const_cast<uint8_t *>(buf), (uint16_t)len) != GCDC_USBD_OK) {
      if (++spins >= kMaxSpins) return 0;   // give up (host not draining)
    }
    return len;
  }

private:
  volatile uint8_t  _ring[GCDC_RX_RING];
  volatile uint16_t _head = 0, _tail = 0;
};
