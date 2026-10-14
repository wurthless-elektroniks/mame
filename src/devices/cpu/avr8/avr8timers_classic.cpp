// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, Sandro Ronco, Felipe Sanches, wurthless-elektroniks
/****************************************************************************

    Classic AVR8 series timers
    Device-ified and modularized by würthless elektroniks
    With much love to all the avr8 core devs and maintainers

    This covers the various timers present on the "classic" megaAVR and tinyAVR lineups.
    The newer AVR8 chips use a completely different timer subsystem which will need
    to go into a separate file, if we ever get to the point where we need to emulate the
    newer chips.

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

    Brief intro to the classic AVR8 timer subsytem:
    -----------------------------------------------------------------

    Typically, the classic AVR8s have the following kinds of timers: 8-bit low-precision,
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

    - Generic timer 1 is a high-precision 16-bit timer with two compare outputs.

    - The "fake RTC" timer can have its time source set either to the CPU clock,
      but also to an external crystal, which is usually a 32.768 kHz crystal
      for timekeeping.

    - The high-speed timers can be clocked by an on-chip PLL at many dozens
      of megahertz.

****************************************************************************/

#include "emu.h"
#include "avr8timers_classic.h"

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_0, avr8_generic_t0_timer_device, "cavr8gent0", "Classic AVR8 Generic Timer 0")
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_0, avr8_megaavr_timer_0_device,  "cmegaavrt0", "Classic megaAVR Timer 0")

///////////////////////////////////////////////////////////////
// BASE TIMER
///////////////////////////////////////////////////////////////

void avr8_base_timer_device::device_start()
{
	save_item(NAME(m_toie));
	save_item(NAME(m_tov));
    // TODO: save all the things

    for (int i = 0; i < 0x100; i++)
    {
        m_wgm_cbs[i] = timer_func(&avr8_base_timer_device::advance_illegal_wgm_state, this);
    }

    install_wgm_callbacks();
}

void avr8_base_timer_device::device_reset()
{
    m_prescale_count = 0xffff;
    m_tcnt = 0;
    m_active_prescale_reload = 0;
    m_prescale_reload = 0;
    
    m_ocr_a = 0;
    m_ocr_b = 0;
    m_ocr_c = 0;
    m_ocr_d = 0;

    m_wgm = 0;

    m_com_a = 0;
    m_com_b = 0;
    m_com_c = 0;
    m_com_d = 0;

    m_toie = false;
    m_tov = false;
    m_ocie_a = false;
    m_ocf_a = false;
    m_ocie_b = false;
    m_ocf_b = false;
    m_ocie_c = false;
    m_ocf_c = false;
    m_ocie_d = false;
    m_ocf_d = false;

    m_prtim = false;
}


void avr8_base_timer_device::tick()
{
    if (!should_tick()) return;

    m_wgm_cbs[m_wgm]();
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

void avr8_base_timer_device::advance_illegal_wgm_state()
{
    logerror("%s: advance timer on illegal wgm state %02x\n", machine().describe_context(), m_wgm);
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
// COMMON 10-BIT PRESCALER
///////////////////////////////////////////////////////////////

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_10BIT_PRESCALER_DEVICE, avr8_10bit_prescaler_device, "avr10bpscl", "Classic AVR8 10-bit Prescaler")

void avr8_10bit_prescaler_device::tick()
{
    if (m_tsm) return;

    int next_count = (m_count + 1) & 0x3ff;

    if (m_clk1024_w_cb) m_clk1024_w_cb((next_count % 1024) == 0); 
    if (m_clk256_w_cb) m_clk256_w_cb((next_count % 256) == 0); 
    if (m_clk64_w_cb) m_clk64_w_cb((next_count % 64) == 0); 
    if (m_clk8_w_cb) m_clk8_w_cb((next_count % 8) == 0);

    m_count = next_count;
}

void avr8_10bit_prescaler_device::gtccr_w(uint8_t data)
{
    if (BIT(data, 7) && !m_tsm)
    {
        // entering TSM mode; stop counting
        m_tsm = true;
    }
    else if (BIT(data, 7) == 0 && m_tsm)
    {
        // exiting TSM mode
        m_tsm = false;
    }

    if (BIT(data, 0) && !m_psrsync)
    {
        // PSRSYNC: reset shared count to 0
        m_count = 0;
        m_psrsync = true;
    }

    if (BIT(data, 1) && !m_psrasy)
    {
        // PSRASY: signal reset to timer 2?
        m_psrasy = true;
        if (m_psrasy_w_cb) m_psrasy_w_cb(1);
    }

    if (!m_tsm)
    {
        m_psrasy = false;
        if (m_psrasy_w_cb) m_psrasy_w_cb(0);

        m_psrsync = false;
    }
}

///////////////////////////////////////////////////////////////
// TIMER 0 COMMON
///////////////////////////////////////////////////////////////

bool avr8_generic_t0_timer_device::should_tick()
{
    return !m_prtim;
}

void avr8_generic_t0_timer_device::install_wgm_callbacks()
{
    m_wgm_cbs[GENERIC_TIMER0_WGM_NORMAL] = timer_func(&avr8_generic_t0_timer_device::advance_normal, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_FAST_PWM] = timer_func(&avr8_generic_t0_timer_device::advance_fast_pwm, this);
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

void avr8_generic_t0_timer_device::update_oc0a_normal_or_ctc()
{
    if (m_oc_a_w_cb == NULL || m_oc_a_toggle_w_cb == NULL)
    {
        logerror("update_ctc_oc0a() called when OC0A callbacks are not set\n");
        return;
    }

    switch (m_com_a)
    {
        case GENERIC_TIMER0_COM_TOGGLE:
            m_oc_a_toggle_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_CLEAR:
            m_oc_a_w_cb(0);
            break;
        
        case GENERIC_TIMER0_COM_SET:
            m_oc_a_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_NORMAL:
        default:
            // does nothing; OCxA pin disconnected
            break;
    }
}

void avr8_generic_t0_timer_device::update_oc0b_normal_or_ctc()
{
    if (m_oc_b_w_cb == NULL || m_oc_b_toggle_w_cb == NULL)
    {
        logerror("update_ctc_oc0b() called when OC0B callbacks are not set\n");
        return;
    }

    switch (m_com_b)
    {
        case GENERIC_TIMER0_COM_TOGGLE:
            m_oc_b_toggle_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_CLEAR:
            m_oc_b_w_cb(0);
            break;
        
        case GENERIC_TIMER0_COM_SET:
            m_oc_b_w_cb(1);
            break;

        case GENERIC_TIMER0_COM_NORMAL:
        default:
            // does nothing; OCxB pin disconnected
            break;
    }
}

void avr8_generic_t0_timer_device::update_oc0a_pwm()
{

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


void avr8_generic_t0_timer_device::cs_w(uint8_t data)
{
    if (data == m_cs) return;

    m_cs = BIT(data, 0, 3);

    // TODO: build this table as a const somewhere, while enforcing the indices correctly
    uint8_t cs_to_clocksource_lut[0b1000];
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_DISABLED] = AVR8_TIMER_SOURCE_DISABLED;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_DIRECT] = AVR8_TIMER_SOURCE_FCPU;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_PRESCALE_DIV8] = AVR8_TIMER_SOURCE_PRESCALER_DIV8;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_PRESCALE_DIV64] = AVR8_TIMER_SOURCE_PRESCALER_DIV64;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_PRESCALE_DIV256] = AVR8_TIMER_SOURCE_PRESCALER_DIV256;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_PRESCALE_DIV1024] = AVR8_TIMER_SOURCE_PRESCALER_DIV1024;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_T0_FALL] = AVR8_TIMER_TX_FALLING_EDGE;
    cs_to_clocksource_lut[GENERIC_TIMER0_CS_T0_RISE] = AVR8_TIMER_TX_RISING_EDGE;

    if (m_timer_source_switch_cb)
    {
        m_timer_source_switch_cb(cs_to_clocksource_lut[m_cs]);
    }
    else
    {
        logerror("%s: attempt to switch timersource with no source switch cb installed (m_cs: %02x)\n",
                 machine().describe_context(),
                 m_cs);
    }
}

///////////////////////////////////////////////////////////////
// GENERIC 16-BIT TIMER 1
///////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////
// FAKE RTC TIMER 2
///////////////////////////////////////////////////////////////

#if 0

void avr8_device<NumTimers>::timer2_tick_default()
{
	LOGMASKED(LOG_TIMER2, "%s: WGM02_PWM_PC: Unimplemented timer#2 waveform generation mode %d\n", machine().describe_context(), WGM2);
	m_r[TCNT2]++;
	m_timer_prescale_count[2] -= m_timer_prescale[2];
}

template <int NumTimers>
void avr8_device<NumTimers>::timer2_tick_norm()
{
	LOGMASKED(LOG_TIMER2, "%s: timer2_tick_norm; WGM02_NORMAL\n", machine().describe_context());
	if (m_r[TCNT2] == 0xff)
	{
		set_interrupt(INTIDX_TOV2);
	}
	else
	{
		clear_interrupt(INTIDX_TOV2);
	}
	m_r[TCNT2]++;
	m_timer_prescale_count[2] -= m_timer_prescale[2];
}

template <int NumTimers>
void avr8_device<NumTimers>::timer2_tick_fast_pwm()
{
	if (m_r[TCNT2] >= m_r[OCR2A])
	{
		if (m_r[TCNT2] >= 0xff)
		{
			// Turn on
			m_r[PORTD] |= 1 << 7;
			m_gpio_out_cb[GPIOD](m_r[PORTD]);
			m_r[TCNT2] = 0;
			m_ocr2_not_reached_yet = true;
		}
		else
		{
			if (m_ocr2_not_reached_yet)
			{
				// Turn off
				m_r[PORTD] &= ~(1 << 7);
				m_gpio_out_cb[GPIOD](m_r[PORTD]);
				m_ocr2_not_reached_yet = false;
			}
		}
	}

	m_r[TCNT2]++;
	m_timer_prescale_count[2] -= m_timer_prescale[2];
}

template <int NumTimers>
void avr8_device<NumTimers>::timer2_tick_fast_pwm_cmp()
{
	const uint8_t ocf2[2] = { (1 << TIFR2_OCF2A_SHIFT), (1 << TIFR2_OCF2B_SHIFT) };
	uint16_t count = m_r[TCNT2];

	int32_t increment = 1;

	if (count == m_r[OCR2A])
	{
		m_r[TIFR2] |= TIFR2_TOV2_MASK;
		count = 0;
		increment = 0;
		m_r[TIFR2] |= ocf2[AVR8_REG_A];
	}
	else if (count == 0)
	{
		m_r[TIFR2] &= ~TIFR2_TOV2_MASK;
		clear_interrupt(INTIDX_TOV2);
	}

	if (count == m_r[OCR2B])
	{
		m_r[TIFR2] |= ocf2[AVR8_REG_B];
	}

	count += increment;

	update_interrupt(INTIDX_TOV2);
	m_r[TCNT2] = count;
	m_timer_prescale_count[2] -= m_timer_prescale[2];
}

template <int NumTimers>
void avr8_device<NumTimers>::timer2_force_output_compare(int reg)
{
	LOGMASKED(LOG_TIMER2 | LOG_UNKNOWN, "%s: force_output_compare: TODO; should be forcing OC2%c\n", machine().describe_context(), 'A' + reg);
}

template <int NumTimers>
void avr8_device<NumTimers>::update_ocr2(uint8_t newval, uint8_t reg)
{
	m_r[(reg == AVR8_REG_A) ? OCR2A : OCR2B] = newval;

	// Nothing needs to be done? All handled in timer callback
}

#endif
