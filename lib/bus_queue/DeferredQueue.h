#pragma once
//
// DeferredQueue.h  (STM32H743 / native STM32Cube HAL+LL port)
//
// Generic, interrupt-safe, NON-RTOS deferred transaction queue for Cortex-M.
// Ported from the Arduino-framework SAMD version to native STM32Cube. The
// queue MECHANICS are unchanged; only the environment couplings were removed:
//
//   * No <Arduino.h>. Depends only on the CMSIS device header (which the
//     STM32Cube HAL pulls in via stm32h7xx.h) for __disable_irq/__enable_irq,
//     __get_IPSR, __DMB, SCB, NVIC_*, PendSV_IRQn, __NVIC_PRIO_BITS.
//   * noInterrupts()/interrupts() (Arduino macros) replaced with a small
//     RAII/inline critical-section using PRIMASK save/restore, which is
//     safer than the plain enable/disable pair because it does not
//     unconditionally re-enable interrupts if a caller nested a critical
//     section (matters once HAL ISRs are in the mix).
//
// This has no knowledge of I2C, SPI, or any peripheral. It manages a ring
// buffer of user-defined transaction objects of type T, where T must provide:
//
//    bool run();   // performs the actual (blocking) work; return value is
//                  // ignored by the queue itself -- fire any completion
//                  // callback from inside run() if you need one.
//
// T must also be default-constructible and copy-assignable.
//
// Behavior (identical to the original, regardless of what T does):
//  - enqueue() may be called from ISR or thread (main-loop) context.
//      Fast path: if idle and nothing is already waiting, run() executes
//      immediately in the caller's context before enqueue() returns.
//      In thread mode (checked via __get_IPSR() == 0) enqueue() also drains
//      anything that arrived while that first transaction was running.
//      Slow path: if busy, or something is already queued ahead (preserves
//      FIFO order), the transaction is copied into the ring under a short
//      critical section and PendSV is pended.
//  - PendSV drains buffered work right after real interrupts finish, just
//    before returning to thread mode.
//  - service() (call from the main loop) is a backstop if PendSV isn't wired
//    up or interrupts were masked long enough elsewhere to delay it.
//
// REQUIRED ONE-TIME SETUP (in exactly one .cpp, not a header):
//
//   // In your system init, AFTER HAL_Init() / clock config:
//   NVIC_SetPriority(PendSV_IRQn, (1u << __NVIC_PRIO_BITS) - 1u); // lowest
//
//   extern "C" void PendSV_Handler(void) {
//     DeferredQueueBase::serviceFromPendSV();
//   }
//
// IMPORTANT (STM32Cube): the CubeMX-generated stm32h7xx_it.c ALSO defines a
// (non-weak) PendSV_Handler when certain middleware is enabled. Because this
// project runs NO preemptive RTOS, PendSV is free -- but you must ensure only
// ONE PendSV_Handler exists. Either delete/rename the generated one, or place
// the DeferredQueueBase::serviceFromPendSV() call inside it. If a preemptive
// RTOS is ever added, it will claim PendSV and this mechanism must move to a
// dedicated software-triggered IRQ instead (see project firmware plan).
//
// D-CACHE NOTE (H7): the ring buffer and transaction copies live in normal
// SRAM and are only ever touched by the CPU (never DMA), so no cache
// maintenance is needed here. If a transaction's run() uses DMA to/from
// cached SRAM, cache clean/invalidate belongs in THAT transfer code, not in
// this queue.

#include "stm32h7xx.h"   // CMSIS core + device (brings in the intrinsics below)
#include <stdint.h>
#include <string.h>

#ifndef DEFERRED_QUEUE_MAX_INSTANCES
#define DEFERRED_QUEUE_MAX_INSTANCES 8   // total across ALL transaction types
#endif

// ---------------------------------------------------------------------------
// Small, nesting-safe critical section (PRIMASK save/restore).
//
// Unlike the Arduino noInterrupts()/interrupts() pair (which unconditionally
// re-enables), this restores the PREVIOUS PRIMASK, so a critical section
// nested inside an already-masked region does not prematurely re-enable
// interrupts. Cheap: two register ops on entry/exit.
// ---------------------------------------------------------------------------
class DQCritical {
public:
  inline DQCritical()  { _primask = __get_PRIMASK(); __disable_irq(); }
  inline ~DQCritical() { if (!_primask) __enable_irq(); }
private:
  uint32_t _primask;
};

// Non-template base: shared registry so a single PendSV_Handler can
// polymorphically drain queues of different transaction types.
class DeferredQueueBase {
public:
  static void serviceFromPendSV() {
    uint8_t cnt = registryCount();
    DeferredQueueBase **slots = registrySlots();
    for (uint8_t i = 0; i < cnt; i++) {
      if (slots[i]) slots[i]->drainAll();
    }
  }

  virtual ~DeferredQueueBase() = default;

protected:
  DeferredQueueBase() {
    uint8_t &cnt = registryCount();
    if (cnt < DEFERRED_QUEUE_MAX_INSTANCES) registrySlots()[cnt++] = this;
  }

  // Drains until empty or until the resource is found already claimed by
  // another context. Safe from thread mode or PendSV; must NOT be called
  // from a real (non-PendSV) ISR -- unbounded blocking risk.
  virtual void drainAll() = 0;

private:
  // Meyer's-singleton statics: one shared instance across all translation
  // units, no separate .cpp needed for static member definitions.
  static DeferredQueueBase **registrySlots() {
    static DeferredQueueBase *slots[DEFERRED_QUEUE_MAX_INSTANCES] = {nullptr};
    return slots;
  }
  static uint8_t &registryCount() {
    static uint8_t count = 0;
    return count;
  }
};

template <typename T, uint8_t Depth = 16>
class TransactionQueue : public DeferredQueueBase {
  static_assert(Depth != 0 && (Depth & (Depth - 1)) == 0, "Depth must be a power of 2");

public:
  // Safe from ISR or thread context. Returns false if the queue is full --
  // caller decides how to handle backpressure (drop, retry, assert, etc).
  //
  // CAVEAT: if called from an ISR and the fast path is taken, T::run() --
  // including any completion callback it invokes -- executes inside that ISR.
  // Keep it short and non-blocking in that case, same as any ISR code.
  bool enqueue(const T &t) {
    bool runNow = false;
    {
      DQCritical cs;
      if (!_busy && _head == _tail) {
        _busy = true;                  // claim atomically w.r.t. other producers
        runNow = true;
      }
    }

    if (runNow) {
      T local = t;                     // private copy, safe regardless of caller storage
      local.run();
      __DMB();                         // ensure run()'s stores complete before releasing
      _busy = false;                   // release before draining so drainAll() can claim

      if (__get_IPSR() == 0) {         // thread mode -- safe to keep draining here
        drainAll();
      }
      return true;
    }

    // Busy, or something already queued ahead -- buffer it so PendSV (or
    // service(), or the next thread-mode enqueue()) drains it later, FIFO.
    bool ok = true;
    {
      DQCritical cs;
      uint8_t nextTail = (_tail + 1) & (Depth - 1);
      if (nextTail == _head) {
        ok = false;                    // full
      } else {
        _queue[_tail] = t;
        _tail = nextTail;
      }
    }

    if (ok) {
      SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;  // drain once nothing higher is pending
    }
    return ok;
  }

  // Call every main-loop iteration. Executes at most one queued transaction.
  // Backstop for when PendSV isn't wired up.
  void service() {
    if (_busy) return;
    if (_head == _tail) return;

    T t;
    {
      DQCritical cs;
      t = _queue[_head];
      _head = (_head + 1) & (Depth - 1);
    }

    _busy = true;
    t.run();
    __DMB();
    _busy = false;
  }

  bool full()  const { return ((_tail + 1) & (Depth - 1)) == _head; }
  bool empty() const { return _head == _tail; }
  bool busy()  const { return _busy; }

private:
  T _queue[Depth];
  volatile uint8_t _head = 0, _tail = 0;
  volatile bool _busy = false;

  void drainAll() override {
    while (true) {
      T t;
      bool tookOne = false;
      {
        DQCritical cs;
        if (!_busy && _head != _tail) {
          _busy = true;
          t = _queue[_head];
          _head = (_head + 1) & (Depth - 1);
          tookOne = true;
        }
      }

      if (!tookOne) return;    // empty, or someone else already holds the resource

      t.run();
      __DMB();
      _busy = false;
    }
  }
};
