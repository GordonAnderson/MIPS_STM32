/*
  STM32PulseTimer.cpp - implementation

  See STM32PulseTimer.h for the full behavioral contract. The two things that
  matter most and differ from the SAMD51 original:

    1. Compare preload is DISABLED  -> LDAC pulse position updates take effect
       immediately, mid-period, in the currently-running period.

    2. The "passed compare" hazard (new compare < current count) is detected
       and handled explicitly per CompareHazardPolicy.
*/

#include "STM32PulseTimer.h"

// The single dedicated instance.
STM32PulseTimer  PulseTimer;
STM32PulseTimer* STM32PulseTimer::_instance = nullptr;

// ---------------------------------------------------------------------------
// Channel mapping helpers  (LL channel constants)
//   CH1 = LDAC leading edge (pulse instant / "mid")
//   CH2 = LDAC trailing edge (pulse width)
//   CH3 = start-of-period marker ("start")
// ---------------------------------------------------------------------------
inline uint32_t STM32PulseTimer::ldacLeadChannel()  { return LL_TIM_CHANNEL_CH1; }
inline uint32_t STM32PulseTimer::ldacTrailChannel() { return LL_TIM_CHANNEL_CH2; }
inline uint32_t STM32PulseTimer::startChannel()     { return LL_TIM_CHANNEL_CH3; }

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
STM32PulseTimer::STM32PulseTimer() :
    _timerKernelClockHz(200000000),   // placeholder; real value set in begin()
    _currentDivisor(1),
    _ldacPulseWidthTicks(0),
    _remainingCycles(0),
    _mode(MODE_CONTINUOUS),
    _extTriggerActive(false),
    _callbackStart(nullptr),
    _callbackMid(nullptr),
    _callbackTerm(nullptr)
{
    _instance = this;
}

// ---------------------------------------------------------------------------
// begin - initialize TIM2 for immediate-compare pulse generation
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::begin(uint32_t timerKernelClockHz) {

    _timerKernelClockHz = timerKernelClockHz;
    _instance = this;

    // 1. Enable the timer peripheral clock.
    PT_ENABLE_CLOCK();

    // 2. Base configuration: upcounting, no auto-reload preload initially.
    LL_TIM_InitTypeDef init;
    LL_TIM_StructInit(&init);
    init.Prescaler         = 0;                       // divider-1 encoding = /1
    init.CounterMode       = LL_TIM_COUNTERMODE_UP;
    init.Autoreload        = 0xFFFFFFFF;              // 32-bit full range default
    init.ClockDivision     = LL_TIM_CLOCKDIVISION_DIV1;
    init.RepetitionCounter = 0;
    LL_TIM_Init(PT_TIM, &init);

    _currentDivisor = 1;

    // 3. --- CRITICAL --- Disable output-compare preload on the LDAC channels
    //    so CCR writes take effect IMMEDIATELY (mid-period), not shadowed to
    //    the next update event. This is the core requirement of the MIPS
    //    pulse-sequence engine and the key departure from the SAMD51 version.
    LL_TIM_OC_DisablePreload(PT_TIM, ldacLeadChannel());
    LL_TIM_OC_DisablePreload(PT_TIM, ldacTrailChannel());
    LL_TIM_OC_DisablePreload(PT_TIM, startChannel());

    // 4. Auto-reload (period/ARR) preload: also disabled by default so a new
    //    period likewise applies without waiting a full cycle. (If a specific
    //    application wants period changes to apply only at the clean rollover,
    //    it can be toggled - but default matches the "immediate" philosophy.)
    LL_TIM_DisableARRPreload(PT_TIM);

    // 5. Configure CH1 (LDAC lead) and CH2 (LDAC trail) as output compare.
    //    We generate the narrow LDAC pulse as a lead/trail edge pair so its
    //    width is defined precisely regardless of period length.
    //    Output polarity is set so the pulse is an active-low strobe for the
    //    AD5668 (idle high, pulse low), latching on the falling (lead) edge.
    LL_TIM_OC_SetMode(PT_TIM, ldacLeadChannel(),  LL_TIM_OCMODE_INACTIVE);
    LL_TIM_OC_SetMode(PT_TIM, ldacTrailChannel(), LL_TIM_OCMODE_ACTIVE);
    // CH1 forces the pin INACTIVE (low = pulse asserted) at the lead compare,
    // CH2 forces it ACTIVE (high = idle) at the trail compare. Net: a low-going
    // pulse of (trail-lead) ticks. Polarity chosen for AD5668 active-low LDAC.
    LL_TIM_OC_SetPolarity(PT_TIM, ldacLeadChannel(),  LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_SetPolarity(PT_TIM, ldacTrailChannel(), LL_TIM_OCPOLARITY_HIGH);

    // 6. CH3 = start-of-period marker (interrupt only, no pin needed).
    LL_TIM_OC_SetMode(PT_TIM, startChannel(), LL_TIM_OCMODE_FROZEN);
    LL_TIM_OC_SetCompareCH3(PT_TIM, 0);   // start compare at count 0

    // 7. Default LDAC pulse width ~1 us (recomputed against the real clock).
    setLDACPulseWidthNs(1000);

    // 8. Enable the LDAC output channels (CH1/CH2 drive the pin together).
    LL_TIM_CC_EnableChannel(PT_TIM, ldacLeadChannel());
    LL_TIM_CC_EnableChannel(PT_TIM, ldacTrailChannel());

    // 9. Enable the counter. (Outputs on general-purpose TIM2/5 are live once
    //    CC channels are enabled; no BDTR main-output-enable as on advanced
    //    timers TIM1/TIM8.)
    LL_TIM_EnableCounter(PT_TIM);

    return *this;
}

// ---------------------------------------------------------------------------
// configureLDACOutputPin - route CH1/CH2 to a package pin
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::configureLDACOutputPin(GPIO_TypeDef* port,
                                                         uint32_t pin,
                                                         uint32_t alternate) {
    // Enable the GPIO port clock is assumed done by board init; configure the
    // pin as alternate-function push-pull at high speed for a clean edge.
    LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_UP);   // idle-high for active-low LDAC

    // Apply the alternate function (low/high AFR register chosen by pin index).
    if (POSITION_VAL(pin) < 8)
        LL_GPIO_SetAFPin_0_7(port, pin, alternate);
    else
        LL_GPIO_SetAFPin_8_15(port, pin, alternate);

    return *this;
}

// ---------------------------------------------------------------------------
// Prescaler / frequency / period
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::setPrescaler(uint32_t divider) {
    if (divider < 1)     divider = 1;
    if (divider > 65536) divider = 65536;
    _currentDivisor = divider;
    // PSC register holds (divider - 1).
    LL_TIM_SetPrescaler(PT_TIM, divider - 1);
    // Force the new prescaler to load via an update generation. Because ARR
    // preload is off, this does not disturb the running compare values beyond
    // reloading PSC; the counter continues.
    LL_TIM_GenerateEvent_UPDATE(PT_TIM);
    return *this;
}

STM32PulseTimer& STM32PulseTimer::setPeriodTicks(uint32_t ticks) {
    if (ticks < 1) ticks = 1;
    LL_TIM_SetAutoReload(PT_TIM, ticks);
    return *this;
}

STM32PulseTimer& STM32PulseTimer::setFrequency(double hz) {
    // Choose the smallest prescaler that keeps the period within 32-bit ARR,
    // maximizing tick resolution. 32-bit ARR max = 0xFFFFFFFF.
    const double kMaxArr = 4294967295.0;
    uint32_t divider = 1;
    double periodTicks = 0.0;

    for (uint32_t d = 1; d <= 65536; d++) {
        periodTicks = (double)_timerKernelClockHz / hz / (double)d;
        if (periodTicks <= kMaxArr) {
            divider = d;
            break;
        }
    }

    setPrescaler(divider);
    setPeriodTicks((uint32_t)(periodTicks + 0.5));

    // Default the LDAC pulse to the mid-point unless the sequence engine
    // overrides it (mirrors the SAMD51 default of a mid toggle).
    setComparePositionFraction(0.5, HAZARD_WRAP, nullptr);
    return *this;
}

STM32PulseTimer& STM32PulseTimer::setPeriod(double microseconds) {
    return setFrequency(1000000.0 / microseconds);
}

// ---------------------------------------------------------------------------
// LDAC pulse width
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::setLDACPulseWidthNs(uint32_t widthNs) {
    double ticks = ((double)widthNs * 1.0e-9) *
                   ((double)_timerKernelClockHz / (double)_currentDivisor);
    uint32_t t = (uint32_t)(ticks + 0.5);
    if (t < 1) t = 1;                    // at least one tick wide
    _ldacPulseWidthTicks = t;
    return *this;
}

// Program the trailing-edge (CH2) compare to lead + width, clamped to period.
void STM32PulseTimer::programLDACWidth(uint32_t leadTicks) {
    uint32_t arr   = LL_TIM_GetAutoReload(PT_TIM);
    uint32_t trail = leadTicks + _ldacPulseWidthTicks;
    if (trail > arr) trail = arr;        // don't run the trail past the period end
    LL_TIM_OC_SetCompareCH2(PT_TIM, trail);
}

// ---------------------------------------------------------------------------
// applyCompareImmediate - write CH1 (and CH2 width) with preload OFF, so it is
// live in the current period.
// ---------------------------------------------------------------------------
void STM32PulseTimer::applyCompareImmediate(uint32_t leadTicks) {
    LL_TIM_OC_SetCompareCH1(PT_TIM, leadTicks);
    programLDACWidth(leadTicks);
}

// ---------------------------------------------------------------------------
// setComparePosition - the on-the-fly, mid-period, immediate control, with
// explicit passed-compare hazard handling.
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::setComparePosition(uint32_t ticks,
                                                     CompareHazardPolicy policy,
                                                     CompareResult* result) {
    uint32_t arr = LL_TIM_GetAutoReload(PT_TIM);
    if (ticks > arr) ticks = arr;

    // Program the new compare position immediately (preload is off).
    applyCompareImmediate(ticks);

    // --- Passed-compare hazard detection ---
    // Re-read the live counter AFTER writing. If the counter is already at or
    // past the new lead compare, a normal CH1 match will not occur again until
    // the next period -> the pulse for THIS step would be missed.
    //
    // The read-after-write order matters: if we sampled the count first and
    // then wrote, the counter could advance past 'ticks' in the gap and we'd
    // miss the hazard. Writing first, then sampling, means any count >= ticks
    // we observe is authoritative for "already passed".
    uint32_t cnt = LL_TIM_GetCounter(PT_TIM);

    bool passed = (cnt >= ticks);

    if (!passed) {
        if (result) *result = COMPARE_SCHEDULED;
        return *this;   // normal case: compare is ahead, will match this period
    }

    // Hazard: the counter has already passed the requested pulse position.
    switch (policy) {
        case HAZARD_FIRE_NOW:
            // Guarantee the step's LDAC pulse is not lost: emit one pulse now.
            // softwareTriggerLDAC() forces a CH1 compare event so the pin pulse
            // and the "mid" callback both occur exactly as a real match would.
            softwareTriggerLDAC();
            if (result) *result = COMPARE_HAZARD_HANDLED;
            break;

        case HAZARD_SKIP:
            // Caller accepts a missed pulse this period; leave compare set for
            // next period. Nothing else to do.
            if (result) *result = COMPARE_HAZARD_HANDLED;
            break;

        case HAZARD_WRAP:
            // Intentionally treat as "next period" - compare already set; it
            // will match after the wrap. No pulse this period by design.
            if (result) *result = COMPARE_HAZARD_HANDLED;
            break;
    }
    return *this;
}

STM32PulseTimer& STM32PulseTimer::setComparePositionFraction(double frac,
                                                            CompareHazardPolicy policy,
                                                            CompareResult* result) {
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    uint32_t arr = LL_TIM_GetAutoReload(PT_TIM);
    uint32_t ticks = (uint32_t)((double)arr * frac + 0.5);
    return setComparePosition(ticks, policy, result);
}

// ---------------------------------------------------------------------------
// Cycle / execution control
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::setCycleMode(CycleMode mode) {
    _mode = mode;
    if (mode == MODE_ONE_SHOT) LL_TIM_SetOnePulseMode(PT_TIM, LL_TIM_ONEPULSEMODE_SINGLE);
    else                       LL_TIM_SetOnePulseMode(PT_TIM, LL_TIM_ONEPULSEMODE_REPETITIVE);
    return *this;
}

STM32PulseTimer& STM32PulseTimer::setMultiShot(uint32_t count) {
    _remainingCycles = count;                 // counted down in the update ISR
    return setCycleMode(MODE_CONTINUOUS);     // repetitive; ISR enforces the count
}

void STM32PulseTimer::start() {
    LL_TIM_SetCounter(PT_TIM, 0);
    LL_TIM_EnableCounter(PT_TIM);
}

void STM32PulseTimer::stop() {
    LL_TIM_DisableCounter(PT_TIM);
}

// Force a single LDAC pulse immediately (used by the hazard handler and
// available to the sequence engine directly). Generating a CH1 event drives
// the same output action and interrupt as a real compare match.
void STM32PulseTimer::softwareTriggerLDAC() {
    LL_TIM_GenerateEvent_CC1(PT_TIM);
}

// ---------------------------------------------------------------------------
// External trigger - hardware ETR slave mode (zero-latency reset+restart)
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::setExternalTrigger(GPIO_TypeDef* etrPort,
                                                     uint32_t pin,
                                                     uint32_t alternate,
                                                     TriggerEdge edge) {
    // Configure the ETR pin as alternate function input.
    LL_GPIO_SetPinMode(etrPort, pin, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinSpeed(etrPort, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    if (POSITION_VAL(pin) < 8)
        LL_GPIO_SetAFPin_0_7(etrPort, pin, alternate);
    else
        LL_GPIO_SetAFPin_8_15(etrPort, pin, alternate);

    // ETR polarity for the requested edge.
    LL_TIM_SetETR(PT_TIM,
                  LL_TIM_ETR_POLARITY_NONINVERTED,   // rising by default
                  LL_TIM_ETR_PRESCALER_DIV1,
                  LL_TIM_ETR_FILTER_FDIV1);
    if (edge == TRIGGER_FALLING) {
        LL_TIM_SetETR(PT_TIM,
                      LL_TIM_ETR_POLARITY_INVERTED,  // falling
                      LL_TIM_ETR_PRESCALER_DIV1,
                      LL_TIM_ETR_FILTER_FDIV1);
    }

    // Slave mode = TRIGGER-RESET on ETRF: an external edge resets the counter
    // to 0 and restarts the period in hardware. Trigger source = ETRF.
    LL_TIM_SetTriggerInput(PT_TIM, LL_TIM_TS_ETRF);
    LL_TIM_SetSlaveMode(PT_TIM, LL_TIM_SLAVEMODE_RESET);

    _extTriggerActive = true;
    return *this;
}

STM32PulseTimer& STM32PulseTimer::clearExternalTrigger() {
    LL_TIM_SetSlaveMode(PT_TIM, LL_TIM_SLAVEMODE_DISABLED);
    _extTriggerActive = false;
    return *this;
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------
double STM32PulseTimer::getTickDurationNs() {
    return (1.0e9 * (double)_currentDivisor) / (double)_timerKernelClockHz;
}

uint32_t STM32PulseTimer::getCounter()     { return LL_TIM_GetCounter(PT_TIM); }
uint32_t STM32PulseTimer::getPeriodTicks() { return LL_TIM_GetAutoReload(PT_TIM); }

// ---------------------------------------------------------------------------
// Interrupt callbacks
// ---------------------------------------------------------------------------
STM32PulseTimer& STM32PulseTimer::attachInterrupts(void (*onStart)(),
                                                   void (*onMid)(),
                                                   void (*onTerm)()) {
    _callbackStart = onStart;
    _callbackMid   = onMid;
    _callbackTerm  = onTerm;

    // Enable the corresponding interrupt sources:
    //   CH3 compare -> start,  CH1 compare -> mid (LDAC instant),  update -> term
    LL_TIM_EnableIT_CC3(PT_TIM);
    LL_TIM_EnableIT_CC1(PT_TIM);
    LL_TIM_EnableIT_UPDATE(PT_TIM);

    NVIC_EnableIRQ(PT_IRQn);
    return *this;
}

// ---------------------------------------------------------------------------
// ISR dispatch
// ---------------------------------------------------------------------------
void STM32PulseTimer::handleISR() {

    // CH3 = start-of-period
    if (LL_TIM_IsActiveFlag_CC3(PT_TIM)) {
        LL_TIM_ClearFlag_CC3(PT_TIM);
        if (_callbackStart) _callbackStart();
    }

    // CH1 = LDAC pulse instant ("mid")
    if (LL_TIM_IsActiveFlag_CC1(PT_TIM)) {
        LL_TIM_ClearFlag_CC1(PT_TIM);
        if (_callbackMid) _callbackMid();
    }

    // Update = period rollover ("terminal") - where the sequence engine
    // typically loads the next step's DAC values and (optionally) new period.
    if (LL_TIM_IsActiveFlag_UPDATE(PT_TIM)) {
        LL_TIM_ClearFlag_UPDATE(PT_TIM);

        if (_remainingCycles > 0) {
            _remainingCycles--;
            if (_remainingCycles == 0) stop();
        }
        if (_callbackTerm) _callbackTerm();
    }
}

// ---------------------------------------------------------------------------
// Hardware ISR vector
// ---------------------------------------------------------------------------
extern "C" void PT_IRQHandler(void) {
    if (STM32PulseTimer::instance()) STM32PulseTimer::instance()->handleISR();
}
