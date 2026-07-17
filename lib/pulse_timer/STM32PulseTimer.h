/*
  STM32PulseTimer.h - High-precision pulse sequence generator for STM32H743
  Designed for the MIPS mass spectrometry pulse sequence engine.

  Ported from SAMD51Timer (Grand Central M4 / TCC peripheral) to the
  STM32H743 TIM2 general-purpose timer, using the STM32 LL (Low-Layer)
  driver set. NOT the Arduino framework.

  --------------------------------------------------------------------------
  ROLE IN MIPS
  --------------------------------------------------------------------------
  This is the single dedicated pulse-sequence generator. The sequence loop is:

      1. Firmware writes new DAC values (analog) / output word (digital)
         into the AD5668 (and/or digital latches) input registers.
      2. Timer fires a narrow LDAC pulse  -> all DAC outputs update
         simultaneously on the LDAC falling edge.
      3. Firmware prepares the next step (new DAC values, and often a new
         compare position and/or new period).
      4. Repeat.

  A single sequence step (one timer PERIOD) can be hundreds of milliseconds
  long, while the LDAC pulse itself is only a few microseconds wide. The
  32-bit TIM2 counter is essential to span that dynamic range at a fine
  prescale.

  --------------------------------------------------------------------------
  CRITICAL BEHAVIORAL DIFFERENCE vs. the SAMD51 original  (READ THIS)
  --------------------------------------------------------------------------
  The SAMD51 version used BUFFERED compare/period updates (CCBUF/PERBUF) that
  take effect at the NEXT overflow - i.e. changes applied to the *next* period.

  MIPS requires the OPPOSITE: the compare (pulse) position must be settable
  ON THE FLY, MID-PERIOD, and take effect IMMEDIATELY within the currently-
  running period. A period may be 100s of ms long, and the firmware moves the
  pulse position while that period is still counting.

  Therefore compare preload is DISABLED (OCxPE = 0): writes to CCR go live
  immediately, not shadowed to the next update event. This is the single most
  important design decision in this driver.

  Consequence - the "passed compare" hazard: if firmware moves the compare
  point to a value the counter has ALREADY passed in the current period, a
  normal compare match will not occur again until the next period, producing a
  MISSED pulse. This driver detects that case and handles it explicitly - see
  setComparePosition() and the CompareHazardPolicy enum below.

  --------------------------------------------------------------------------
  TIMER ALLOCATION (MIPS-wide)
  --------------------------------------------------------------------------
  This class claims TIM2 ONLY. MIPS uses ~8 timers total (delay generators,
  clock generation, etc.); as those are ported, keep a running map of which
  STM32 TIM instance backs which MIPS function so they do not collide. TIM5
  is the identical 32-bit drop-in alternative to TIM2 (same feature set,
  including an ETR pin) if TIM2 is needed elsewhere - see USE_TIM5 below.
*/

#ifndef STM32_PULSE_TIMER_H
#define STM32_PULSE_TIMER_H

#include "stm32h7xx_ll_tim.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_rcc.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Timer instance selection - TIM2 by default, TIM5 as identical alternative.
// Both are 32-bit general-purpose timers on APB1 with an ETR input.
// ---------------------------------------------------------------------------
// #define STM32_PULSE_TIMER_USE_TIM5    // uncomment to retarget to TIM5

#ifdef STM32_PULSE_TIMER_USE_TIM5
  #define PT_TIM              TIM5
  #define PT_ENABLE_CLOCK()   LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5)
  #define PT_IRQn             TIM5_IRQn
  #define PT_IRQHandler       TIM5_IRQHandler
#else
  #define PT_TIM              TIM2
  #define PT_ENABLE_CLOCK()   LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2)
  #define PT_IRQn             TIM2_IRQn
  #define PT_IRQHandler       TIM2_IRQHandler
#endif


class STM32PulseTimer {
public:

    // -----------------------------------------------------------------------
    // Enumerations
    // -----------------------------------------------------------------------

    enum TriggerEdge {
        TRIGGER_RISING,
        TRIGGER_FALLING
        // Note: hardware ETR slave-mode reset is edge-selectable rising/falling
        // via ETR polarity. "BOTH" is intentionally omitted - the TIM slave-mode
        // controller resets on a single configured polarity, not both edges.
        // (The SAMD51 software-interrupt version could do CHANGE; the hardware
        //  trigger cannot, and hardware is the correct choice here.)
    };

    enum CycleMode {
        MODE_CONTINUOUS,   // free-running: reload and continue at each update
        MODE_ONE_SHOT      // stop automatically after a single period
    };

    // What to do if a new compare position is written that the counter has
    // already passed within the current period (see file header).
    enum CompareHazardPolicy {
        HAZARD_FIRE_NOW,   // immediately generate the LDAC pulse in software-
                           //   equivalent fashion (recommended for the pulse
                           //   engine - guarantees the step's pulse is not lost)
        HAZARD_SKIP,       // let it fall through; pulse will occur next period
                           //   (or not at all this period) - caller accepts it
        HAZARD_WRAP        // treat the request as "next period": leave the
                           //   compare set; it will match after the wrap
    };

    // Result reported back from setComparePosition() so the sequence engine
    // knows what actually happened.
    enum CompareResult {
        COMPARE_SCHEDULED, // compare is ahead of current count - will match normally
        COMPARE_HAZARD_HANDLED, // compare was already passed; handled per policy
    };

    // -----------------------------------------------------------------------
    // Construction / initialization
    // -----------------------------------------------------------------------

    STM32PulseTimer();

    // Initialize TIM2 hardware.
    //
    // timerKernelClockHz : the actual TIM2 kernel clock in Hz. On STM32H743
    //   this is derived from the APB1 timer clock (often 200-240 MHz depending
    //   on the CubeMX clock tree) - pass the real configured value; the class
    //   does not assume it. This is what all frequency/period math uses.
    //
    // Compare preload is DISABLED here (immediate mid-period updates). ARR
    // (period) preload handling is configurable via setPeriodPreload().
    STM32PulseTimer& begin(uint32_t timerKernelClockHz);

    // Route the LDAC output to a hardware pin driven by the timer output
    // compare channel. gpioPort/pin/alternate identify the package pin and its
    // TIM2_CHx alternate function (from the datasheet AF table / CubeMX).
    // Which channel this pin maps to must match ldacChannel() below.
    STM32PulseTimer& configureLDACOutputPin(GPIO_TypeDef* port,
                                            uint32_t pin,
                                            uint32_t alternate);

    // -----------------------------------------------------------------------
    // Clock / prescaler control  (user-programmable, per application)
    // -----------------------------------------------------------------------
    //
    // STM32 prescaler is a full 16-bit integer divider (1..65536), far finer
    // than the SAMD51's 8 fixed power-of-two steps. Both a direct prescaler
    // set and auto-calculating helpers are provided.

    // Set the raw prescaler divider directly. divider is 1..65536.
    // Tick rate becomes timerKernelClockHz / divider.
    STM32PulseTimer& setPrescaler(uint32_t divider);

    // Auto-select a prescaler and period (ARR) to produce the requested
    // output frequency, maximizing counter resolution (smallest prescaler
    // that keeps the period within the 32-bit ARR range).
    STM32PulseTimer& setFrequency(double hz);

    // Same, expressed as a period in microseconds.
    STM32PulseTimer& setPeriod(double microseconds);

    // Set the period (ARR) directly, in timer ticks. Range 1..0xFFFFFFFF.
    STM32PulseTimer& setPeriodTicks(uint32_t ticks);

    // -----------------------------------------------------------------------
    // Pulse (LDAC) position - the on-the-fly, mid-period-immediate control
    // -----------------------------------------------------------------------
    //
    // Sets the counter value at which the LDAC pulse is generated this period.
    // Takes effect IMMEDIATELY in the current period (compare preload off).
    //
    // Handles the passed-compare hazard per the given policy and reports what
    // happened via the CompareResult out-parameter (may be nullptr).
    STM32PulseTimer& setComparePosition(uint32_t ticks,
                                        CompareHazardPolicy policy = HAZARD_FIRE_NOW,
                                        CompareResult* result = nullptr);

    // Convenience: set the LDAC pulse position as a fraction (0..1) of the
    // current period. Same immediate/hazard semantics as above.
    STM32PulseTimer& setComparePositionFraction(double frac,
                                                CompareHazardPolicy policy = HAZARD_FIRE_NOW,
                                                CompareResult* result = nullptr);

    // Set the LDAC pulse WIDTH in nanoseconds (default ~1 us). The AD5668
    // latches on the LDAC falling edge; min low pulse width is ~20 ns, so the
    // default is comfortably safe. Width is realized via a second compare
    // channel (rising/falling edge pair) - see .cpp.
    STM32PulseTimer& setLDACPulseWidthNs(uint32_t widthNs);

    // -----------------------------------------------------------------------
    // Cycle / execution control
    // -----------------------------------------------------------------------

    STM32PulseTimer& setCycleMode(CycleMode mode);
    STM32PulseTimer& setMultiShot(uint32_t count);   // run exactly N periods then stop

    void start();                                    // reset counter to 0 and run
    void stop();                                     // halt the counter
    void softwareTriggerLDAC();                      // force one LDAC pulse now

    // -----------------------------------------------------------------------
    // External trigger (hardware ETR slave mode - true zero-latency)
    // -----------------------------------------------------------------------
    //
    // Configures the TIM2 ETR pin so an external edge resets the counter to 0
    // and (re)starts the period entirely in hardware - no ISR, no software
    // latency. This replaces the SAMD51 version's attachInterrupt() software
    // trigger and is the correct realization of the MIPS Q/R trigger inputs.
    //
    // etrPort/pin/alternate identify the ETR package pin and its AF.
    STM32PulseTimer& setExternalTrigger(GPIO_TypeDef* etrPort,
                                        uint32_t pin,
                                        uint32_t alternate,
                                        TriggerEdge edge = TRIGGER_RISING);

    STM32PulseTimer& clearExternalTrigger();

    // -----------------------------------------------------------------------
    // Timing helpers
    // -----------------------------------------------------------------------

    double   getTickDurationNs();      // ns per tick at current prescaler
    uint32_t getCounter();             // live 32-bit counter value
    uint32_t getPeriodTicks();         // current ARR
    uint32_t getCurrentDivisor() { return _currentDivisor; }

    // -----------------------------------------------------------------------
    // Interrupt callbacks  (kept from the SAMD51 API - all three retained)
    // -----------------------------------------------------------------------
    //
    // onStart : fires at the start-of-period compare (CH-start)
    // onMid   : fires at the LDAC compare match (the pulse instant)
    // onTerm  : fires at the period rollover (update event) - typically where
    //           the sequence engine loads the next step's DAC values + period
    //
    // Pass nullptr for any event not needed.
    STM32PulseTimer& attachInterrupts(void (*onStart)(),
                                      void (*onMid)(),
                                      void (*onTerm)());

    // Called from the TIMx_IRQHandler ISR (see .cpp). Public so the extern "C"
    // vector can reach it; not intended for application use.
    void handleISR();

private:

    uint32_t          _timerKernelClockHz;
    uint32_t          _currentDivisor;      // active prescaler divider (1..65536)
    uint32_t          _ldacPulseWidthTicks; // LDAC pulse width in ticks
    volatile uint32_t _remainingCycles;     // multishot countdown (0 = infinite)
    CycleMode         _mode;
    bool              _extTriggerActive;

    void (*_callbackStart)();
    void (*_callbackMid)();
    void (*_callbackTerm)();

    // Compare channel assignment on PT_TIM:
    //   CH1 = LDAC pulse leading edge  (the "mid" pulse instant)
    //   CH2 = LDAC pulse trailing edge (defines pulse width)
    //   CH3 = start-of-period marker   (the "start" callback)
    // Update event (UIF) = terminal / period rollover ("term" callback).
    static inline uint32_t ldacLeadChannel();
    static inline uint32_t ldacTrailChannel();
    static inline uint32_t startChannel();

    void applyCompareImmediate(uint32_t leadTicks);
    void programLDACWidth(uint32_t leadTicks);

    static STM32PulseTimer* _instance;   // single dedicated instance (one PSG)

public:
    static STM32PulseTimer* instance() { return _instance; }
};

// Single global instance - there is exactly one pulse sequence generator.
extern STM32PulseTimer PulseTimer;

#endif // STM32_PULSE_TIMER_H
