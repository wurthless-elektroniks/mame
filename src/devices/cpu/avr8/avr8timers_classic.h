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

        AVR8_TIMER_SOURCE_PRESCALER_DIV8, // clock from parent prescaler CLK_DIV8
        AVR8_TIMER_SOURCE_PRESCALER_DIV16, // clock from parent prescaler CLK_DIV16
        AVR8_TIMER_SOURCE_PRESCALER_DIV32, // clock from parent prescaler CLK_DIV32
        AVR8_TIMER_SOURCE_PRESCALER_DIV64, // clock from parent prescaler CLK_DIV64
        AVR8_TIMER_SOURCE_PRESCALER_DIV256, // clock from parent prescaler CLK_DIV256
        AVR8_TIMER_SOURCE_PRESCALER_DIV1024, // clock from parent prescaler CLK_DIV1024

        AVR8_TIMER_SOURCE_TX_FALLING_EDGE, // clock from Tx pin, falling edge
        AVR8_TIMER_SOURCE_TX_RISING_EDGE, // clock from Tx pin, rising edge

        AVR8_TIMER_SOURCE_TOSC1, // clock from TOSC1 (e.g., 32768 Hz crystal)

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
    
    // OCFxC, OCIExC
    void set_ocf_c_int_cb(devcb_write_line cb) { m_ocf_c_int_cb = cb; }
    void ocf_c_w(uint8_t data);
    void ocf_c_ack_w(uint8_t data) { m_ocf_c = false; }
    uint8_t ocf_c_r() { return m_ocf_c; }
    void ocie_c_w(uint8_t data);
    uint8_t ocie_c_r();

    // ICF
    void icf_w(uint8_t data);
    uint8_t icf_r();

    /**
     * Writes the PRTIMx value (1 = shutdown, 0 = run).
     * 
     * Typically, "shutdown" mode means the whole timer stops, but there are cases
     * like with the fake RTC timer 2, where the timer will still run under certain
     * conditions.
     */
    void prtim_w(uint8_t data);
    uint8_t prtim_r();

    // generic register access/manipulation
    uint16_t tcnt_internal_r();
    void tcnt_internal_w(uint16_t data);

    void wgm_w(uint8_t data);
    uint8_t wgm_r(uint8_t data);


    /**
     * Set value on Tx pin (e.g., T0, T1, etc.)
     * 
     * The Tx pin is usually sampled only in time with the system clock,
     * so a simple Tx rise or fall should not automatically count the timer.
     * Read the datasheets for more information.
     */
    void t_w(uint8_t data) { m_t = data != 0; }


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
     * Tick the timer for one clock cycle. The timers are responsible
     * for defining this logic.
     * 
     * What calls tick() depends on the clock source set elsewhere. Most of the time
     * it is the system clock, so it'll be clocked in the execution loop.
     */
    virtual void tick() = 0;

    // TODO: determine if this should be pure virtual or just a common setter
    virtual void cs_w(uint8_t data) = 0;

    uint8_t cs_r() { return m_cs; }

protected:
	typedef delegate<void (void)> timer_func;

    uint32_t m_tcnt; // TCNT; active count. 32-bit internally to prevent calculation problems.

    // Output compare values, MUST BE SAVESTATE-ABLE.
    // Most timers will only need a couple of these.
    uint16_t m_ocr_a;
    uint16_t m_ocr_b;
    uint16_t m_ocr_c;

    uint8_t m_wgm; // WGM i.e., waveform generation mode (base timer mode)

    uint8_t m_cs; // Clock select (enable/disable timer, prescaler select, switch clock inputs...)

    // COM i.e., compare/match output mode. setting these can override GPIOs
    uint8_t m_com_a;
    uint8_t m_com_b;
    uint8_t m_com_c;

    // Interrupt enables and interrupt flags, MUST BE SAVESTATE-ABLE.
    bool m_toie;  // TOIEx; overflow interrupt enable
    bool m_tov;   // TOVx; overflow interrupt flag
    bool m_ocie_a; // OCIExA; compare interrupt enable
    bool m_ocf_a; // OCFxA; compare interrupt flag
    bool m_ocie_b; // OCIExB; compare interrupt enable
    bool m_ocf_b; // OCFxB; compare interrupt flag
    bool m_ocie_c; // OCIExC; compare interrupt enable
    bool m_ocf_c; // OCFxC; compare interrupt flag
    
    bool m_icie; // ICIE; input capture interrupt enable (if necessary)
    bool m_icf;

    bool m_prtim; // PRTIMx bit; usually stops counting when true

    bool m_t; // Tx input pin state, takes effect on next tick
    bool m_t_last; // Tx input pin state on last sample

    uint8_t m_last_timer_source; // last AVR8_TIMER_SOURCE_xx value set internally by this timer

    // Interrupt callbacks; all write 1 to set interrupt pending and 0 to clear it.
    // When one of these are called, the core or whatever else implementing the callback
    // should NOT try to clear/raise an interrupt again, or we get infinite recursion headaches.
    devcb_write_line m_tov_int_cb;
    devcb_write_line m_ocf_a_int_cb;
    devcb_write_line m_ocf_b_int_cb;
    devcb_write_line m_ocf_c_int_cb;

    // OCxx output callbacks when enabling/disabling the outputs
    // so downstream GPIO drivers and other devices can react
    devcb_write_line m_oc_a_en_w_cb; // OCxA enable output
    devcb_write_line m_oc_b_en_w_cb; // OCxB enable output
    devcb_write_line m_oc_c_en_w_cb; // OCxC enable output

    // OCxx output callbacks for modes where we can explicitly request an output value
    devcb_write_line m_oc_a_w_cb; // OCxA output
    devcb_write_line m_oc_b_w_cb; // OCxB output
    devcb_write_line m_oc_c_w_cb; // OCxC output

    // OCxx output callbacks for toggle modes; GPIO mux or something else must keep
    // track of the previous value for us. The value written to these is irrelevant.
    devcb_write_line m_oc_a_toggle_w_cb; // OCxA toggle request
    devcb_write_line m_oc_b_toggle_w_cb; // OCxB toggle request
    devcb_write_line m_oc_c_toggle_w_cb; // OCxC toggle request

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
    inline bool is_compare_d() { return tcnt_internal_r() == m_ocr_d; }
    
    inline bool will_compare_a() { return next_count() == m_ocr_a; }
    inline bool will_compare_b() { return next_count() == m_ocr_b; }
    inline bool will_compare_c() { return next_count() == m_ocr_c; }
    inline bool will_compare_c() { return next_count() == m_ocr_d; }

    // WGM callbacks install
    virtual void install_wgm_callbacks() = 0;

    // return number of bits in this timer (8, 10, 16)
    virtual uint32_t get_num_bits() const = 0;

    // Generic implementation of the "Normal" timer mode, which advances until overflow,
    // then sets the TOV interrupt flag upon wrap to zero.
    // This does not implement the OCxx toggles; subclasses must do that.
    virtual void advance_normal();
};

//////////////////////////////////////////////////////////////////////////////////////////
// MIX-INS
//////////////////////////////////////////////////////////////////////////////////////////

/**
 * Adds signals to connect the timer to avr8_10bit_prescaler_device.
 */
template <typename T>
class avr8_timer_prescaler_input_mixin
{
public:
    // taps from prescaler to the timer itself; not all will be used
    void clk_div8_w(uint8_t data) { m_div8 = data != 0; }
    void clk_div16_w(uint8_t data) { m_div16 = data != 0; }
    void clk_div32_w(uint8_t data) { m_div32 = data != 0; }
    void clk_div64_w(uint8_t data) { m_div64 = data != 0; }
    void clk_div128_w(uint8_t data) { m_div128 = data != 0; }
    void clk_div256_w(uint8_t data) { m_div256 = data != 0; }
    void clk_div1024_w(uint8_t data) { m_div1024 = data != 0; }

protected:
    // MUST BE SAVESTATE-ABLE!
    // The mixin will NOT add these for you!
    bool m_clk_div8;
    bool m_clk_div16;
    bool m_clk_div32;
    bool m_clk_div64;
    bool m_clk_div128;
    bool m_clk_div256;
    bool m_clk_div1024;
};

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
    void tccr_a_w(uint8_t data);
    uint8_t tccr_a_r(uint8_t data);
};

// Mix-ins that are NOT included here because they can't be genericized:
// - TCNT mixins. Odd behavior can result when TCNT is written while timers are running.
// - TCCRxB and TCCRxC: These vary wildly between implementations.

//////////////////////////////////////////////////////////////////////////////////////////
// TIMER IMPLEMENTATIONS
//////////////////////////////////////////////////////////////////////////////////////////

class avr8_generic_t0_timer_device : public avr8_base_timer_device,
                                            avr8_timer_prescaler_input_mixin<avr8_generic_t0_timer_device>
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

public:

    virtual void tick() override;

protected:
    virtual void install_wgm_callbacks() override;

    void advance_fast_pwm(); // =GENERIC_TIMER0_WGM_FAST_PWM
    void advance_ctc();
    
    void update_oc0a_normal_or_ctc();
    void update_oc0b_normal_or_ctc();
    
    void update_oc0a_fast_pwm();
    void update_oc0b_fast_pwm();

    void force_oc0a(); // force OC0A output as if comparison happened; does not set interrupt flag or raise interrupt
    void force_oc0b(); // force OC0B output as if comparison happened; does not set interrupt flag or raise interrupt

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
class avr8_generic_t1_timer_device : public avr8_base_timer_device
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

    enum : uint8_t
    {
        GENERIC_TIMER1_CS_DISABLED         = 0b000, // disable timer entirely
        GENERIC_TIMER1_CS_DIRECT           = 0b001, // clock directly from clk i/o, bypassing the prescaler
        GENERIC_TIMER1_CS_PRESCALE_DIV8    = 0b010, // clk/8 from prescaler
        GENERIC_TIMER1_CS_PRESCALE_DIV64   = 0b011, // clk/64 from prescaler
        GENERIC_TIMER1_CS_PRESCALE_DIV256  = 0b100, // clk/256 from prescaler
        GENERIC_TIMER1_CS_PRESCALE_DIV1024 = 0b101, // clk/1024 from prescaler
        GENERIC_TIMER1_CS_T1_FALL          = 0b110, // T1 pin, falling edge
        GENERIC_TIMER1_CS_T1_RISE          = 0b111, // T1 pin, rising edge
    };

public:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

    void tcnt_h_w(uint8_t data);
    uint8_t tcnt_h_r();
    void tcnt_l_w(uint8_t data);
    uint8_t tcnt_l_r();

    void ocr_a_h_w(uint8_t data);
    void ocr_a_l_w(uint8_t data);

    void ocr_b_h_w(uint8_t data);
    void ocr_b_l_w(uint8_t data);

protected:
    uint8_t m_temp; // temporary register for 16-bit accesses
    uint16_t m_ocr_a_buffer; // OCRxA reload value in PWM mode when timer hits TOP or BOTTOM
    uint16_t m_ocr_b_buffer; // OCRxB reload value in PWM mode when timer hits TOP or BOTTOM
    uint16_t m_ocr_c_buffer; // OCRxC reload value in PWM mode when timer hits TOP or BOTTOM
};
#endif


#if 0

class avr8_generic_fakertc_timer_device : public avr8_base_timer_device,
                                                 avr8_timer_prescaler_input_mixin
public:
    enum : uint8_t
    {
        GENERIC_FAKERTC_CS_DISABLED         = 0b000,
        GENERIC_FAKERTC_CS_DIRECT           = 0b001,
        GENERIC_FAKERTC_CS_PRESCALE_DIV8    = 0b010,
        GENERIC_FAKERTC_CS_PRESCALE_DIV32   = 0b011,
        GENERIC_FAKERTC_CS_PRESCALE_DIV64   = 0b100,
        GENERIC_FAKERTC_CS_PRESCALE_DIV128  = 0b101,
        GENERIC_FAKERTC_CS_PRESCALE_DIV256  = 0b110,
        GENERIC_FAKERTC_CS_PRESCALE_DIV1024 = 0b111,
    };


};

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

    enum : uint8_t
    {
        ATMEGA16U4_TIMER4_COM_DISCONNECTED = 0b00, // disconnect OC4x output
        ATMEGA16U4_TIMER4_COM_TOGGLE       = 0b01,
        ATMEGA16U4_TIMER4_COM_CLEAR        = 0b10,
        ATMEGA16U4_TIMER4_COM_SET          = 0b11,
    };

    enum : uint8_t
    {
        ATMEGA16U4_TIMER4_CS_DISABLE   = 0b0000, // timer is stopped/disabled
        ATMEGA16U4_TIMER4_CS_PCK       = 0b0001, // clock directly
        ATMEGA16U4_TIMER4_CS_PCK_2     = 0b0010, // clock prescaler div 2
        ATMEGA16U4_TIMER4_CS_PCK_4     = 0b0011, // clock prescaler div 4
        ATMEGA16U4_TIMER4_CS_PCK_8     = 0b0100, // clock prescaler div 8
        ATMEGA16U4_TIMER4_CS_PCK_16    = 0b0101, // clock prescaler div 16
        ATMEGA16U4_TIMER4_CS_PCK_32    = 0b0110, // clock prescaler div 32
        ATMEGA16U4_TIMER4_CS_PCK_64    = 0b0111, // clock prescaler div 64
        ATMEGA16U4_TIMER4_CS_PCK_128   = 0b1000, // clock prescaler div 128
        ATMEGA16U4_TIMER4_CS_PCK_256   = 0b1001, // clock prescaler div 256
        ATMEGA16U4_TIMER4_CS_PCK_512   = 0b1010, // clock prescaler div 512
        ATMEGA16U4_TIMER4_CS_PCK_1024  = 0b1011, // clock prescaler div 1024
        ATMEGA16U4_TIMER4_CS_PCK_2048  = 0b1100, // clock prescaler div 2048
        ATMEGA16U4_TIMER4_CS_PCK_4096  = 0b1101, // clock prescaler div 4096
        ATMEGA16U4_TIMER4_CS_PCK_8192  = 0b1110, // clock prescaler div 8192
        ATMEGA16U4_TIMER4_CS_PCK_16384 = 0b1111, // clock prescaler div 16384  
    };
};
#endif

/**
 * Common 10-bit prescaler implementation.
 * 
 * The prescaler for Timer 0 typically has taps at /8, /64, /256 and /1024,
 * and will always be clocked from CLK[i/o].
 * 
 * The prescaler for the fake RTC in Timer 2 has additional /32 and /128 taps.
 * 
 * The prescaler outputs will be pulsed (NOT held) on the same clock cycle that
 * the timer division is satisfied. At all other times they will be pulled low.
 * See the AVR8 documentation "Timer/Counter Timing Diagrams" section on how that works.
 */
class avr8_10bit_prescaler_device : public device_t 
{
public:
    void set_clk8_w_cb(devcb_write_line cb) { m_clk8_w_cb = cb; }
    void set_clk32_w_cb(devcb_write_line cb) { m_clk32_w_cb = cb; }
    void set_clk64_w_cb(devcb_write_line cb) { m_clk64_w_cb = cb; }
    void set_clk128_w_cb(devcb_write_line cb) { m_clk128_w_cb = cb; }
    void set_clk256_w_cb(devcb_write_line cb) { m_clk256_w_cb = cb; }
    void set_clk1024_w_cb(devcb_write_line cb) { m_clk1024_w_cb = cb; }
    
    /**
     * Tick the prescaler from whatever clock source is feeding it.
     */
    void tick();

    void sync_w(uint8_t data);
    uint8_t sync_r();

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

protected:
    // internal registers, MUST BE SAVESTATE-ABLE
    uint16_t m_count;
    bool m_sync;
    
    // Prescaler callbacks, to be installed during various device_add_mconfig phases
    devcb_write_line m_clk8_w_cb;
    devcb_write_line m_clk32_w_cb;
    devcb_write_line m_clk64_w_cb;
    devcb_write_line m_clk128_w_cb;
    devcb_write_line m_clk256_w_cb;
    devcb_write_line m_clk1024_w_cb;
};

/**
 * Output Compare Modulator. Takes two outputs from timer comparators
 * and sends them through to the GPIO port pin.
 * If both outputs are enabled at the same time, the compare outputs
 * will be modulated together based on the PORTx bit value (0 = AND modulate,
 * 1 = OR modulate). At all other times it simply routes the PORTx bit
 * straight to the output pin. In all cases the DDR setting on the receiving
 * GPIO driver always applies.
 */
class avr8_output_compare_modulator_device : public device_t
{
public:
    void set_gpio_bit_w_cb(devcb_write_line cb) { m_gpio_bit_w_wb = cb; }

    void compare_out_a_w(uint8_t data); // OCxx from output compare unit
    void compare_out_en_a(uint8_t data); // from output compare unit; 1 if COMxx != 0
    void compare_out_b_w(uint8_t data); // OCxx from output compare unit
    void compare_out_en_b(uint8_t data); // from output compare unit; 1 if COMxx != 0
    
    void port_bit_w(uint8_t data); // from PORTx bit

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

protected:
    // internal bits, MUST BE SAVESTATE-ABLE
    bool m_compare_out_a;
    bool m_compare_out_en_a;
    bool m_compare_out_b;
    bool m_compare_out_en_b;
    bool m_port_bit;

    void update_outputs();

    devcb_write_line m_gpio_bit_w_wb; // to GPIO output; the receiving GPIO must obey its DDR signal
};

#if 0

/**
 * Asynchronous timer controller for the fake RTC, providing the ASSR register
 * and clock source switching.
 */
class avr8_fakertc_async_controller_device : public device_t 
{
public:
    void set_external_crystal_frequency(attotime period);

    /**
     * Direct line from the TOSC1 pin to the asynchronous timer control unit.
     * If ASSC has EXCLK set, this pin will be used to clock Timer 2.
     */
    void tosc_w(uint8_t data);

    /**
     * Set asynchronous mode. 0 = clock from I/O clock, 1 = clock from external source.
     * 
     * If AS2=0, clk_io ticks the prescaler.
     * If AS=1 and EXCLK=0, m_external_crystal_timer ticks the prescaler.
     * If AS=1 and EXCLK=1, TOSC1 rising edge ticks the prescaler.
     */
    void as2_w(uint8_t data);

    /**
     * clk_io input. If AS2=0 (default), then the prescaler will be ticked
     * on the rising edge of this signal.
     */
    void clk_io_w(uint8_t data);
    
protected:
    bool m_exclk;
    bool m_as2;

    bool m_tosc_last_state;
    bool m_clk_io;

    required_device<avr8_10bit_prescaler_device> m_prescaler;
    required_device<avr8_generic_fakertc_timer_device> m_fakertc_timer;

    emu_timer *m_external_crystal_timer;
};

#endif

// device type definitions
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_0, avr8_generic_t0_timer_device)
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_0, avr8_megaavr_timer_0_device)

DECLARE_DEVICE_TYPE(AVR8_CLASSIC_10BIT_PRESCALER, avr8_10bit_prescaler_device)
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_OUTPUT_COMPARE_MODULATOR, avr8_output_compare_modulator_device)

#endif // MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H
