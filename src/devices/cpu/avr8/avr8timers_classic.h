#ifndef MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H
#define MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H

#pragma once

class avr8_base_timer_device : public device_t 
{
public:
    enum : uint8_t
    {
        AVR8_TIMER_SOURCE_DISABLED, // timer is not clocked at all

        AVR8_TIMER_SOURCE_FCPU, // timer clocked in sync with the CPU core

        AVR8_TIMER_SOURCE_PRESCALER_DIV8,       // clock from common prescaler CLK_DIV8
        AVR8_TIMER_SOURCE_PRESCALER_DIV64,      // clock from common prescaler CLK_DIV64
        AVR8_TIMER_SOURCE_PRESCALER_DIV256,     // clock from common prescaler CLK_DIV256
        AVR8_TIMER_SOURCE_PRESCALER_DIV1024,    // clock from common prescaler CLK_DIV1024

        AVR8_TIMER_TX_FALLING_EDGE, // clock from Tx pin, falling edge
        AVR8_TIMER_TX_RISING_EDGE, //  clock from Tx pin, rising edge

        AVR8_TIMER_SOURCE_USB_PLL, // 16u4/32u4 PLL timer; clock source switched by the PLL controller
    };


    /**
     * Force-unmaps a given WGM mode, needed if the AVR8 variant
     * doesn't support the given timer mode.
     */
    void unmap_wgm_mode(uint8_t which);

    // set callbacks for OCxx line state changes, where applicable
    void set_oc_a_w_cb(devcb_write8 cb) { m_oc_a_w_cb = cb; }
    void set_oc_b_w_cb(devcb_write8 cb) { m_oc_b_w_cb = cb; }
    void set_oc_c_w_cb(devcb_write8 cb) { m_oc_c_w_cb = cb; }
    void set_oc_d_w_cb(devcb_write8 cb) { m_oc_d_w_cb = cb; }

    // TOV/TOIE: timer overflow interrupt, common to all timers
    void set_tov_int_cb(devcb_write_line cb) { m_tov_int_cb = cb; } // TOVx interrupt callback (can clear a pending interrupt)
    void tov_w(uint8_t data); // TOVx clear/acknowledge (when written by program)
    void tov_ack_w(uint8_t data) { m_tov = false; }; // TOVx clear/acknowledge (when AVR8 core takes the interrupt)
    uint8_t tov_r() { return m_tov; } // TOVx status
    void toie_w(uint8_t data);
    uint8_t toie_r() { return m_toie; }

    // OCFxA, OCIExA
    void set_ocf_a_int_cb(devcb_write_line cb) { m_ocf_a_int_cb = cb; }
    void ocf_a_w(uint8_t data);
    void ocf_a_ack_w(uint8_t data) { m_ocf_a = false; }
    uint8_t ocf_a_r() { return m_ocf_a; }
    void ocie_a_w(uint8_t data);
    uint8_t ocie_a_r();
    
    // OCFxB, OCIExB
    void set_ocf_b_int_cb(devcb_write_line cb) { m_ocf_b_int_cb = cb; }
    void ocf_b_w(uint8_t data);
    void ocf_b_ack_w(uint8_t data) { m_ocf_b = false; }
    uint8_t ocf_b_r() { return m_ocf_b; }
    void ocie_b_w(uint8_t data);
    uint8_t ocie_b_r();
    
    // OCFxC
    void set_ocf_c_int_cb(devcb_write_line cb) { m_ocf_c_int_cb = cb; }
    void ocf_c_w(uint8_t data);
    void ocf_c_ack_w(uint8_t data) { m_ocf_c = false; }
    uint8_t ocf_c_r() { return m_ocf_c; }
    void ocie_c_w(uint8_t data);
    uint8_t ocie_c_r();

    // OCFxD
    void set_ocf_d_int_cb(devcb_write_line cb) { m_ocf_d_int_cb = cb; }
    void ocf_d_w(uint8_t data);
    void ocf_d_ack_w(uint8_t data) { m_ocf_c = false; }
    uint8_t ocf_d_r() { return m_ocf_c; }
    void ocie_d_w(uint8_t data);
    uint8_t ocie_d_r();

    // ICF
    void icf_w(uint8_t data);
    uint8_t icf_r();


    /**
     * Writes the PRTIMx value (1 = shutdown, 0 = run).
     * 
     * Typically, "shutdown" mode means the whole timer stops, but there are cases
     * like with the fake RTC timer 2, where the timer will still run under certain
     * conditions. See should_tick().
     */
    void prtim_w(uint8_t data);
    uint8_t prtim_r();

    // generic register access/manipulation
    uint16_t tcnt_internal_r();
    void tcnt_internal_w(uint16_t data);

    void wgm_w(uint8_t data);
    uint8_t wgm_r(uint8_t data);


    /**
     * Tick the timer, NOT accounting for prescaler values.
     * 
     * The timers all expect that some other device manages what
     * clock source actually ticks it. Most of the time that will
     * be the CPU and timer ticking will be done in the main execution loop.
     */
    void tick();

    /**
     * Returns the last timer source set within this timer's internal context.
     * This is to be used if some external clock source releases its control
     * of the timer tick() function, and the timer itself has to restore its
     * last configured timer source.
     */
    uint8_t get_last_timer_source() { return m_last_timer_source; }

    /**
     * Set callback to run when this timer internally switches its
     * clock source. For example, writes to the CS0 bits in timer 0
     * will fire the callback.
     */
    void set_timer_source_switch_cb(devcb_write8 cb) { m_timer_source_switch_cb = cb; }

    /**
     * Set value on Tx pin (e.g., T0, T1, etc.)
     * If timer is configured to be clocked by this input, it responds appropriately.
     * Default implementation does nothing.
     */
    virtual void t_w(uint8_t data) { }

protected:
	typedef delegate<void (void)> timer_func;

    // Timer count and prescaler values, MUST BE SAVESTATE-ABLE.
    uint32_t m_prescale_count;  // number of ticks left in prescale before count is advanced
    uint32_t m_tcnt;            // TCNT; active count. 32-bit internally to prevent calculation problems.
    uint32_t m_active_prescale_reload; // prescale reload value when prescaling is enabled
    uint32_t m_prescale_reload; // prescale reload value at all other times

    // Output compare values, MUST BE SAVESTATE-ABLE.
    // Most timers will only need a couple of these.
    uint16_t m_ocr_a;
    uint16_t m_ocr_b;
    uint16_t m_ocr_c;
    uint16_t m_ocr_d;

    uint8_t m_wgm; // WGM i.e., waveform generation mode (base timer mode)

    uint8_t m_cs; // Clock select (enable/disable timer, prescaler select, switch clock input from CPU to others...)

    // COM i.e., compare/match output mode. setting these can override GPIOs
    uint8_t m_com_a;
    uint8_t m_com_b;
    uint8_t m_com_c;
    uint8_t m_com_d;

    // Interrupt enables and interrupt flags, MUST BE SAVESTATE-ABLE.
    bool m_toie;  // TOIEx; overflow interrupt enable
    bool m_tov;   // TOVx; overflow interrupt flag
    bool m_ocie_a;
    bool m_ocf_a; // OCFxA
    bool m_ocie_b;
    bool m_ocf_b; // OCFxB
    bool m_ocie_c;
    bool m_ocf_c; // OCFxC
    bool m_ocie_d;
    bool m_ocf_d; // OCFxD
    
    bool m_icie; // ICIE; input capture interrupt enable (if necessary)
    bool m_icf;

    bool m_prtim; // PRTIMx bit; usually stops counting when true

    uint8_t m_last_timer_source; // last AVR8_TIMER_SOURCE_xx value set internally by this timer

    // Interrupt callbacks; all write 1 to set interrupt pending and 0 to clear it.
    // When one of these are called, the core or whatever else implementing the callback
    // should NOT try to clear/raise an interrupt again, or we get infinite recursion headaches.
    devcb_write_line m_tov_int_cb;
    devcb_write_line m_ocf_a_int_cb;
    devcb_write_line m_ocf_b_int_cb;
    devcb_write_line m_ocf_c_int_cb;
    devcb_write_line m_ocf_d_int_cb;

    // OCxx output callbacks for modes where we can explicitly request an output value
    devcb_write_line m_oc_a_w_cb; // OCxA output
    devcb_write_line m_oc_b_w_cb; // OCxB output
    devcb_write_line m_oc_c_w_cb; // OCxC output
    devcb_write_line m_oc_d_w_cb; // OCxD output

    // OCxx output callbacks for toggle modes; GPIO mux or something else must keep
    // track of the previous value for us. The value written to these is irrelevant.
    devcb_write_line m_oc_a_toggle_w_cb; // OCxA toggle request
    devcb_write_line m_oc_b_toggle_w_cb; // OCxB toggle request
    devcb_write_line m_oc_c_toggle_w_cb; // OCxC toggle request
    devcb_write_line m_oc_d_toggle_w_cb; // OCxD toggle request


    devcb_write8 m_timer_source_switch_cb; // TODO: probably shouldn't be devcb_write8

    // Callback table that points a wgm value to the timer advance function
    timer_func m_wgm_cbs[0x100];

    void advance_illegal_wgm_state();

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

    // Generic counter update/compare functions.
    // It is guaranteed that calling these will have NO unexpected side effects.
    inline uint32_t next_count() { return ((m_tcnt + 1) & ((1 << get_num_bits()) - 1)); } // compute next tcnt value (obeys wraparound)
    inline void advance_count() { tcnt_internal_w(next_count()); } // set tcnt to next_count() (obeys wraparound)
    inline void reset_count() { tcnt_internal_w(0); } // set tcnt to 0
    inline bool will_count_overflow() { return tcnt_internal_r() != 0 && next_count() == 0; } // return true if tcnt will wrap

    inline bool is_compare_a() { return tcnt_internal_r() == m_ocr_a; }
    inline bool is_compare_b() { return tcnt_internal_r() == m_ocr_b; }
    inline bool is_compare_c() { return tcnt_internal_r() == m_ocr_c; }
    
    inline bool will_compare_a() { return next_count() == m_ocr_a; }
    inline bool will_compare_b() { return next_count() == m_ocr_b; }
    inline bool will_compare_c() { return next_count() == m_ocr_c; }



    // WGM callbacks install
    virtual void install_wgm_callbacks() = 0;

    // return number of bits in this timer (8, 10, 16)
    virtual uint32_t get_num_bits() const = 0;

    // Generic implementation of the "Normal" timer mode, which advances until overflow,
    // then sets the TOV interrupt flag upon wrap to zero.
    // This does not implement the OCxx toggles; subclasses must do that.
    virtual void advance_normal();

    /**
     * Return true if the timer should tick when tick() is called.
     * Forced to be pure virtual so that subclass implementations pay attention to
     * how the PRTIMx behavior actually works for that timer.
     * */
    virtual bool should_tick() = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////
// MIX-INS
//////////////////////////////////////////////////////////////////////////////////////////

/**
 * Generic megaAVR-style TIFR register template.
 * Routes bits in reads/writes to the appropriate registers.
 */
template <typename T,
          int TovBit = -1,
          int OcfABit = -1,
          int OcfBBit = -1,
          int OcfCBit = -1,
          int TicfBit = -1>
class avr8_megaavr_tifr_mixin
{
public:
    void tifr_w(uint8_t data)
    {
        if (0 <= TovBit && TovBit <= 7) T::tov_w(BIT(data, TovBit));
        if (0 <= OcfABit && OcfABit <= 7) T::ocf_a_w(BIT(data, OcfABit));
        if (0 <= OcfBBit && OcfBBit <= 7) T::ocf_b_w(BIT(data, OcfBBit));
        if (0 <= OcfCBit && OcfCBit <= 7) T::ocf_c_w(BIT(data, OcfCBit));
        if (0 <= TicfBit && TicfBit <= 7) T::icf_w(BIT(data, TicfBit));
    }

    uint8_t tifr_r()
    {
        return
            (TovBit >= 0 ? T::tov_r() << TovBit : 0) |
            (OcfABit >= 0 ? T::ocf_a_r() << OcfABit : 0) |
            (OcfBBit >= 0 ? T::ocf_b_r() << OcfBBit : 0) |
            (OcfCBit >= 0 ? T::ocf_c_r() << OcfCBit : 0) |
            (TicfBit >= 0 ? T::icf_r() << TicfBit : 0);
    }
};

/**
 * Generic megaAVR-style TCCRxA register template.
 * Routes bits in reads/writes to the appropriate registers.
 * 
 * Note that TCCRxB and TCCRxC registers vary wildly and can't be genericized.
 */
template <typename T,
          int Wgm0Bit  = -1,
          int Wgm1Bit  = -1,
          int ComA0Bit = -1,
          int ComA1Bit = -1,
          int ComB0Bit = -1,
          int ComB1Bit = -1,
          int ComC0Bit = -1,
          int ComC1Bit = -1>
class avr8_megaavr_tccr_a_mixin
{
public:
    void tccr_a_w(uint8_t data)
    {
        
    }

    uint8_t tccr_a_r(uint8_t data)
    {

    }
};

// Mix-ins that are NOT included here because they can't be genericized:
// - TCNT mixins. Odd behavior can result when TCNT is written while timers are running.
// - TCCRxB and TCCRxC: These vary wildly between implementations.

//////////////////////////////////////////////////////////////////////////////////////////
// TIMER IMPLEMENTATIONS
//////////////////////////////////////////////////////////////////////////////////////////

class avr8_generic_t0_timer_device : public avr8_base_timer_device
{
public:
    enum : uint8_t
    {
        GENERIC_TIMER0_CS_DISABLED         = 0b000, // disable timer entirely
        GENERIC_TIMER0_CS_DIRECT           = 0b001, // clock directly from clk i/o, bypassing the prescaler
        GENERIC_TIMER0_CS_PRESCALE_DIV8    = 0b010, // clk/8 from prescaler
        GENERIC_TIMER0_CS_PRESCALE_DIV64   = 0b011, // clk/64 from prescaler
        GENERIC_TIMER0_CS_PRESCALE_DIV256  = 0b100, // clk/256 from prescaler
        GENERIC_TIMER0_CS_PRESCALE_DIV1024 = 0b101, // clk/1024 from prescaler
        GENERIC_TIMER0_CS_T0_FALL          = 0b110, // T0 pin, falling edge
        GENERIC_TIMER0_CS_T0_RISE          = 0b111, // T0 pin, rising edge
    };

    enum : uint8_t
    {
        /**
         * Normal mode (WGM 0b000): Counts from 0 to 255. At the top, the
         * overflow flag is set. If interrupts are enabled, the CPU takes the
         * interrupt and clears the flag. Otherwise, the flag will stay set
         * until manually acknowledged.
         * 
         * Normal mode uses the same COMxx behavior as CTC mode.
         */
        GENERIC_TIMER0_WGM_NORMAL        = 0b000,

        GENERIC_TIMER0_WGM_PWM_BOTTOM    = 0b001,

        /**
         * CTC (Clear Timer on Compare) mode (WGM 0b010): Similar to normal mode, but it counts from 0
         * to the value set in OCR0A or OCR0B, whichever is reached first. COMxx settings will be obeyed
         * here, and will toggle the OCxA and OCxB pins on compare.
         */
        GENERIC_TIMER0_WGM_CTC           = 0b010,

        /**
         * Fast PWM mode (WGM = 0b011): Counts and signals interrupts the same
         * way as normal mode, but also outputs to the OC0A and OC0B pins on compare.
         */
        GENERIC_TIMER0_WGM_FAST_PWM      = 0b011,

        // 0b100 invalid/reserved

        GENERIC_TIMER0_WGM_PWM_OCR0A     = 0b101,

        // 0b110 invalid/reserved

        GENERIC_TIMER0_WGM_FAST_PWM_OCR0A = 0b111,
    };

    enum : uint8_t
    {
        /**
         * Normal CTC mode. Signal interrupt and wrap to zero
         * when count matches OCR0A or OCR0B.
         */
        GENERIC_TIMER0_COM_NORMAL = 0,

        /**
         * Toggle mode: when count reaches TOP (set by OCR???), toggle OC0B state,
         * then wrap to zero.
         */
        GENERIC_TIMER0_COM_TOGGLE,

        /**
         * Set mode: when count reaches TOP (set by OCR???), set OC0B to 1,
         * then wrap to zero.
         */
        GENERIC_TIMER0_COM_SET,

        /**
         * Clear mode: when count reaches TOP (set by OCR???), set OC0B to 0,
         * then wrap to zero.
         */
        GENERIC_TIMER0_COM_CLEAR,
    };

protected:
    virtual void install_wgm_callbacks() override;

    void advance_fast_pwm(); // =GENERIC_TIMER0_WGM_FAST_PWM
    void advance_ctc();
    
    void update_oc0a_normal_or_ctc();
    void update_oc0b_normal_or_ctc();
    
    void update_oc0a_fast_pwm();
    void update_oc0b_fast_pwm();

    void force_oc0a();
    void force_oc0b();

    virtual uint32_t get_num_bits() const override { return 8; }

    virtual void advance_normal() override;
    virtual bool should_tick() override;

    void cs_w(uint8_t data);
};


// Timer 0 for most megaAVR-series chips, with related megaavr mix-ins.
// Remember that tinyAVR-series chips group timer 0 and timer 1 controls
// into single registers.
class avr8_megaavr_timer_0_device : avr8_generic_t0_timer_device,
                                    avr8_megaavr_tifr_mixin<avr8_megaavr_timer_0_device, 0, 1, 2>,
                                    avr8_megaavr_tccr_a_mixin<avr8_megaavr_timer_0_device, 0, 1, 6, 7, 4, 5>
{
public:
};


#if 0
class avr8_generic_t1_timer_device : avr8_base_timer_device<16, 0b1111>
{
    enum : uint8_t
    {
        GENERIC_TIMER1_WGM_NORMAL         = 0b0000,

        GENERIC_TIMER1_WGM_8_BIT          = 0b0001,

        GENERIC_TIMER1_WGM_9_BIT          = 0b0010,

        GENERIC_TIMER1_WGM_10_BIT         = 0b0011,

        GENERIC_TIMER1_WGM_CTC            = 0b0100,

        GENERIC_TIMER1_WGM_8_BIT_INVERSE  = 0b0101,
        GENERIC_TIMER1_WGM_9_BIT_INVERSE  = 0b0110,
        GENERIC_TIMER1_WGM_10_BIT_INVERSE = 0b0111,
        
        GENERIC_TIMER1_WGM_ICR1           = 0b1000,
        GENERIC_TIMER1_WGM_OCR1A          = 0b1001,
        GENERIC_TIMER1_WGM_ICR1_ALT       = 0b1010,
        GENERIC_TIMER1_WGM_OCR1A_ALT      = 0b1011,

        GENERIC_TIMER1_WGM_CTC_ICR1       = 0b1100,
        // 0b1101 reserved                 
        GENERIC_TIMER1_WGM_FAST_PWM_ICR1  = 0b1110,
        GENERIC_TIMER1_WGM_FAST_PWM_OCR1A = 0b1111,
    };
};
#endif


#if 0
class atmegaxu4_t4_timer_device : avr8_base_timer_device<10>
{

public:
    enum : uint8_t
    {
        /**
         * Single-slope (WGM4 = 0b00): Count from BOTTOM to TOP, then overflow.
         */
        ATMEGA16U4_WGM4_SINGLE_SLOPE = 0b00,

        /**
         * Dual-slope (WGM4 = 0b01): Count from BOTTOM to TOP, then back to BOTTOM.
         */
        ATMEGA16U4_WGM4_DUAL_SLOPE = 0b01,
    };
};
#endif

/**
 * Common 10-bit prescaler that is shared between different timers.
 * It provides taps at /8, /64, /256 and /1024 counts.
 * 
 * The prescaler outputs will be pulsed (NOT held) on the same clock cycle that
 * the timer division is satisfied. See the AVR8 documentation
 * "Timer/Counter Timing Diagrams" section on how that works.
 */
class avr8_10bit_prescaler_device : public device_t 
{
public:
    void set_clk8_w_cb(devcb_write_line cb) { m_clk8_w_cb = cb; }
    void set_clk64_w_cb(devcb_write_line cb) { m_clk64_w_cb = cb; }
    void set_clk256_w_cb(devcb_write_line cb) { m_clk256_w_cb = cb; }
    void set_clk1024_w_cb(devcb_write_line cb) { m_clk1024_w_cb = cb; }

    void set_psrasy_cb(devcb_write_line cb) { m_psrasy_w_cb = cb; }

    void gtccr_w(uint8_t data);
    
    void tick();

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

protected:
    // internal registers, MUST BE SAVESTATE-ABLE
    uint16_t m_count;
    bool m_tsm; // Timer Synchronization Mode; when set, the prescaler does not tick
    bool m_psrasy; // Prescaler Reset Asynchronous; usually routed to timer 2
    bool m_psrsync; // Prescaler reset, automatically clears to 0 when TSM not enabled

    // Prescaler callbacks (ephemeral)
    devcb_write_line m_clk8_w_cb;
    devcb_write_line m_clk64_w_cb;
    devcb_write_line m_clk256_w_cb;
    devcb_write_line m_clk1024_w_cb;
    devcb_write_line m_psrasy_w_cb;
};

// device type definition
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_0, avr8_generic_t0_timer_device)
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_0, avr8_megaavr_timer_0_device)

#endif // MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H
