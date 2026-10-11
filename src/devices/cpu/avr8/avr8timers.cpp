// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, Sandro Ronco, Felipe Sanches, wurthless-elektroniks
/****************************************************************************

    AVR8-series timers
    Device-ified and modularized by würthless elektroniks
    With much love to all the avr8 core devs and maintainers

    Design philosophy:
    -----------------------------------------------------------------

    - Airgap all timer classes from the AVR8 core implementations.
      The timers are devices and not really part of the CPU core itself,
      so all we really have are lines that come in, and lines that go out.
    
    - The AVR8 core keeps track of what calls tick(), and switches time
      sources for us. However, any write to a timer register that forces
      a time source switch should signal back to the core, or whatever
      is listening, that it's time to switch to another source.
      See AVR8_TIMER_SOURCE_xx enums.

    Brief intro to the AVR8 timer subsytem:
    -----------------------------------------------------------------

    Typically, AVR8s have the following kinds of timers: 8-bit low-precision,
    16-bit high precision, and/or 10-bit high speed.

    | Series       | Timer 0 | Timer 1 | Timer 2 | Timer 3 | Timer 4 | Timer 5 |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega       | Generic | Generic | 8-bit   |  n/a    |   n/a   |   n/a   | 
    | 88, 168, 328 | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega       | Generic | Generic |  n/a    | 16-bit  | 10-bit  | n/a     |
    | 16U4, 32U4   | timer 0 | timer 1 |         |         | hispeed |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 640,  | Generic | 16-bit  | 8-bit   | 16-bit  | 16-bit  | 16-bit  |
    | 1280, 2560   | timer 0 | 3 ch.   | fakeRTC | 3 ch.   | 3 ch.   | 3 ch.   |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 644,  | Generic | Generic | 8-bit   |  n/a    |   n/a   |   n/a   | 
    | 1284, 2564   | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny15     | Generic |  n/a    |  n/a    |  n/a    |  n/a    |  n/a    |
    |              | timer 0 |         |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny25,    | Generic | 8-bit   |  n/a    |  n/a    |  n/a    |  n/a    |
    | 45, 85       | timer 0 | hispeed |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|

    - Generic timer 0 is a common 8-bit timer across the AVR8 family.

    - The "fake RTC" timer can have its time source set either to the CPU clock,
      but also to an external crystal, which is usually a 32.768 kHz crystal
      for timekeeping.

    - The high-speed timers can be clocked by an on-chip PLL at many dozens
      of megahertz.

****************************************************************************/

#include "emu.h"
#include "avr8timers.h"

DEFINE_DEVICE_TYPE(AVR8_GENERIC_TIMER_0, avr8_generic_t0_timer_device, "avr8gent0", "AVR8 generic timer 0")
DEFINE_DEVICE_TYPE(AVR8_MEGAAVR_CLASSIC_TIMER_0, avr8_megaavr_timer_0_device,  "avr8gent0", "AVR8 megaAVR Classic Timer 0")

///////////////////////////////////////////////////////////////
// BASE TIMER
///////////////////////////////////////////////////////////////

void avr8_base_timer_device::device_start()
{
	save_item(NAME(m_toie));
	save_item(NAME(m_tov));
    // TODO: save all the things

    install_wgm_callbacks();
}

void avr8_base_timer_device::device_reset()
{
    m_toie = false;
    m_tov = false;

    // m_timer_top[t] = 0;
    // m_timer_prescale[t] = 0xffff;
    // m_timer_prescale_count[t] = 0;

    m_ocr_a = 0;
    m_ocr_b = 0;
    m_ocr_c = 0;
    m_ocr_d = 0;
}


void avr8_base_timer_device::tick()
{
    m_prescale_count++;
    if (m_prescale_count > m_active_prescale_reload)
    {
        // TODO: call appropriate handler using current wgm value

        // TODO: fix prescale stuff
    }
}

void avr8_base_timer_device::toie_w(uint8_t data)
{
    m_toie = data != 0;
    if (m_tov_int_cb) m_tov_int_cb(m_toie != 0 && m_tov != 0);
}

void avr8_base_timer_device::tov_w(uint8_t data)
{
    if (data == 0) return;

    m_tov = false;
    if (m_tov_int_cb) m_tov_int_cb(0);
}

uint16_t avr8_base_timer_device::tcnt_internal_r()
{
    return m_tcnt & ((1 << get_num_bits()) - 1);
}

void avr8_base_timer_device::tcnt_internal_w(uint16_t data)
{
    m_tcnt = data & ((1 << get_num_bits()) - 1);

    // NOTE: Some timers will have special behavior when the
    // TCNT value is updated. This is just a setter.
}


void avr8_base_timer_device::advance_normal()
{
    if (will_count_overflow())
    {
        m_tov = true;
        if(m_tov_int_cb) m_tov_int_cb(1);
    }

    advance_count();
}

///////////////////////////////////////////////////////////////
// TIMER 0 COMMON
///////////////////////////////////////////////////////////////

void avr8_generic_t0_timer_device::install_wgm_callbacks()
{
    m_wgm_cb[GENERIC_TIMER0_WGM_NORMAL] = FUNC(avr8_generic_t0_timer_device::advance_normal);
    m_wgm_cb[GENERIC_TIMER0_WGM_FAST_PWM] = FUNC(avr8_generic_t0_timer_device::advance_fast_pwm);
}

void avr8_generic_t0_timer_device::advance_normal()
{
    if (is_compare_a())
    {
        update_ctc_oc0a();
    }

    if (is_compare_b())
    {
        update_ctc_oc0b();
    }

    avr8_base_timer_device::advance_normal();
}

void avr8_generic_t0_timer_device::advance_ctc()
{
    if (is_compare_a())
    {
        m_ocf_a = true;

        update_ctc_oc0a();
        
        if (m_ocf_a_int_cb) m_ocf_a_int_cb(1);
        reset_count();
    }
    else if (is_compare_b())
    {
        m_ocf_b = true;

        update_ctc_oc0b();

        if (m_ocf_b_int_cb) m_ocf_b_int_cb(1);
        reset_count();
    }
    else
    {
        advance_count();
    }
}

void avr8_generic_t0_timer_device::update_ctc_oc0a()
{
    switch (m_com_a)
    {
        case GENERIC_TIMER0_COM_TOGGLE:
            if (m_oc_a_toggle_w_cb) m_oc_a_toggle_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_CLEAR:
            if (m_oc_a_w_cb) m_oc_a_w_cb(0);
            break;
        
        case GENERIC_TIMER0_COM_SET:
            if (m_oc_a_w_cb) m_oc_a_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_NORMAL:
        default:
            // does nothing; OCxA pin disconnected
            break;
    }
}

void avr8_generic_t0_timer_device::update_ctc_oc0b()
{
    switch (m_com_b)
    {
        case GENERIC_TIMER0_COM_TOGGLE:
            if (m_oc_b_toggle_w_cb) m_oc_b_toggle_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_CLEAR:
            if (m_oc_b_w_cb) m_oc_b_w_cb(0);
            break;
        
        case GENERIC_TIMER0_COM_SET:
            if (m_oc_b_w_cb) m_oc_b_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_NORMAL:
        default:
            // does nothing; OCxB pin disconnected
            break;
    }
}

void avr8_generic_t0_timer_device::advance_fast_pwm()
{
    // TODO: the various OC0A/OC0B setting/clearing events
    //
    // for OC0A
    // GENERIC_TIMER0_COM_TOGGLE = if WGM0 bit 2 cleared, ignore; if set, toggle OC0A on match
    // GENERIC_TIMER0_COM_CLEAR = clear at compare, set at TOP
    // GENERIC_TIMER0_COM_SET = set at compare, clear at TOP
    // 
    // for 0COB
    // GENERIC_TIMER0_CTC_COM_TOGGLE = marked as reserved on 16u4/32u4, same on 328
    // GENERIC_TIMER0_CTC_COM_CLEAR = clear at compare, set at TOP
    // GENERIC_TIMER0_CTC_COM_SET = set at compare, clear at TOP

    if (will_compare_a())
    {
        // update OCxA
    }
    if (will_compare_b())
    {
        // update OCxB
    }

    if (will_count_overflow())
    {
        m_tov = true;
        if (m_tov_int_cb) m_tov_int_cb(1);
    }
    else if (next_count() == 0xff)
    {
        // update OCxx flags since timer is at TOP
    }

    advance_count();
}

