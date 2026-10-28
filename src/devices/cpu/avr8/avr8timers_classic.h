#ifndef MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H
#define MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H

#pragma once


/**
 * Common 10-bit prescaler implementation.
 * 
 * The prescaler for Timer 0 typically has taps at /8, /64, /256 and /1024,
 * and will always be clocked from clk_io.
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
    avr8_10bit_prescaler_device(const machine_config &mconfig,
                                const char *tag,
                                device_t *owner,
                                uint32_t clock) ATTR_COLD;

    
    /**
     * Tick the prescaler from whatever clock source is feeding it.
     */
    void tick();

    void sync_w(uint8_t data);
    uint8_t sync_r();

    template <int Modulo>
    uint8_t clkdiv_r() { return (m_count % Modulo) == 0; }

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

protected:
    // internal registers, MUST BE SAVESTATE-ABLE
    uint16_t m_count;
    bool m_sync;
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
    avr8_output_compare_modulator_device(const machine_config &mconfig,
                                         const char *tag,
                                         device_t *owner,
                                         uint32_t clock);

    // inputs from output compare units
    void compare_out_a_w(uint8_t data); // OCxx from output compare unit
    void compare_out_en_a(uint8_t data); // from output compare unit; 1 if COMxx != 0
    void compare_out_b_w(uint8_t data); // OCxx from output compare unit
    void compare_out_en_b(uint8_t data); // from output compare unit; 1 if COMxx != 0
    
    // inputs from PORTx mux
    void port_bit_w(uint8_t data); // from PORTx bit

    // outputs to GPIO mux
    auto gpio_bit_w_cb() { return m_gpio_bit_w_wb.bind(); }

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

class avr8_base_timer_device : public device_t 
{
public:
    enum : uint8_t
    {
        AVR8_TIMER_COM_NORMAL = 0b00,
        AVR8_TIMER_COM_TOGGLE = 0b01,
        AVR8_TIMER_COM_CLEAR  = 0b10,
        AVR8_TIMER_COM_SET    = 0b11
    };

    /**
     * Force-unmaps a given WGM mode, needed if the AVR8 variant
     * doesn't support the given timer mode.
     */
    void unmap_wgm_mode(uint8_t which);

    // set callbacks for OCxx line state changes, where applicable
    auto oc_a_w_cb() { return m_oc_a_w_cb.bind(); }
    auto oc_b_w_cb() { return m_oc_b_w_cb.bind(); }
    auto oc_c_w_cb() { return m_oc_c_w_cb.bind(); }

    auto oc_a_en_w_cb() { return m_oc_a_en_w_cb.bind(); }
    auto oc_b_en_w_cb() { return m_oc_b_en_w_cb.bind(); }
    auto oc_c_en_w_cb() { return m_oc_c_en_w_cb.bind(); }

    auto oc_a_toggle_w_cb() { return m_oc_a_toggle_w_cb.bind(); }
    auto oc_b_toggle_w_cb() { return m_oc_b_toggle_w_cb.bind(); }
    auto oc_c_toggle_w_cb() { return m_oc_c_toggle_w_cb.bind(); }

    // TOV/TOIE: timer overflow interrupt, common to all timers
    auto tov_int_cb() { return m_tov_int_cb.bind(); } // TOVx interrupt callback (can clear a pending interrupt)
    void tov_w(uint8_t data); // TOVx clear/acknowledge (when written by program)
    void tov_ack_w(uint8_t data); // TOVx clear/acknowledge (when AVR8 core takes the interrupt)
    uint8_t tov_r() { return m_tov; } // TOVx status
    void toie_w(uint8_t data);
    uint8_t toie_r() { return m_toie; }

    // OCFxA, OCIExA
    auto ocf_a_int_cb() { return m_ocf_a_int_cb.bind(); }
    void ocf_a_w(uint8_t data);
    void ocf_a_ack_w(uint8_t data);
    uint8_t ocf_a_r() { return m_ocf_a; }
    void ocie_a_w(uint8_t data);
    uint8_t ocie_a_r() { return m_ocie_a; }
    
    // OCFxB, OCIExB
    auto ocf_b_int_cb() { return m_ocf_b_int_cb.bind(); }
    void ocf_b_w(uint8_t data);
    void ocf_b_ack_w(uint8_t data);
    uint8_t ocf_b_r() { return m_ocf_b; }
    void ocie_b_w(uint8_t data);
    uint8_t ocie_b_r() { return m_ocie_b; }
    
    // OCFxC, OCIExC
    auto ocf_c_int_cb() { return m_ocf_c_int_cb.bind(); }
    void ocf_c_w(uint8_t data);
    void ocf_c_ack_w(uint8_t data);
    uint8_t ocf_c_r() { return m_ocf_c; }
    void ocie_c_w(uint8_t data);
    uint8_t ocie_c_r() { return m_ocie_c; }

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

    virtual void wgm_w(uint8_t data) = 0; // WGM modes vary wildly between each timer type
    uint8_t wgm_r() { return m_wgm; }

    // COMxx (Compare Output Mode) write handlers must be installed in the subclasses
    // because the setting values tend to change between timer types.
    // The default implementations should panic if called.
    //
    // Reads can just return the m_com_x value; there's no side effects.
    virtual void com_a_w(uint8_t data); // COMxA write
    uint8_t com_a_r() { return m_com_a; }
    
    virtual void com_b_w(uint8_t data);
    uint8_t com_b_r() { return m_com_b; }
    
    virtual void com_c_w(uint8_t data);
    uint8_t com_c_r() { return m_com_c; }

    /**
     * Set incoming value on Tx pin (T0, T1, etc.). This is to be called
     * when the GPIO pin state changes at all, not just for input state changes.
     * Per the datasheets, "transitions on the [Tx] pin will clock the
     * counter even if the pin is configured as an output". Exceptions
     * may apply; read the datasheets to double-check.
     * 
     * The Tx pin is usually sampled only in time with the system clock,
     * so toggles on this signal won't necessarily tick the timer.
     */
    void t_w(uint8_t data) { m_t = data != 0; }

    /**
     * Tick the timer for one clock cycle. The timer subclasses are responsible
     * for defining this logic.
     * 
     * What calls tick() depends on the clock source set elsewhere. Most of the time
     * it is the system clock, so it'll be clocked in the execution loop.
     */
    virtual void tick() = 0;

    // TODO: determine if this should be pure virtual or just a common setter
    virtual void cs_w(uint8_t data) = 0;

    uint8_t cs_r() { return m_cs; }

    /**
     * Change clk_io line state. clk_io is fed from the master clock generator
     * and is tied to the AVR8 CPU clock. If the timer is configured to be
     * synchronous, then the rising edge of clk_io will cause the timer to tick().
     */
    virtual void clk_io_w(uint8_t data);

    template <typename T> void set_upstream_prescaler_tag(T &&tag) { m_upstream_prescaler.set_tag(std::forward<T>(tag)); }

protected:
    avr8_base_timer_device(const machine_config &mconfig,
					       const device_type type,
                           const char *tag,
                           device_t *owner,
                           uint32_t clock) ATTR_COLD;

	typedef delegate<void (void)> timer_func;

    // TODO: should this live in base, or in a different subclass?
    required_device<avr8_10bit_prescaler_device> m_upstream_prescaler;

    // TCNT; active count. MUST BE SAVESTATE-ABLE.
    // 32-bit internally to prevent calculation problems.
    // Anything that writes to this register assumes full responsibility
    // over side-effects which may or may not happen.
    uint32_t m_tcnt;

    // Output compare values, MUST BE SAVESTATE-ABLE.
    // Most timers will only need a couple of these.
    uint16_t m_ocr_a;
    uint16_t m_ocr_b;
    uint16_t m_ocr_c;
    uint16_t m_ocr_a_buf; // OCRxA double-buffer value; needed for PWM modes
    uint16_t m_ocr_b_buf;
    uint16_t m_ocr_c_buf;

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
    
    bool m_icie; // ICIE; input capture interrupt enable
    bool m_icf;

    bool m_prtim; // PRTIMx bit; usually stops counting when true

    bool m_t; // Tx input pin state, takes effect on next tick
    bool m_t_last; // Tx input pin state on last sample

    bool m_dual_slope; // dual slope counting enable
    bool m_dual_slope_counting_down;

    bool m_no_compare_this_cycle; // set if TCNTx is written; blocks compare operation on current timer cycle

    bool m_clk_io; // clk_io signal; derrived from master clock. typically the same as the CPU clcok

    // Interrupt callbacks; all write 1 to set interrupt pending and 0 to clear it.
    // When one of these are called, the core or whatever else implementing the callback
    // should NOT try to clear/raise an interrupt again, or we get infinite recursion headaches.
    devcb_write_line m_tov_int_cb;
    devcb_write_line m_ocf_a_int_cb;
    devcb_write_line m_ocf_b_int_cb;
    devcb_write_line m_ocf_c_int_cb;

    // OCxx output callbacks when enabling/disabling the outputs
    // so downstream GPIO drivers and other devices can react.
    devcb_write_line m_oc_a_en_w_cb; // OCxA enable output
    devcb_write_line m_oc_b_en_w_cb; // OCxB enable output
    devcb_write_line m_oc_c_en_w_cb; // OCxC enable output

    // OCxx output callbacks for modes where we can explicitly request an output value
    devcb_write_line m_oc_a_w_cb; // OCxA output
    devcb_write_line m_oc_b_w_cb; // OCxB output
    devcb_write_line m_oc_c_w_cb; // OCxC output

    // OCxx output callbacks for toggle modes; GPIO mux or something else must keep
    // track of the previous value for us. The value written is irrelevant.
    devcb_write_line m_oc_a_toggle_w_cb; // OCxA toggle request
    devcb_write_line m_oc_b_toggle_w_cb; // OCxB toggle request
    devcb_write_line m_oc_c_toggle_w_cb; // OCxC toggle request

    // Callback table that points a wgm value to the timer advance function
    avr8_base_timer_device::timer_func m_wgm_cbs[0x100];

    void advance_illegal_wgm_state();

    // device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

    // Generic counter update/compare functions.
    // It is guaranteed that calling these will have NO unexpected side effects.
    inline uint32_t max_count() { return (1 << get_num_bits()) - 1; } // return MAX value (0xff for 8-bit timers, 0xffff for 16-bit timers, etc.)
    inline uint32_t next_count() { return (m_tcnt + 1) & max_count(); } // return TCNT+1, wrapping to zero on overflow
    inline uint32_t prev_count() { return (m_tcnt - 1) & max_count(); } // return TCNT-1, wrapping to MAX on underflow
    inline bool will_next_count_overflow() { return m_tcnt != 0 && next_count() == 0; } // return true if tcnt will wrap
    inline bool will_prev_count_reach_bottom() { return m_tcnt != 0 && prev_count() == 0; } // return true if tcnt will hit 0 if counting down
    inline bool is_compare_a() { return m_tcnt == m_ocr_a; }
    inline bool is_compare_b() { return m_tcnt == m_ocr_b; }
    inline bool is_compare_c() { return m_tcnt == m_ocr_c; }
    inline bool will_compare_a() { return next_count() == m_ocr_a; }
    inline bool will_compare_b() { return next_count() == m_ocr_b; }
    inline bool will_compare_c() { return next_count() == m_ocr_c; }
    inline bool will_prev_compare_a() { return prev_count() == m_ocr_a; }
    inline bool will_prev_compare_b() { return prev_count() == m_ocr_b; }
    inline bool will_prev_compare_c() { return prev_count() == m_ocr_c; }
    
    void raise_tov();   // raise TOV flag and signal an interrupt if enabled
    void raise_ocf_a(); // raise OCFxA flag and signal an interrupt if enabled
    void raise_ocf_b(); // raise OCFxB flag and signal an interrupt if enabled
    void raise_ocf_c(); // raise OCFxC flag and signal an interrupt if enabled

    void clear_tov(); // clear TOV flag and de-assert CPU interrupt
    void clear_ocf_a(); // clear OCFxA flag and de-assert CPU interrupt
    void clear_ocf_b(); // clear OCFxB flag and de-assert CPU interrupt
    void clear_ocf_c(); // clear OCFxC flag and de-assert CPU interrupt

    virtual bool is_async_mode() { return false; } // default assumes synchronous timer, so always returns false

    // WGM callbacks install
    virtual void install_wgm_callbacks() = 0;

    // return number of bits in this timer (8, 10, 16)
    virtual uint32_t get_num_bits() const = 0;

    // Generic implementation of the "Normal" timer mode, which advances until overflow,
    // then sets the TOV interrupt flag upon wrap to zero.
    // This does not implement the OCxx toggles; subclasses must do that.
    virtual void advance_normal();

    void update_oc_a_nonpwm_generic(); 
    void update_oc_b_nonpwm_generic(); 
    void update_oc_c_nonpwm_generic(); 
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
        if (0 <= TovBit && TovBit <= 7) ((T*)this)->tov_w(BIT(data, TovBit));
        if (0 <= OcfABit && OcfABit <= 7) ((T*)this)->ocf_a_w(BIT(data, OcfABit));
        if (0 <= OcfBBit && OcfBBit <= 7) ((T*)this)->ocf_b_w(BIT(data, OcfBBit));
        if (0 <= OcfCBit && OcfCBit <= 7) ((T*)this)->ocf_c_w(BIT(data, OcfCBit));
        if (0 <= TicfBit && TicfBit <= 7) ((T*)this)->icf_w(BIT(data, TicfBit));
    }

    uint8_t tifr_r()
    {
        return
            (0 <= TovBit && TovBit <= 7 ? ((T*)this)->tov_r() << TovBit : 0) |
            (0 <= OcfABit && OcfABit <= 7 ? ((T*)this)->ocf_a_r() << OcfABit : 0) |
            (0 <= OcfBBit && OcfBBit <= 7 ? ((T*)this)->ocf_b_r() << OcfBBit : 0) |
            (0 <= OcfCBit && OcfCBit <= 7 ? ((T*)this)->ocf_c_r() << OcfCBit : 0) |
            (0 <= TicfBit && TicfBit <= 7 ? ((T*)this)->icf_r() << TicfBit : 0);
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
        if ((0 <= Wgm0Bit && Wgm0Bit <= 7) || (0 <= Wgm1Bit && Wgm1Bit <= 7))
        {
            uint8_t wgm = ((T*)this)->wgm_r() & 0b11111100;
            if (0 <= Wgm1Bit && Wgm1Bit <= 7) wgm |= BIT(data, Wgm1Bit) << 1;
            if (0 <= Wgm0Bit && Wgm0Bit <= 7) wgm |= BIT(data, Wgm0Bit);
            ((T*)this)->wgm_w(wgm);
        }

        if ((0 <= ComA1Bit && ComA1Bit <= 7) || (0 <= ComA0Bit && ComA0Bit <= 7))
        {
            ((T*)this)->com_a_w((0 <= ComA1Bit && ComA1Bit <= 7 ? BIT(data, ComA1Bit) << 1 : 0) |
                    (0 <= ComA0Bit && ComA0Bit <= 7 ? BIT(data, ComA0Bit) << 0 : 0));
        }

        if ((0 <= ComB1Bit && ComB1Bit <= 7) || (0 <= ComB0Bit && ComB0Bit <= 7))
        {
            ((T*)this)->com_b_w((0 <= ComB1Bit && ComB1Bit <= 7 ? BIT(data, ComB1Bit) << 1 : 0) |
                    (0 <= ComB0Bit && ComB0Bit <= 7 ? BIT(data, ComB0Bit) << 0 : 0));
        }
        
        if ((0 <= ComC1Bit && ComC1Bit <= 7) || (0 <= ComC0Bit && ComC0Bit <= 7))
        {
            ((T*)this)->com_c_w((0 <= ComC1Bit && ComC1Bit <= 7 ? BIT(data, ComC1Bit) << 1 : 0) |
                    (0 <= ComC0Bit && ComC0Bit <= 7 ? BIT(data, ComC0Bit) << 0 : 0));
        }
    }

    uint8_t tccr_a_r()
    {
        uint8_t value = 0;

        if ((0 <= Wgm0Bit && Wgm0Bit <= 7) || (0 <= Wgm1Bit && Wgm1Bit <= 7))
        {
            uint8_t wgm = ((T*)this)->wgm_r() & 0b11111100;
            if (0 <= Wgm1Bit && Wgm1Bit <= 7) value |= BIT(wgm, 1) << Wgm1Bit;
            if (0 <= Wgm0Bit && Wgm0Bit <= 7) value |= BIT(wgm, 0) << Wgm0Bit;
        }

        if ((0 <= ComA1Bit && ComA1Bit <= 7) || (0 <= ComA0Bit && ComA0Bit <= 7))
        {
            uint8_t com_a = ((T*)this)->com_a_r();
            if (0 <= ComA1Bit && ComA1Bit <= 7) value |= BIT(com_a, 1) << ComA1Bit;
            if (0 <= ComA0Bit && ComA0Bit <= 7) value |= BIT(com_a, 0) << ComA0Bit;
        }

        if ((0 <= ComB1Bit && ComB1Bit <= 7) || (0 <= ComB0Bit && ComB0Bit <= 7))
        {
            uint8_t com_b = ((T*)this)->com_b_r();
            if (0 <= ComB1Bit && ComB1Bit <= 7) value |= BIT(com_b, 1) << ComB1Bit;
            if (0 <= ComB0Bit && ComB0Bit <= 7) value |= BIT(com_b, 0) << ComB0Bit;
        }
        
        if ((0 <= ComC1Bit && ComC1Bit <= 7) || (0 <= ComC0Bit && ComC0Bit <= 7))
        {
            uint8_t com_c = ((T*)this)->com_c_r();
            if (0 <= ComC1Bit && ComB1Bit <= 7) value |= BIT(com_c, 1) << ComB1Bit;
            if (0 <= ComB0Bit && ComB0Bit <= 7) value |= BIT(com_c, 0) << ComB0Bit;
        }

        return value;
    }
};

template <typename T,
          int ToieBit = -1,
          int OcieABit = -1,
          int OcieBBit = -1,
          int OcieCBit = -1,
          int IcieBit = -1>
class avr8_megaavr_timsk_mixin
{
public:
    void timsk_w(uint8_t data)
    {
        if (0 <= ToieBit && ToieBit <= 7) ((T*)this)->toie_w(BIT(data, ToieBit));
        if (0 <= OcieABit && OcieABit <= 7) ((T*)this)->ocie_a_w(BIT(data, OcieABit));
        if (0 <= OcieBBit && OcieBBit <= 7) ((T*)this)->ocie_b_w(BIT(data, OcieBBit));
        if (0 <= OcieCBit && OcieCBit <= 7) ((T*)this)->ocie_c_w(BIT(data, OcieCBit));

        // TODO: ICIE bit handling
    }

    uint8_t timsk_r()
    {
        uint8_t result = 0;

        if (0 <= ToieBit && ToieBit <= 7) result |= ((T*)this)->toie_r() << ToieBit;
        if (0 <= OcieABit && OcieABit <= 7) result |= ((T*)this)->ocie_a_r() << OcieABit;
        if (0 <= OcieBBit && OcieBBit <= 7) result |= ((T*)this)->ocie_b_r() << OcieBBit;
        if (0 <= OcieCBit && OcieCBit <= 7) result |= ((T*)this)->ocie_c_r() << OcieCBit;

        // TODO: ICIE bit handling

        return result;
    }
};

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
        GENERIC_TIMER0_CS_T0_FALL          = 0b110, // T0 pin, falling edge, sampled on clk i/o pulse
        GENERIC_TIMER0_CS_T0_RISE          = 0b111, // T0 pin, rising edge, sampled on clk i/o pulse
    };

    enum : uint8_t
    {
        /**
         * Normal mode (WGM 0b000): Counts from 0 to 255. TOV0 is set upon
         * wrap from 255 to 0.
         * 
         * Normal mode uses the same COMxx behavior as CTC mode.
         */
        GENERIC_TIMER0_WGM_NORMAL        = 0b000,

        /**
         * Phase Correct PWM Mode (WGM 0b001): Count from 0x00 to 0xff, then count
         * down from 0xff to 0x00.
         */
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

        /**
         * Phase Correct PWM Mode, counting to OCR0A (WGM 0b101)
         */
        GENERIC_TIMER0_WGM_PWM_OCR0A     = 0b101,

        // 0b110 invalid/reserved

        /**
         * Fast PWM mode, counting to OCR0A (WGM = 0b111).
         */
        GENERIC_TIMER0_WGM_FAST_PWM_OCR0A = 0b111,
    };

public:
    avr8_generic_t0_timer_device(const machine_config &mconfig,
                                 const char *tag,
                                 device_t *owner,
                                 uint32_t clock,
                                 const device_type device_type);

    virtual void tick() override;

    virtual void com_a_w(uint8_t data) override;
    virtual void com_b_w(uint8_t data) override;

    void ocr_a_w(uint8_t data);
    uint8_t ocr_a_r();
    void ocr_b_w(uint8_t data);
    uint8_t ocr_b_r();

    void tcnt_w(uint8_t data);
    uint8_t tcnt_r() { return m_tcnt & 0xff; }

    void tccr_b_w(uint8_t data);
    uint8_t tccr_b_r();


    virtual void wgm_w(uint8_t data) override;

protected:
    virtual void install_wgm_callbacks() override;

    void advance_ctc();
    void advance_pwm();
    void advance_pwm_ocr0a();
    void advance_fast_pwm(); // =GENERIC_TIMER0_WGM_FAST_PWM
    void advance_fast_pwm_ocr0a();     

    void update_oc0a_fast_pwm_on_compare();
    void update_oc0a_fast_pwm_on_top();
    void update_oc0b_fast_pwm_on_compare();
    void update_oc0b_fast_pwm_on_top();

    void update_oc0a_pwm_on_upcount_compare();
    void update_oc0a_pwm_on_downcount_compare();
    void update_oc0b_pwm_on_upcount_compare();
    void update_oc0b_pwm_on_downcount_compare();


    void force_oc0a(); // force OC0A output as if comparison happened; does not set interrupt flag
    void force_oc0b(); // force OC0B output as if comparison happened; does not set interrupt flag

    virtual uint32_t get_num_bits() const override { return 8; }

    virtual void advance_normal() override;
    

    void cs_w(uint8_t data) override;

};


/**
 * Timer 0 implementation for most classic megaAVR-series chips,
 * with related megaAVR mix-ins. 
 * 
 * This timer implementation does not apply to the ATMega8 and relatives,
 * nor does it apply to the tinyAVRs, because their TIFR and TIMSK registers
 * are mapped to more than one timer.
 */
class avr8_megaavr_timer_0_device : public avr8_generic_t0_timer_device,
                                    public avr8_megaavr_tifr_mixin<avr8_megaavr_timer_0_device, 0, 1, 2>,
                                    public avr8_megaavr_tccr_a_mixin<avr8_megaavr_timer_0_device, 0, 1, 6, 7, 4, 5>,
                                    public avr8_megaavr_timsk_mixin<avr8_megaavr_timer_0_device, 0, 1, 2>
{
public:
    avr8_megaavr_timer_0_device(const machine_config &mconfig,
                                const char *tag,
                                device_t *owner,
                                uint32_t clock);                         
};


// TODO: rename to avr8_megaavr_timer_1_device, as all classic megaAVRs share this
class avr8_generic_t1_timer_device : public avr8_base_timer_device,
                                     public avr8_megaavr_tifr_mixin<avr8_generic_t1_timer_device, 0, 1, 2>,
                                     public avr8_megaavr_tccr_a_mixin<avr8_generic_t1_timer_device, 0, 1, 6, 7, 4, 5>,
                                     public avr8_megaavr_timsk_mixin<avr8_generic_t1_timer_device, 0, 1, 2>
{
    // see https://ww1.microchip.com/downloads/en/DeviceDoc/ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061A.pdf
    // Table 16-4.
    enum : uint8_t
    {
        GENERIC_TIMER1_WGM_NORMAL          = 0b0000, // normal: count 0x0000-0xffff
        GENERIC_TIMER1_WGM_PWM_8_BIT       = 0b0001, // phase-correct, 8-bit: count 0x0000-0x00ff
        GENERIC_TIMER1_WGM_PWM_9_BIT       = 0b0010, // phase-correct, 9-bit: count 0x0000-0x01ff
        GENERIC_TIMER1_WGM_PWM_10_BIT      = 0b0011, // phase-correct, 9-bit: count 0x0000-0x03ff
        GENERIC_TIMER1_WGM_CTC             = 0b0100, // CTC: count to OCR1A
        GENERIC_TIMER1_WGM_FAST_PWM_8_BIT  = 0b0101, // fast PWM, 8-bit: count 0x0000-0x00ff
        GENERIC_TIMER1_WGM_FAST_PWM_9_BIT  = 0b0110, // fast PWM, 9-bit: count 0x0000-0x01ff
        GENERIC_TIMER1_WGM_FAST_PWM_10_BIT = 0b0111, // fast PWM, 10-bit: count 0x0000-0x03ff
        GENERIC_TIMER1_WGM_PWM_PFC_ICR1    = 0b1000, // phase/frequency correct; TOP = ICR1
        GENERIC_TIMER1_WGM_PWM_PFC_OCR1A   = 0b1001, // phase/frequency correct; TOP = OCR1A
        GENERIC_TIMER1_WGM_PWM_ICR1        = 0b1010, // phase-correct; TOP = ICR1
        GENERIC_TIMER1_WGM_PWM_OCR1A       = 0b1011, // phase-correct; TOP = OCR1A
        GENERIC_TIMER1_WGM_CTC_ICR1        = 0b1100, // CTC: count to ICR1
        // 0b1101: reserved/illegal
        GENERIC_TIMER1_WGM_FAST_PWM_ICR1   = 0b1110, // fast PWM; count to ICR1
        GENERIC_TIMER1_WGM_FAST_PWM_OCRA   = 0b1111, // fast PWM; count to OCR1A. craft needs this to run
    };

    // while the CS settings are the same as timer 0,
    // we can't common them together because other timers
    // might have different CS settings.
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
    avr8_generic_t1_timer_device(const machine_config &mconfig,
        const char *tag,
        device_t *owner,
        uint32_t clock);

    void set_ocr_channel_c_enabled(bool enabled) { m_ocr_c_enabled = enabled; } 

    void tcnt_h_w(uint8_t data);
    uint8_t tcnt_h_r();
    void tcnt_l_w(uint8_t data);
    uint8_t tcnt_l_r();

    void ocr_a_h_w(uint8_t data);
    uint8_t ocr_a_h_r();
    void ocr_a_l_w(uint8_t data);
    uint8_t ocr_a_l_r();
    
    void ocr_b_h_w(uint8_t data);
    uint8_t ocr_b_h_r();
    void ocr_b_l_w(uint8_t data);
    uint8_t ocr_b_l_r();
    
    void ocr_c_h_w(uint8_t data);
    uint8_t ocr_c_h_r();
    void ocr_c_l_w(uint8_t data);
    uint8_t ocr_c_l_r();

    void tccr_b_w(uint8_t data);
    uint8_t tccr_b_r();
    void tccr_c_w(uint8_t data);
    uint8_t tccr_c_r();
    
    void tick() override;

    virtual void com_a_w(uint8_t data) override;
    virtual void com_b_w(uint8_t data) override;
    virtual void com_c_w(uint8_t data) override;

    virtual void wgm_w(uint8_t data) override;
    virtual void cs_w(uint8_t data) override;
protected:
    avr8_generic_t1_timer_device(const machine_config &mconfig,
        const char *tag,
        device_t *owner,
        uint32_t clock,
        const device_type device_type);

    uint8_t m_temp; // temporary register for 16-bit accesses
    
    bool m_ocr_c_enabled; // true only on devices that have 3-channel 16-bit timers

    virtual void install_wgm_callbacks() override;
    virtual void advance_normal() override;

    virtual uint32_t get_num_bits() const override { return 16; }



    void advance_pwm_common(uint16_t top);

    void advance_pwm_8bit();
    void advance_pwm_9bit();
    void advance_pwm_10bit();

    void advance_fast_pwm_ocr_a();

    void update_oc_a_pwm_on_downcount_compare();
    void update_oc_a_pwm_on_upcount_compare();
    void update_oc_b_pwm_on_downcount_compare();
    void update_oc_b_pwm_on_upcount_compare();
    void update_oc_c_pwm_on_downcount_compare();
    void update_oc_c_pwm_on_upcount_compare();
};


#if 0

class avr8_generic_fakertc_timer_device : public avr8_base_timer_device
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


#if 0

/**
 * Asynchronous timer controller for the fake RTC, providing the ASSR register
 * and clock source switching.
 */
class avr8_fakertc_async_controller_device : public device_t 
{
public:
    /**
     * Set external crystal frequency. Default setting is 0 (do not clock).
     */
    void set_external_crystal_frequency(attotime period);

    /**
     * Direct line from the TOSC1 pin to the asynchronous timer control unit.
     * If ASSC has EXCLK set, the prescaler will be clocked on TOSC1's rising edge.
     */
    void tosc_w(uint8_t data);

    /**
     * Set asynchronous mode. 0 = clock from I/O clock, 1 = clock from external source.
     * 
     * If AS2=0, clk_io rising edge ticks the prescaler.
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

DECLARE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_1, avr8_generic_t1_timer_device)

DECLARE_DEVICE_TYPE(AVR8_CLASSIC_10BIT_PRESCALER, avr8_10bit_prescaler_device)
DECLARE_DEVICE_TYPE(AVR8_CLASSIC_OUTPUT_COMPARE_MODULATOR, avr8_output_compare_modulator_device)

#endif // MAME_CPU_AVR8_AVR8TIMERS_CLASSIC_H
