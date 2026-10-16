// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, Sandro Ronco, Felipe Sanches, wurthless-elektroniks
/****************************************************************************

    Classic AVR8 series timers
    Device-ified and modularized by würthless elektroniks
    With much love to all the AVR8 core devs and maintainers

    This covers the various timers present on the "classic" megaAVR and tinyAVR lineups.
    The modern AVR8 chips use a completely different timer subsystem which will need
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
    | ATMega 16,   | Generic | Generic | 8-bit   |  n/a    |   n/a   |   n/a   | 
    | 32           | timer 0 | timer 1 | fakeRTC |         |         |         |
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
    | ATMega 324,  | Generic | Generic | 8-bit   |  n/a    |   n/a   |   n/a   |
    | 644          | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 1284, | Generic | Generic | 8-bit   | Generic |   n/a   |   n/a   | 
    | 2564         | timer 0 | timer 1 | fakeRTC | timer 1 |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny15     | Generic |  n/a    |  n/a    |  n/a    |  n/a    |  n/a    |
    |              | timer 0 |         |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny25,    | Generic | 8-bit   |  n/a    |  n/a    |  n/a    |  n/a    |
    | 45, 85       | timer 0 | hispeed |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|

    Types used:
    - "Generic timer 0" is a common 8-bit timer across the classic AVR8s,
      slotted into the Timer 0 position.

    - "Generic timer 1" is a high-precision 16-bit timer with two compare outputs,
      but it also has an input comparator.

    - The "fake RTC" asynchronous timer can have its time source set either to the CPU clock,
      but also to an external clock source, which is usually a 32.768 kHz crystal
      for timekeeping. It still requires software intervention to update and
      has no battery backing, so we can consider it to not be a "real" RTC.

    - The high-speed timers can be clocked by an on-chip PLL at many dozens
      of megahertz.

    16-bit register accesses:
    -----------------------------------------------------------------

    Paraphrasing the common "Accessing 16-bit Registers" from the ATmega datasheets...

    The 16-bit timer registers must be accessed atomically, so each 16-bit timer
    device contains an 8-bit temporary register to help with this. And to complicate
    our emulation, of course.

    The temporary register is updated when the lower 8 bits of a 16-bit register
    are read, or the upper 8 bits of a register are written. In both cases, the
    temporary register gets the upper 8 bits of the value to be read or written
    to the 16-bit register. Writes only take effect when the lower 8 bits are written.
    
    The datasheets therefore recommend that reads are done lower 8 bits followed by the
    upper 8 bits, and writes be performed by writing the upper 8 bits first,
    followed by the lower 8 bits. Interrupts also have to be disabled, as ISRs
    can overwrite the contents of the temporary register and buggy behavior will result.

    A bonus sideeffect of this temporary register is that you can latch the upper 8 bits
    in that register, and then writes to the other 16-bit registers will reuse whatever
    you put in the temporary register.

    There are obviously exceptions to this behavior to add a few more headaches,
    but those apply to the specific registers. In general, TCNTx will obey these
    rules exactly.

****************************************************************************/

#include "emu.h"
#include "avr8timers_classic.h"

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_0, avr8_generic_t0_timer_device, "cavr8gent0", "Classic AVR8 Generic Timer 0");
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_0, avr8_megaavr_timer_0_device,  "cmegaavrt0", "Classic megaAVR Timer 0");


DEFINE_DEVICE_TYPE(AVR8_CLASSIC_10BIT_PRESCALER, avr8_10bit_prescaler_device, "avr8presc", "Classic AVR8 10-bit prescaler");

#if 0
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_1, avr8_megaavr_timer_1_device,  "cmegaavrt0", "Classic megaAVR Timer 1")
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_1, avr8_megaavr_timer_1_device,  "cmegaavrt0", "Classic megaAVR Timer 1")

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_FAKERTC_TIMER, avr8_megaavr_fakertc_timer_device,  "cmegaavrt0", "Classic megaAVR fake RTC timer")



#endif

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

void avr8_10bit_prescaler_device::device_start()
{
    save_item(NAME(m_count));
    save_item(NAME(m_tsm));
    save_item(NAME(m_psrasy));
    save_item(NAME(m_psrsync));
}

void avr8_10bit_prescaler_device::device_reset()
{
    m_count = 0;

    // per GTCCR initial settings in datasheet
    m_tsm = false;
    m_psrasy = false;
    m_psrsync = false;
}


void avr8_10bit_prescaler_device::tick()
{
    if (m_tsm)
    {
        if (m_clk1024_w_cb) m_clk1024_w_cb(0); 
        if (m_clk256_w_cb) m_clk256_w_cb(0); 
        if (m_clk128_w_cb) m_clk128_w_cb(0); 
        if (m_clk64_w_cb) m_clk64_w_cb(0); 
        if (m_clk32_w_cb) m_clk32_w_cb(0); 
        if (m_clk8_w_cb) m_clk8_w_cb(0);
        return;
    }

    int next_count = (m_count + 1) & 0x3ff;

    if (m_clk1024_w_cb) m_clk1024_w_cb((next_count % 1024) == 0);
    if (m_clk256_w_cb) m_clk256_w_cb((next_count % 256) == 0);
    if (m_clk128_w_cb) m_clk128_w_cb((next_count % 128) == 0);
    if (m_clk64_w_cb) m_clk64_w_cb((next_count % 64) == 0);
    if (m_clk32_w_cb) m_clk32_w_cb((next_count % 32) == 0);
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


void avr8_generic_t0_timer_device::install_wgm_callbacks()
{
    m_wgm_cbs[GENERIC_TIMER0_WGM_NORMAL] = timer_func(&avr8_generic_t0_timer_device::advance_normal, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_FAST_PWM] = timer_func(&avr8_generic_t0_timer_device::advance_fast_pwm, this);
}

void avr8_generic_t0_timer_device::tick()
{
    if (m_prtim) return;

    // Tx pin is sampled on every tick, so check that first
    bool t_rising = false, t_falling = false;
    if (m_t != m_t_last)
    {
        t_rising = !m_t_last && m_t;
        t_falling = m_t_last && !m_t;
        m_t = m_t_last;
    }

    if (m_cs == GENERIC_TIMER0_CS_DISABLED) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV8 && !m_div8) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV64 && !m_div64) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV256 && !m_div256) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV1024 && !m_div1024) return;
    if (m_cs == GENERIC_TIMER0_CS_T0_FALL && !t_falling) return;
    if (m_cs == GENERIC_TIMER0_CS_T0_RISE && !t_rising) return;
    // GENERIC_TIMER0_CS_DIRECT = just fall through

    m_wgm_cbs[m_wgm]();
}


void avr8_generic_t0_timer_device::advance_normal()
{
    if (is_compare_a())
    {
        update_oc0a_normal_or_ctc();
    }

    if (is_compare_b())
    {
        update_oc0b_normal_or_ctc();
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

    // Switch timer source internally ONLY.
    // Timer 0 will always run in sync with the I/O clock, even
    // when switched to Tx pin triggers.
    m_cs = BIT(data, 0, 3);
}

///////////////////////////////////////////////////////////////
// GENERIC 16-BIT TIMER 1
///////////////////////////////////////////////////////////////

#if 0

void avr8_generic_t1_timer_device::device_add_mconfig(machine_config &config)
{
    avr8_base_timer_device::device_add_mconfig(config);

}

#endif

///////////////////////////////////////////////////////////////
// FAKE RTC TIMER 2 (AND TIMER SOURCE CONTROLLER)
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
