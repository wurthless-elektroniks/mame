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
    | ATMega 16,   | Generic | Generic | 8-bit   |  n/a    |  n/a    |  n/a    | 
    | 32           | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega       | Generic | Generic | 8-bit   |  n/a    |  n/a    |  n/a    | 
    | 88, 168, 328 | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega       | Generic | Generic |  n/a    | 16-bit  | 10-bit  |  n/a    |
    | 16U4, 32U4   | timer 0 | timer 1 |         |         | hispeed |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 640,  | Generic | 16-bit  | 8-bit   | 16-bit  | 16-bit  | 16-bit  |
    | 1280, 2560   | timer 0 | 3 ch.   | fakeRTC | 3 ch.   | 3 ch.   | 3 ch.   |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 324,  | Generic | Generic | 8-bit   |  n/a    |  n/a    |  n/a    |
    | 644          | timer 0 | timer 1 | fakeRTC |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATMega 1284, | Generic | Generic | 8-bit   | Generic |  n/a    |  n/a    | 
    | 2564         | timer 0 | timer 1 | fakeRTC | timer 1 |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny 15    | Generic |  n/a    |  n/a    |  n/a    |  n/a    |  n/a    |
    |              | timer 0 |         |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|
    | ATTiny 25,   | Generic | 8-bit   |  n/a    |  n/a    |  n/a    |  n/a    |
    | 45, 85       | timer 0 | hispeed |         |         |         |         |
    |--------------|---------|---------|---------|---------|---------|---------|

    Types used:
    - "Generic timer 0" is a common 8-bit timer across the classic AVR8s,
      slotted into the Timer 0 position.

    - "Generic timer 1" is a high-precision 16-bit timer with two or three
      comparator output channels and one comparator input channel.
    
    - The "fake RTC" asynchronous timer is another 8-bit timer. In synchronous
      mode its prescaler will be clocked by the I/O clock, but it can also be
      switched to an external clock source, which is usually a 32.768 kHz crystal
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
    in that register, and then writes to the lower halves of other 16-bit registers will
    reuse whatever you put there.

    There are obviously exceptions to this behavior to add a few more headaches,
    but those apply to the specific registers. In general, TCNTx will obey these
    rules exactly.

    Output compare modulator:
    -----------------------------------------------------------------

    - ATMega88, ATMega168, ATMega328: Not present. ATMega328PB has one.

    - ATMega640, ATMega1280, ATMega2560: inputs on OC1C/OC0A, output to OC1C/OC0A/PB7 pin.

    - ATMega16U4/ATMega32U4: inputs on OC1C/OC0A, output to OC1C/OC0A/PB7 pin.

****************************************************************************/

#include "emu.h"
#include "avr8timers_classic.h"

#define LOG_TIMER0          (1U << 0)
#define LOG_TIMER0_TICK     (1U << 1)
#define LOG_TIMER1          (1U << 2)
#define LOG_TIMER1_TICK     (1U << 3)

#define LOG_INT_ACKS (1 << 4)

#define VERBOSE             (LOG_TIMER1 | LOG_TIMER1_TICK)
//#define LOG_OUTPUT_FUNC     osd_printf_info
#include "logmacro.h"

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_0, avr8_generic_t0_timer_device, "cavr8gent0", "Classic AVR8 generic 8-bit timer 0");
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_0, avr8_megaavr_timer_0_device,  "cmegaavrt0", "Classic megaAVR Timer 0");

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_1, avr8_generic_t1_timer_device, "cavr8gent1", "Classic AVR8 generic 16-bit timer");

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_10BIT_PRESCALER, avr8_10bit_prescaler_device, "avr8presc", "Classic AVR8 10-bit prescaler");
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_OUTPUT_COMPARE_MODULATOR, avr8_output_compare_modulator_device, "avr8ocm", "Classic AVR8 Output Compare Modulator");

#if 0
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_TIMER_1, avr8_megaavr_timer_1_device,  "cmegaavrt0", "Classic megaAVR Timer 1")
DEFINE_DEVICE_TYPE(AVR8_CLASSIC_MEGAAVR_TIMER_1, avr8_megaavr_timer_1_device,  "cmegaavrt0", "Classic megaAVR Timer 1")

DEFINE_DEVICE_TYPE(AVR8_CLASSIC_GENERIC_FAKERTC_TIMER, avr8_megaavr_fakertc_timer_device,  "cmegaavrt0", "Classic megaAVR fake RTC timer")



#endif

///////////////////////////////////////////////////////////////
// BASE TIMER
///////////////////////////////////////////////////////////////

avr8_base_timer_device::avr8_base_timer_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock) :
    device_t(mconfig, type, tag, owner, clock),
    m_upstream_prescaler(*this, "syncprescaler"),
    m_tov_int_cb(*this),
    m_ocf_a_int_cb(*this),
    m_ocf_b_int_cb(*this),
    m_ocf_c_int_cb(*this),
    m_oc_a_en_w_cb(*this),
    m_oc_b_en_w_cb(*this),
    m_oc_c_en_w_cb(*this),
    m_oc_a_w_cb(*this),
    m_oc_b_w_cb(*this),
    m_oc_c_w_cb(*this),
    m_oc_a_toggle_w_cb(*this),
    m_oc_b_toggle_w_cb(*this),
    m_oc_c_toggle_w_cb(*this)
{}

void avr8_base_timer_device::device_start()
{
    save_item(NAME(m_tcnt));
    save_item(NAME(m_ocr_a));
    save_item(NAME(m_ocr_b));
    save_item(NAME(m_ocr_c));
    save_item(NAME(m_wgm));
    save_item(NAME(m_cs));
    save_item(NAME(m_com_a));
    save_item(NAME(m_com_b));
    save_item(NAME(m_com_c));
    save_item(NAME(m_toie));
    save_item(NAME(m_tov));
    save_item(NAME(m_ocie_a));
    save_item(NAME(m_ocf_a));
    save_item(NAME(m_ocie_b));
    save_item(NAME(m_ocf_b));
    save_item(NAME(m_ocie_c));
    save_item(NAME(m_ocf_c));
    save_item(NAME(m_prtim));
    save_item(NAME(m_t));
    save_item(NAME(m_t_last));

    for (int i = 0; i < 0x100; i++)
    {
        m_wgm_cbs[i] = avr8_base_timer_device::timer_func(&avr8_base_timer_device::advance_illegal_wgm_state, this);
    }

    install_wgm_callbacks();
}

void avr8_base_timer_device::device_reset()
{
    m_tcnt = 0;
    m_ocr_a = 0;
    m_ocr_b = 0;
    m_ocr_c = 0;
    m_wgm = 0;
    m_cs = 0;
    m_com_a = AVR8_TIMER_COM_NORMAL;
    m_com_b = AVR8_TIMER_COM_NORMAL;
    m_com_c = AVR8_TIMER_COM_NORMAL;
    m_toie = false;
    m_tov = false;
    m_ocie_a = false;
    m_ocf_a = false;
    m_ocie_b = false;
    m_ocf_b = false;
    m_ocie_c = false;
    m_ocf_c = false;
    m_prtim = false;
    m_t = false;
    m_t_last = false;
}

void avr8_base_timer_device::toie_w(uint8_t data)
{
    m_toie = data != 0;
    if (!m_tov_int_cb.isunset()) m_tov_int_cb(m_toie && m_tov);
}

void avr8_base_timer_device::ocie_a_w(uint8_t data)
{
    m_ocie_a = data != 0;
    if (!m_ocf_a_int_cb.isunset()) m_ocf_a_int_cb(m_ocie_a && m_ocf_a);
}

void avr8_base_timer_device::ocie_b_w(uint8_t data)
{
    m_ocie_b = data != 0;
    if (!m_ocf_b_int_cb.isunset()) m_ocf_b_int_cb(m_ocie_b && m_ocf_b);
}

void avr8_base_timer_device::ocie_c_w(uint8_t data)
{
    m_ocie_c = data != 0;
    if (!m_ocf_c_int_cb.isunset()) m_ocf_c_int_cb(m_ocie_c && m_ocf_c);
}

void avr8_base_timer_device::tov_w(uint8_t data)
{
    if (data == 0) return;

    LOGMASKED(LOG_INT_ACKS, "%s: TOV int acknowledged by program\n", machine().describe_context());
    clear_tov();
}

void avr8_base_timer_device::tov_ack_w(uint8_t _)
{
    LOGMASKED(LOG_INT_ACKS, "%s: TOV int acknowledged by CPU\n", machine().describe_context());
    m_tov = false;
}

void avr8_base_timer_device::ocf_a_ack_w(uint8_t _)
{
    LOGMASKED(LOG_INT_ACKS, "%s: OCF A int acknowledged by CPU\n", machine().describe_context());
    m_ocf_a = false;
}

void avr8_base_timer_device::ocf_b_ack_w(uint8_t _)
{
    LOGMASKED(LOG_INT_ACKS, "%s: OCF B int acknowledged by CPU\n", machine().describe_context());
    m_ocf_b = false;
}

void avr8_base_timer_device::ocf_c_ack_w(uint8_t _)
{
    LOGMASKED(LOG_INT_ACKS, "%s: OCF C int acknowledged by CPU\n", machine().describe_context());
    m_ocf_c = false;
}

void avr8_base_timer_device::ocf_a_w(uint8_t data)
{
    if (data == 0) return;

    clear_ocf_a();
}

void avr8_base_timer_device::ocf_b_w(uint8_t data)
{
    if (data == 0) return;

    clear_ocf_b();
}

void avr8_base_timer_device::ocf_c_w(uint8_t data)
{
    if (data == 0) return;

    clear_ocf_c();
}

void avr8_base_timer_device::raise_tov()
{
    m_tov = true;
    if (m_toie && !m_tov_int_cb.isunset()) m_tov_int_cb(1);
}

void avr8_base_timer_device::raise_ocf_a()
{
    m_ocf_a = true;
    if (m_ocie_a && !m_ocf_a_int_cb.isunset()) m_ocf_a_int_cb(1);
}

void avr8_base_timer_device::raise_ocf_b()
{
    m_ocf_b = true;
    if (m_ocie_b && !m_ocf_b_int_cb.isunset()) m_ocf_b_int_cb(1);
}

void avr8_base_timer_device::raise_ocf_c()
{
    m_ocf_c = true;
    if (m_ocie_c && !m_ocf_c_int_cb.isunset()) m_ocf_c_int_cb(1);
}

void avr8_base_timer_device::clear_tov()
{
    m_tov = false;
    if (m_toie && !m_tov_int_cb.isunset()) m_tov_int_cb(0);
}

void avr8_base_timer_device::clear_ocf_a()
{
    m_ocf_a = false;
    if (m_ocie_a && !m_ocf_a_int_cb.isunset()) m_ocf_a_int_cb(1);
}

void avr8_base_timer_device::clear_ocf_b()
{
    m_ocf_b = false;
    if (m_ocie_b && !m_ocf_b_int_cb.isunset()) m_ocf_b_int_cb(1);
}

void avr8_base_timer_device::clear_ocf_c()
{
    m_ocf_c = false;
    if (m_ocie_c && !m_ocf_c_int_cb.isunset()) m_ocf_c_int_cb(1);
}


void avr8_base_timer_device::com_a_w(uint8_t data)
{
    fatalerror("com_a_w unimplemented. Timer doesn't support it, or you found a bug.\n");
}
    
void avr8_base_timer_device::com_b_w(uint8_t data)
{
    fatalerror("com_b_w unimplemented. Timer doesn't support it, or you found a bug.\n");
}

void avr8_base_timer_device::com_c_w(uint8_t data)
{
    fatalerror("com_c_w unimplemented. Timer doesn't support it, or you found a bug.\n");
}

void avr8_base_timer_device::clk_io_w(uint8_t data)
{
    if (!m_clk_io && data != 0 && !is_async_mode())
    {
        tick();
    }
    m_clk_io = data != 0;
}

void avr8_base_timer_device::advance_illegal_wgm_state()
{
    logerror("%s: advance timer on illegal/unimplemented wgm state %02x\n", machine().describe_context(), m_wgm);
}

void avr8_base_timer_device::advance_normal()
{
    if (will_next_count_overflow())
    {
        raise_tov();
    }
    m_tcnt = next_count();
}

void avr8_base_timer_device::update_oc_a_nonpwm_generic()
{
    if (m_oc_a_w_cb.isunset() || m_oc_a_toggle_w_cb.isunset())
    {
        return;
    }

    switch (m_com_a)
    {
        case AVR8_TIMER_COM_TOGGLE:
            m_oc_a_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_CLEAR:
            m_oc_a_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_SET:
            m_oc_a_w_cb(1);
            break;

        case AVR8_TIMER_COM_NORMAL:
        default:
            // does nothing; OCxA pin disconnected
            break;
    }
}

void avr8_base_timer_device::update_oc_b_nonpwm_generic()
{
    if (m_oc_b_w_cb.isunset() || m_oc_b_toggle_w_cb.isunset())
    {
        return;
    }

    switch (m_com_b)
    {
        case AVR8_TIMER_COM_TOGGLE:
            m_oc_b_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_CLEAR:
            m_oc_b_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_SET:
            m_oc_b_w_cb(1);
            break;

        case AVR8_TIMER_COM_NORMAL:
        default:
            // does nothing; OCxA pin disconnected
            break;
    }
}

void avr8_base_timer_device::update_oc_c_nonpwm_generic()
{
    if (m_oc_c_w_cb.isunset() || m_oc_c_toggle_w_cb.isunset())
    {
        return;
    }

    switch (m_com_a)
    {
        case AVR8_TIMER_COM_TOGGLE:
            m_oc_c_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_CLEAR:
            m_oc_c_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_SET:
            m_oc_c_w_cb(1);
            break;

        case AVR8_TIMER_COM_NORMAL:
        default:
            // does nothing; OCxA pin disconnected
            break;
    }
}

///////////////////////////////////////////////////////////////
// COMMON 10-BIT PRESCALER
///////////////////////////////////////////////////////////////

avr8_10bit_prescaler_device::avr8_10bit_prescaler_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
    device_t(mconfig, AVR8_CLASSIC_10BIT_PRESCALER, tag, owner, clock)
{}

void avr8_10bit_prescaler_device::device_start()
{
    save_item(NAME(m_count));
    save_item(NAME(m_sync));
}

void avr8_10bit_prescaler_device::device_reset()
{
    m_count = 0;
    m_sync = false;
}

void avr8_10bit_prescaler_device::tick()
{
    // TODO: this is a guess; if the signals rise and stay risen
    // then they should not cause the timers downstream to advance
    int next_count = m_sync ? 0 : (m_count + 1) & 0x3ff;

    m_count = next_count;
}

void avr8_10bit_prescaler_device::sync_w(uint8_t data)
{
    if (data != 0) m_count = 0;

    // remember: GTCCR and others can keep this signal asserted
    m_sync = data != 0;
}

uint8_t avr8_10bit_prescaler_device::sync_r()
{
    return m_sync;
}

///////////////////////////////////////////////////////////////
// OUTPUT COMPARE MODULATOR
///////////////////////////////////////////////////////////////

avr8_output_compare_modulator_device::avr8_output_compare_modulator_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
    device_t(mconfig, AVR8_CLASSIC_OUTPUT_COMPARE_MODULATOR, tag, owner, clock),
    m_gpio_bit_w_wb(*this)
{ }

void avr8_output_compare_modulator_device::device_start()
{
    save_item(NAME(m_compare_out_a));
    save_item(NAME(m_compare_out_en_a));
    save_item(NAME(m_compare_out_b));
    save_item(NAME(m_compare_out_en_b));
    save_item(NAME(m_port_bit));
}

void avr8_output_compare_modulator_device::device_reset()
{
    m_compare_out_a = false;
    m_compare_out_en_a = false;
    m_compare_out_b = false;
    m_compare_out_en_b = false;
    m_port_bit = false;
}

void avr8_output_compare_modulator_device::update_outputs()
{
    // passthrough mode: PORTx bit goes directly to GPIO output
    // when both compare outputs disabled
    if (!m_compare_out_en_a && !m_compare_out_en_b)
    {
        m_gpio_bit_w_wb(m_port_bit);
        return;
    }
    
    // modulation mode: if both compare outputs enabled,
    // PORTx bit selects between AND modulation (=0) or
    // OR modulation (=1)
    if (m_compare_out_en_a && m_compare_out_en_b)
    {
        if (m_port_bit)
        {
            m_gpio_bit_w_wb(m_compare_out_a | m_compare_out_b);
        }
        else
        {
            m_gpio_bit_w_wb(m_compare_out_a & m_compare_out_b);
        }
        return;
    }

    if (m_compare_out_en_a) m_gpio_bit_w_wb(m_compare_out_a);
    if (m_compare_out_en_b) m_gpio_bit_w_wb(m_compare_out_b);
}

void avr8_output_compare_modulator_device::compare_out_a_w(uint8_t data)
{
    m_compare_out_a = data != 0;
    update_outputs();
}

void avr8_output_compare_modulator_device::compare_out_en_a(uint8_t data)
{
    m_compare_out_en_a = data != 0;
    update_outputs();
}

void avr8_output_compare_modulator_device::compare_out_b_w(uint8_t data)
{
    m_compare_out_b = data != 0;
    update_outputs();
}

void avr8_output_compare_modulator_device::compare_out_en_b(uint8_t data)
{
    m_compare_out_en_b = data != 0;
    update_outputs();
}

void avr8_output_compare_modulator_device::port_bit_w(uint8_t data)
{
    m_port_bit = data != 0;
    update_outputs();
}

///////////////////////////////////////////////////////////////
// TIMER 0 COMMON
///////////////////////////////////////////////////////////////

avr8_generic_t0_timer_device::avr8_generic_t0_timer_device(const machine_config &mconfig,
    const char *tag,
    device_t *owner,
    uint32_t clock,
    const device_type device_type = AVR8_CLASSIC_GENERIC_TIMER_0) :
    avr8_base_timer_device(mconfig, device_type, tag, owner, clock)
{ }

avr8_megaavr_timer_0_device::avr8_megaavr_timer_0_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
    avr8_generic_t0_timer_device(mconfig, tag, owner, clock, AVR8_CLASSIC_MEGAAVR_TIMER_0)
{ }

void avr8_generic_t0_timer_device::install_wgm_callbacks()
{
    m_wgm_cbs[GENERIC_TIMER0_WGM_NORMAL] = timer_func(&avr8_generic_t0_timer_device::advance_normal, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_CTC] = timer_func(&avr8_generic_t0_timer_device::advance_ctc, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_PWM_BOTTOM] = timer_func(&avr8_generic_t0_timer_device::advance_pwm, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_FAST_PWM] = timer_func(&avr8_generic_t0_timer_device::advance_fast_pwm, this);
    m_wgm_cbs[GENERIC_TIMER0_WGM_FAST_PWM_OCR0A] = timer_func(&avr8_generic_t0_timer_device::advance_fast_pwm_ocr0a, this);
}

void avr8_generic_t0_timer_device::tick()
{
    if (m_prtim) return;

    // Tx pin is sampled on every I/O clock
    // TODO: where to put this? check async timer docs
    bool t_rising = false, t_falling = false;
    if (m_t != m_t_last)
    {
        t_rising = !m_t_last && m_t;
        t_falling = m_t_last && !m_t;
        m_t = m_t_last;
    }


    if (m_cs == GENERIC_TIMER0_CS_DISABLED)
    {
        // LOGMASKED(LOG_TIMER0_TICK, "%s: timer is disabled (m_cs == GENERIC_TIMER0_CS_DISABLED), doing nothing.\n", machine().describe_context());
        return;
    }

    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV8 && !m_upstream_prescaler->clkdiv_r<8>()) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV64 && !m_upstream_prescaler->clkdiv_r<64>()) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV256 && !m_upstream_prescaler->clkdiv_r<256>()) return;
    if (m_cs == GENERIC_TIMER0_CS_PRESCALE_DIV1024 && !m_upstream_prescaler->clkdiv_r<1024>()) return;
    if (m_cs == GENERIC_TIMER0_CS_T0_FALL && !t_falling) return;
    if (m_cs == GENERIC_TIMER0_CS_T0_RISE && !t_rising) return;
    // GENERIC_TIMER0_CS_DIRECT = just fall through

    LOGMASKED(LOG_TIMER0_TICK, "%s: running wgm callback %d\n", machine().describe_context(), m_wgm);

    m_wgm_cbs[m_wgm]();
    m_no_compare_this_cycle = false; // because the current timer cycle has ended
}

void avr8_generic_t0_timer_device::advance_normal()
{
    if (is_compare_a())
    {
        raise_ocf_a();
        update_oc_a_nonpwm_generic();
    }

    if (is_compare_b())
    {
        raise_ocf_b();
        update_oc_b_nonpwm_generic();
    }

    avr8_base_timer_device::advance_normal();
}

void avr8_generic_t0_timer_device::advance_ctc()
{
    if (is_compare_a())
    {
        raise_ocf_a();
        update_oc_a_nonpwm_generic();
        m_tcnt = 0;
    }
    else if (is_compare_b())
    {
        raise_ocf_b();
        update_oc_b_nonpwm_generic();

        if (!m_ocf_b_int_cb.isunset()) m_ocf_b_int_cb(1);
        m_tcnt = 0;
    }
    else
    {
        m_tcnt = next_count();
    }
}

void avr8_generic_t0_timer_device::advance_pwm()
{
    uint8_t next_count_value = m_dual_slope_counting_down ? prev_count() : next_count();
    uint8_t count_limit = m_dual_slope_counting_down ? 0 : max_count();

    if (next_count_value == m_ocr_a)
    {
        if (m_ocr_a != max_count())
        {
            raise_ocf_a();
        }

        if (m_dual_slope_counting_down)
        {
            update_oc0a_pwm_on_downcount_compare();
        }
        else
        {
            update_oc0a_pwm_on_upcount_compare();
        }
    }

    if (next_count_value == m_ocr_b)
    {
        if (m_dual_slope_counting_down)
        {
            update_oc0b_pwm_on_downcount_compare();
        }
        else
        {
            update_oc0b_pwm_on_upcount_compare();
        }
    }

    if (next_count_value == count_limit)
    {
        if (m_dual_slope_counting_down)
        {
            raise_tov();
        }

        m_dual_slope_counting_down = !m_dual_slope_counting_down;
    }

    m_tcnt = next_count_value;
}

void avr8_generic_t0_timer_device::advance_pwm_ocr0a()
{
    uint8_t next_count_value = m_dual_slope_counting_down ? prev_count() : next_count();
    // uint8_t count_limit = m_dual_slope_counting_down ? 0 : m_ocr_a;

    if (next_count_value == m_ocr_a)
    {
        // TODO: confirm this; datasheet phrasing is a bit odd
        if (m_ocr_a != max_count())
        {
            raise_ocf_a();
        }

        if (m_dual_slope_counting_down)
        {
            update_oc0a_pwm_on_downcount_compare();
        }
        else
        {
            // timer hitting TOP on this tick
            update_oc0a_pwm_on_upcount_compare();
            m_dual_slope_counting_down = !m_dual_slope_counting_down;
        }
    }

    if (next_count_value == m_ocr_b)
    {
        // TODO: confirm this; datasheet phrasing is a bit odd
        if (m_ocr_b != max_count())
        {
            raise_ocf_b();
        }

        if (m_dual_slope_counting_down)
        {
            update_oc0b_pwm_on_downcount_compare();
        }
        else
        {
            update_oc0b_pwm_on_upcount_compare();
        }
    }

    if (m_dual_slope_counting_down && next_count_value == 0)
    {
        if (m_dual_slope_counting_down)
        {
            raise_tov();
        }

        m_dual_slope_counting_down = !m_dual_slope_counting_down;
    }

    m_tcnt = next_count_value;
}

void avr8_generic_t0_timer_device::advance_fast_pwm()
{
	if (will_compare_a())
	{
        update_oc0a_fast_pwm_on_compare();
	}
	
    if (will_compare_b())
	{
        update_oc0b_fast_pwm_on_compare();
	}

	if (will_next_count_overflow())
	{
		raise_tov();
        update_oc0a_fast_pwm_on_top();
        update_oc0b_fast_pwm_on_top();
	}
    
    m_tcnt = next_count();
}

void avr8_generic_t0_timer_device::advance_fast_pwm_ocr0a()
{
    if (will_compare_a())
    {
        // timer hitting TOP on this tick
        update_oc0a_fast_pwm_on_top();
        update_oc0b_fast_pwm_on_top();
    }
    else if (will_compare_b())
    {
        update_oc0b_fast_pwm_on_compare();
    }
    
    if (m_tcnt >= m_ocr_a)
    {
        raise_tov();
        m_tcnt = 0;
        return;
    }

    m_tcnt = next_count();
}

void avr8_generic_t0_timer_device::update_oc0a_fast_pwm_on_compare()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_TOGGLE:
            // this is how the datasheets describe this behavior, so I'm replicating that here.
            if (BIT(m_wgm, 2)) m_oc_a_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_CLEAR:
            m_oc_b_w_cb(0);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_b_w_cb(1);
            break;
        
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0b_fast_pwm_on_compare()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_TOGGLE:
            // marked as reserved on 16u4/32u4, same on 328
            logerror("%s: com_b set to toggle in fast pwm mode\n", machine().describe_context());
            break;

        case AVR8_TIMER_COM_CLEAR:
            m_oc_b_w_cb(0);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_b_w_cb(1);
            break;
        
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0a_fast_pwm_on_top()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_CLEAR:
            m_oc_a_w_cb(1);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_a_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0b_fast_pwm_on_top()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_CLEAR:
            m_oc_b_w_cb(1);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_b_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0a_pwm_on_upcount_compare()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_CLEAR:
            m_oc_a_w_cb(0);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_a_w_cb(1);
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
            break;

        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0a_pwm_on_downcount_compare()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_CLEAR:
            m_oc_a_w_cb(1);
            break;

        case AVR8_TIMER_COM_SET:
            m_oc_a_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
            break;
            
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0b_pwm_on_upcount_compare()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_CLEAR:
            break;

        case AVR8_TIMER_COM_SET:
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
            break;
            
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::update_oc0b_pwm_on_downcount_compare()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_CLEAR:
            break;

        case AVR8_TIMER_COM_SET:
            break;
        
        case AVR8_TIMER_COM_TOGGLE:
            break;
            
        case AVR8_TIMER_COM_NORMAL:
        default:
            return;
    };
}

void avr8_generic_t0_timer_device::com_a_w(uint8_t data)
{
    // COMx changes take effect immediately
    m_com_a = BIT(data, 0, 2);

    if (m_com_a == AVR8_TIMER_COM_NORMAL)
    {
        if (!m_oc_a_en_w_cb.isunset()) m_oc_a_en_w_cb(0);
        return;
    }

    // OC0A only changes upon compare; whatever I/O is latched
    // downstream right now will (usually) not change.
    if (!m_oc_a_en_w_cb.isunset()) m_oc_a_en_w_cb(1);
}
    
void avr8_generic_t0_timer_device::com_b_w(uint8_t data)
{
    // COMx changes take effect immediately
    m_com_b = BIT(data, 0, 2);

    if (m_com_b == AVR8_TIMER_COM_NORMAL)
    {
        if (!m_oc_b_en_w_cb.isunset()) m_oc_b_en_w_cb(0);
        return;
    }

    if (!m_oc_b_en_w_cb.isunset()) m_oc_b_en_w_cb(1);
    
    // TODO: see above
}

void avr8_generic_t0_timer_device::cs_w(uint8_t data)
{
    if (data == m_cs) return;

    // Switch timer source internally ONLY.
    // Timer 0 will always run in sync with the I/O clock, even
    // when switched to Tx pin triggers.
    m_cs = BIT(data, 0, 3);
}

void avr8_generic_t0_timer_device::wgm_w(uint8_t data)
{
    if (m_wgm == data) return;

    logerror("%s: switch wgm to %02x\n", machine().describe_context(), data);
    m_wgm = data;

    switch (m_wgm)
    {
        case GENERIC_TIMER0_WGM_NORMAL:
        case GENERIC_TIMER0_WGM_CTC:
            // not sure of any sideeffects (yet)
            break;

        case GENERIC_TIMER0_WGM_FAST_PWM:
        case GENERIC_TIMER0_WGM_FAST_PWM_OCR0A:
            // PWM modes double buffer OCRxx values to prevent misshapen output waveforms
            m_ocr_a_buf = m_ocr_a;
            m_ocr_b_buf = m_ocr_b;
            break;
        
        case GENERIC_TIMER0_WGM_PWM_BOTTOM:
        case GENERIC_TIMER0_WGM_PWM_OCR0A:
            m_dual_slope_counting_down = false; // guessed

            // PWM modes double buffer OCRxx values to prevent misshapen output waveforms
            m_ocr_a_buf = m_ocr_a;
            m_ocr_b_buf = m_ocr_b;
            break;

        default:
            logerror("%s: switched to illegal/reserved timer mode %02x\n", m_wgm);
            break;
    };
}

void avr8_generic_t0_timer_device::ocr_a_w(uint8_t data)
{
    switch (m_wgm)
    {
        case GENERIC_TIMER0_WGM_FAST_PWM:
        case GENERIC_TIMER0_WGM_FAST_PWM_OCR0A:
        case GENERIC_TIMER0_WGM_PWM_BOTTOM:
        case GENERIC_TIMER0_WGM_PWM_OCR0A:
            m_ocr_a_buf = data;
            break;
        default:
            m_ocr_a = data;
            break;
    };
}

uint8_t avr8_generic_t0_timer_device::ocr_a_r()
{
    switch (m_wgm)
    {
        case GENERIC_TIMER0_WGM_FAST_PWM:
        case GENERIC_TIMER0_WGM_FAST_PWM_OCR0A:
        case GENERIC_TIMER0_WGM_PWM_BOTTOM:
        case GENERIC_TIMER0_WGM_PWM_OCR0A:
            return m_ocr_a_buf;

        default:
            return m_ocr_a;
    }
}

void avr8_generic_t0_timer_device::ocr_b_w(uint8_t data)
{
    switch (m_wgm)
    {
        case GENERIC_TIMER0_WGM_FAST_PWM:
        case GENERIC_TIMER0_WGM_FAST_PWM_OCR0A:
        case GENERIC_TIMER0_WGM_PWM_BOTTOM:
        case GENERIC_TIMER0_WGM_PWM_OCR0A:
            m_ocr_b_buf = data;
            break;
        default:
            m_ocr_b = data;
            break;
    };
}

uint8_t avr8_generic_t0_timer_device::ocr_b_r()
{
    switch (m_wgm)
    {
        case GENERIC_TIMER0_WGM_FAST_PWM:
        case GENERIC_TIMER0_WGM_FAST_PWM_OCR0A:
        case GENERIC_TIMER0_WGM_PWM_BOTTOM:
        case GENERIC_TIMER0_WGM_PWM_OCR0A:
            return m_ocr_b_buf;

        default:
            return m_ocr_b;
    }
}

void avr8_generic_t0_timer_device::tcnt_w(uint8_t data)
{
    // CPU writes override any compare matches for the current timer clock cycle
    m_tcnt = data;
    m_no_compare_this_cycle = true;
}

void avr8_generic_t0_timer_device::force_oc0a()
{
    if (m_wgm == GENERIC_TIMER0_WGM_NORMAL || m_wgm == GENERIC_TIMER0_WGM_CTC)
    {
        update_oc_a_nonpwm_generic();
    }
}

void avr8_generic_t0_timer_device::force_oc0b()
{
    if (m_wgm == GENERIC_TIMER0_WGM_NORMAL || m_wgm == GENERIC_TIMER0_WGM_CTC)
    {
        update_oc_b_nonpwm_generic();
    }
}

void avr8_generic_t0_timer_device::tccr_b_w(uint8_t data)
{
    LOGMASKED(LOG_TIMER0, "tccr_b_w: %02x\n", data);

    if (BIT(data,7)) force_oc0a();
    if (BIT(data,6)) force_oc0b();
    cs_w(BIT(data,0,3));
    wgm_w(BIT(wgm_r(),0,2) | BIT(data,3) << 2);
}

uint8_t avr8_generic_t0_timer_device::tccr_b_r()
{
    // the FOCxx bits always read as zero; they are never stored.
    return cs_r() | (BIT(wgm_r(),2) << 3);
}

///////////////////////////////////////////////////////////////
// GENERIC 16-BIT TIMER 1
///////////////////////////////////////////////////////////////

avr8_generic_t1_timer_device::avr8_generic_t1_timer_device(const machine_config &mconfig,
    const char *tag,
    device_t *owner,
    uint32_t clock) :
    avr8_base_timer_device(mconfig, AVR8_CLASSIC_GENERIC_TIMER_1, tag, owner, clock)
{
    avr8_generic_t1_timer_device(mconfig, tag, owner, clock, AVR8_CLASSIC_GENERIC_TIMER_1);
}


avr8_generic_t1_timer_device::avr8_generic_t1_timer_device(const machine_config &mconfig,
    const char *tag,
    device_t *owner,
    uint32_t clock,
    const device_type device_type) :
    avr8_base_timer_device(mconfig, device_type, tag, owner, clock)
{ }

void avr8_generic_t1_timer_device::install_wgm_callbacks()
{
    m_wgm_cbs[GENERIC_TIMER1_WGM_NORMAL] = timer_func(&avr8_generic_t1_timer_device::advance_normal, this);
    m_wgm_cbs[GENERIC_TIMER1_WGM_PWM_8_BIT] = timer_func(&avr8_generic_t1_timer_device::advance_pwm_8bit, this);
    m_wgm_cbs[GENERIC_TIMER1_WGM_PWM_9_BIT] = timer_func(&avr8_generic_t1_timer_device::advance_pwm_9bit, this);
    m_wgm_cbs[GENERIC_TIMER1_WGM_PWM_10_BIT] = timer_func(&avr8_generic_t1_timer_device::advance_pwm_10bit, this);
    m_wgm_cbs[GENERIC_TIMER1_WGM_CTC] = timer_func(&avr8_generic_t1_timer_device::advance_ctc_ocr_a, this);
    // TODO: GENERIC_TIMER1_WGM_CTC
    // GENERIC_TIMER1_WGM_FAST_PWM_8_BIT  = 0b0101, // fast PWM, 8-bit: count 0x0000-0x00ff
    // GENERIC_TIMER1_WGM_FAST_PWM_9_BIT  = 0b0110, // fast PWM, 9-bit: count 0x0000-0x01ff
    // GENERIC_TIMER1_WGM_FAST_PWM_10_BIT = 0b0111, // fast PWM, 10-bit: count 0x0000-0x03ff
    // GENERIC_TIMER1_WGM_PWM_PFC_ICR1    = 0b1000, // phase/frequency correct; TOP = ICR1
    // GENERIC_TIMER1_WGM_PWM_PFC_OCR1A   = 0b1001, // phase/frequency correct; TOP = OCR1A
    // GENERIC_TIMER1_WGM_PWM_ICR1        = 0b1010, // phase-correct; TOP = ICR1
    // GENERIC_TIMER1_WGM_PWM_OCR1A       = 0b1011, // phase-correct; TOP = OCR1A
    // GENERIC_TIMER1_WGM_CTC_ICR1        = 0b1100, // CTC: count to ICR1
    // // 0b1101: reserved/illegal
    // GENERIC_TIMER1_WGM_FAST_PWM_ICR1   = 0b1110, // fast PWM; count to ICR1
    m_wgm_cbs[GENERIC_TIMER1_WGM_FAST_PWM_OCRA] = timer_func(&avr8_generic_t1_timer_device::advance_fast_pwm_ocr_a, this);
}

void avr8_generic_t1_timer_device::tick()
{
    // this routine very obviously copypasted from timer 0.
    // remember that the GENERIC_TIMER0_CS_xx and GENERIC_TIMER1_CS_xx enums
    // can't be commoned together.

    if (m_prtim) return;

    // Tx pin is sampled on every I/O clock, so check that first
    bool t_rising = false, t_falling = false;
    if (m_t != m_t_last)
    {
        t_rising = !m_t_last && m_t;
        t_falling = m_t_last && !m_t;
        m_t = m_t_last;
    }

    if (m_cs == GENERIC_TIMER1_CS_DISABLED)
    {
        // LOGMASKED(LOG_TIMER1_TICK, "%s: timer is disabled (m_cs == GENERIC_TIMER1_CS_DISABLED), doing nothing.\n", machine().describe_context());
        return;
    }

    if (m_cs == GENERIC_TIMER1_CS_PRESCALE_DIV8 && !m_upstream_prescaler->clkdiv_r<8>()) return;
    if (m_cs == GENERIC_TIMER1_CS_PRESCALE_DIV64 && !m_upstream_prescaler->clkdiv_r<64>()) return;
    if (m_cs == GENERIC_TIMER1_CS_PRESCALE_DIV256 && !m_upstream_prescaler->clkdiv_r<256>()) return;
    if (m_cs == GENERIC_TIMER1_CS_PRESCALE_DIV1024 && !m_upstream_prescaler->clkdiv_r<1024>()) return;
    if (m_cs == GENERIC_TIMER1_CS_T1_FALL && !t_falling) return;
    if (m_cs == GENERIC_TIMER1_CS_T1_RISE && !t_rising) return;
    // GENERIC_TIMER1_CS_DIRECT = just fall through

    // LOGMASKED(LOG_TIMER1_TICK, "%s: running wgm callback %d\n", machine().describe_context(), m_wgm);

    m_wgm_cbs[m_wgm]();
    m_no_compare_this_cycle = false; // because the current timer cycle has ended
}


void avr8_generic_t1_timer_device::advance_normal()
{
    // this is basically the same as timer 0's advance_normal(), but 16-bit.
    // underlying framework will handle the wraparound for us,
    // but we still have to do compares ourself.

    if (is_compare_a())
    {
        raise_ocf_a();
        update_oc_a_nonpwm_generic();
    }

    if (is_compare_b())
    {
        raise_ocf_b();
        update_oc_b_nonpwm_generic();
    }

    if (m_ocr_c_enabled && is_compare_c())
    {
        raise_ocf_c();
        update_oc_c_nonpwm_generic();
    }

    // fall through to normal case, which will catch the wraparound and set TOV1
    avr8_base_timer_device::advance_normal();
}

void avr8_generic_t1_timer_device::advance_pwm_common(uint16_t top)
{ 
    uint8_t next_count_value = m_dual_slope_counting_down ? prev_count() : next_count();
   
    if (next_count_value == m_ocr_a)
    {
        // TODO: confirm this; datasheet phrasing is a bit odd
        if (m_ocr_a != max_count())
        {
            raise_ocf_a();
        }

        // TODO: flags etc.
    }

    if (!m_dual_slope_counting_down && next_count_value == top)
    {
        m_dual_slope_counting_down = !m_dual_slope_counting_down;

        // OCRxA is reloaded at TOP for all phase-correct PWM modes.
        // fast PWM modes update at BOTTOM, phase/frequency correct PWM update at BOTTOM.
        m_ocr_a = m_ocr_a_buf;
    }

    if (next_count_value == m_ocr_b)
    {
        // TODO: confirm this; datasheet phrasing is a bit odd
        if (m_ocr_b != max_count())
        {
            raise_ocf_b();
        }

        if (m_dual_slope_counting_down)
            update_oc_b_pwm_on_downcount_compare();
        else
            update_oc_b_pwm_on_upcount_compare();
    }

    if (m_ocr_c_enabled && next_count_value == m_ocr_c)
    {
        // TODO: confirm this; datasheet phrasing is a bit odd
        if (m_ocr_c != max_count())
        {
            raise_ocf_c();
        }

        if (m_dual_slope_counting_down)
            update_oc_c_pwm_on_downcount_compare();
        else
            update_oc_c_pwm_on_upcount_compare();
    }

    if (m_dual_slope_counting_down && next_count_value == 0)
    {
        // BOTTOM hit
        raise_tov();
        m_dual_slope_counting_down = !m_dual_slope_counting_down;
    }

    m_tcnt = next_count_value;
}

void avr8_generic_t1_timer_device::advance_pwm_8bit()
{
    advance_pwm_common(0xff);
}

void avr8_generic_t1_timer_device::advance_pwm_9bit()
{
    advance_pwm_common(0x1ff);
}

void avr8_generic_t1_timer_device::advance_pwm_10bit()
{
    advance_pwm_common(0x3ff);
}

void avr8_generic_t1_timer_device::advance_ctc_ocr_a()
{
    bool will_increment = true;

    if (will_next_count_overflow())
    {
        // LOGMASKED(LOG_TIMER1_TICK, "%s: timer1 WGM1 CTC_OCR, TOP, new count %04x, OCR1A/B %04x/%04x, ICR1 %04x\n", machine().describe_context(), timer1_count, m_ocr1[AVR8_REG_A], m_ocr1[AVR8_REG_B], icr1);
        raise_tov();

        // will_increment left true because next_count() below will wrap the timer
    }

    if (is_compare_a())
    {
        // LOGMASKED(LOG_TIMER1_TICK, "%s: timer1 WGM1 CTC_OCR, OCR1A, new count %04x, OCR1A/B %04x/%04x, ICR1 %04x\n", machine().describe_context(), timer1_count, m_ocr1[AVR8_REG_A], m_ocr1[AVR8_REG_B], icr1);
        
        // explicit timer reset upon compare
        m_tcnt = 0;
        will_increment = false;

        raise_ocf_a();
        update_oc_a_nonpwm_generic();
    }
    else if (m_tcnt == 0)
    {
        // LOGMASKED(LOG_TIMER1_TICK, "%s: timer1 WGM1 CTC_OCR, BOTTOM, new count %04x, OCR1A/B %04x/%04x, ICR1 %04x\n", machine().describe_context(), timer1_count, m_ocr1[AVR8_REG_A], m_ocr1[AVR8_REG_B], icr1);
        clear_tov();
    }

    if (is_compare_b())
    {
        raise_ocf_b();
        update_oc_b_nonpwm_generic();
    }
    
    if (m_ocr_c_enabled && is_compare_c())
    {
        raise_ocf_c();
        update_oc_c_nonpwm_generic();
    }

    if (will_increment) m_tcnt = next_count();
}

void avr8_generic_t1_timer_device::advance_fast_pwm_ocr_a()
{
    if (m_tcnt == 0)
    {
        // OCR values reload from buffers at BOTTOM in this mode
        m_ocr_a = m_ocr_a_buf;
        m_ocr_b = m_ocr_b_buf;
        m_ocr_c = m_ocr_c_buf;
    }

    if (is_compare_a())
    {
        raise_tov();

        // TODO: fast pwm update output pins

        raise_ocf_a();

        m_tcnt = 0;
    
        // the timer's rolled over, so we have to stop here
        // or else the timer will advance!
        return;
    }
    else if (m_tcnt == 0)
    {
        clear_tov();

        // TODO: fast pwm update output pins
    }

    if (is_compare_b())
    {
        // LOGMASKED(LOG_TIMER1_TICK, "%s: timer1 WGM1 FAST_PWM_OCR, OCR1B, new count %04x, OCR1A/B %04x/%04x, ICR1 %04x\n", machine().describe_context(), timer1_count, m_ocr1[AVR8_REG_A], m_ocr1[AVR8_REG_B], icr1);
        
        // TODO: update OCxB output
        
        raise_ocf_b();
    }
    else if (m_tcnt == 0)
    {
        // TODO: update OCxB
    }

    // in all other cases, just keep ticking the timer upwards
    m_tcnt = next_count();
}

void avr8_generic_t1_timer_device::update_oc_a_pwm_on_downcount_compare()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_TOGGLE:
            // TODO: does this toggle in downcount?
            if ((m_wgm == GENERIC_TIMER1_WGM_PWM_PFC_OCR1A || m_wgm == GENERIC_TIMER1_WGM_PWM_OCR1A))
                if (!m_oc_a_toggle_w_cb.isunset()) m_oc_a_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_SET:
            if (!m_oc_a_w_cb.isunset()) m_oc_a_w_cb(1); // set counting down, clear counting up
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_a_w_cb.isunset()) m_oc_a_w_cb(0); // clear counting down, set counting up
            break;
        
        default:
            // OC1A disabled in normal mode
            break;
    }
}

void avr8_generic_t1_timer_device::update_oc_a_pwm_on_upcount_compare()
{
    switch (m_com_a)
    {
        case AVR8_TIMER_COM_TOGGLE:
            // per x8 series datasheet: toggles only in WGM modes 9 or 11.
            // disconnected in all other WGM modes.
            if ((m_wgm == GENERIC_TIMER1_WGM_PWM_PFC_OCR1A || m_wgm == GENERIC_TIMER1_WGM_PWM_OCR1A))
                if (!m_oc_a_toggle_w_cb.isunset()) m_oc_a_toggle_w_cb(1);
            break;

        case AVR8_TIMER_COM_SET:
            if (!m_oc_a_w_cb.isunset()) m_oc_a_w_cb(0); // set counting down, clear counting up
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_a_w_cb.isunset()) m_oc_a_w_cb(1); // clear counting down, set counting up
            break;
        
        default:
            // OC1A disabled in normal mode
            break;
    }
}

void avr8_generic_t1_timer_device::update_oc_b_pwm_on_downcount_compare()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_SET:
            if (!m_oc_b_w_cb.isunset()) m_oc_b_w_cb(1);
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_b_w_cb.isunset()) m_oc_b_w_cb(0);
            break;
        
        default:
            // OC1B disabled in toggle or normal modes
            break;
    }
}

void avr8_generic_t1_timer_device::update_oc_b_pwm_on_upcount_compare()
{
    switch (m_com_b)
    {
        case AVR8_TIMER_COM_SET:
            if (!m_oc_b_w_cb.isunset()) m_oc_b_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_b_w_cb.isunset()) m_oc_b_w_cb(1);
            break;
        
        default:
            // OC1B disabled in toggle or normal modes
            break;
    }
}

void avr8_generic_t1_timer_device::update_oc_c_pwm_on_downcount_compare()
{
    // per the atmegax0 (and 1281/2561) datasheet,
    // behavior here is the same as OCxB
    switch (m_com_c)
    {
        case AVR8_TIMER_COM_SET:
            if (!m_oc_c_w_cb.isunset()) m_oc_b_w_cb(1);
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_c_w_cb.isunset()) m_oc_b_w_cb(0);
            break;
        
        default:
            // OC1C disabled in toggle or normal modes
            break;
    }
}

void avr8_generic_t1_timer_device::update_oc_c_pwm_on_upcount_compare()
{
    // per the atmegax0 (and 1281/2561) datasheet,
    // behavior here is the same as OCxB
    switch (m_com_c)
    {
        case AVR8_TIMER_COM_SET:
            if (!m_oc_c_w_cb.isunset()) m_oc_b_w_cb(0);
            break;
        
        case AVR8_TIMER_COM_CLEAR:
            if (!m_oc_c_w_cb.isunset()) m_oc_b_w_cb(1);
            break;
        
        default:
            // OC1C disabled in toggle or normal modes
            break;
    }
}

void avr8_generic_t1_timer_device::com_a_w(uint8_t data)
{
    // COMx changes take effect immediately
    m_com_a = BIT(data, 0, 2);

    if (m_com_a == AVR8_TIMER_COM_NORMAL)
    {
        if (!m_oc_a_en_w_cb.isunset()) m_oc_a_en_w_cb(0);
        return;
    }

    if (m_com_a == AVR8_TIMER_COM_TOGGLE)
    {
        // In toggle mode, OC1A will output only when WGM = 9, 11, 14, 15;
        // at all other times it is disconnected
        if (m_wgm == GENERIC_TIMER1_WGM_PWM_PFC_OCR1A ||
            m_wgm == GENERIC_TIMER1_WGM_PWM_OCR1A ||
            m_wgm == GENERIC_TIMER1_WGM_FAST_PWM_ICR1 ||
            m_wgm == GENERIC_TIMER1_WGM_FAST_PWM_OCRA)
        {
            if (!m_oc_a_en_w_cb.isunset()) m_oc_a_en_w_cb(0);
            return;
        }
    }

    if (!m_oc_a_en_w_cb.isunset()) m_oc_a_en_w_cb(1);
}

void avr8_generic_t1_timer_device::com_b_w(uint8_t data)
{
    m_com_b = BIT(data, 0, 2);

    if (m_com_b == AVR8_TIMER_COM_NORMAL || m_com_b == AVR8_TIMER_COM_TOGGLE)
    {
        if (!m_oc_b_en_w_cb.isunset()) m_oc_b_en_w_cb(0);
        return;
    }

    if (!m_oc_b_en_w_cb.isunset()) m_oc_b_en_w_cb(1);
}

void avr8_generic_t1_timer_device::com_c_w(uint8_t data)
{
    m_com_c = BIT(data, 0, 2);

    if (m_com_c == AVR8_TIMER_COM_NORMAL || m_com_c == AVR8_TIMER_COM_TOGGLE)
    {
        if (!m_oc_c_en_w_cb.isunset()) m_oc_c_en_w_cb(0);
        return;
    }

    if (!m_oc_c_en_w_cb.isunset()) m_oc_c_en_w_cb(1);
}

void avr8_generic_t1_timer_device::wgm_w(uint8_t data)
{
    if (m_wgm == data) return;

    m_wgm = data;

    // enable/disable outputs if necessary
    com_a_w(m_com_a);
    com_b_w(m_com_b);
    if (m_ocr_c_enabled) com_c_w(m_com_c);

    switch (m_wgm)
    {
        case GENERIC_TIMER1_WGM_NORMAL:
        case GENERIC_TIMER1_WGM_CTC:
            // not sure of any sideeffects (yet)
            break;

        case GENERIC_TIMER1_WGM_PWM_8_BIT:
        case GENERIC_TIMER1_WGM_PWM_9_BIT:
        case GENERIC_TIMER1_WGM_PWM_10_BIT:
        case GENERIC_TIMER1_WGM_PWM_PFC_ICR1:
        case GENERIC_TIMER1_WGM_PWM_PFC_OCR1A:
            m_dual_slope_counting_down = false; // guessed

            // PWM modes double buffer OCRxx values to prevent misshapen output waveforms
            m_ocr_a_buf = m_ocr_a;
            m_ocr_b_buf = m_ocr_b;
            if (m_ocr_c_enabled) m_ocr_c_buf = m_ocr_c;
            break;

        case GENERIC_TIMER1_WGM_FAST_PWM_8_BIT:
        case GENERIC_TIMER1_WGM_FAST_PWM_9_BIT:
        case GENERIC_TIMER1_WGM_FAST_PWM_10_BIT:
        case GENERIC_TIMER1_WGM_FAST_PWM_ICR1:
        case GENERIC_TIMER1_WGM_FAST_PWM_OCRA:
            // PWM modes double buffer OCRxx values to prevent misshapen output waveforms
            m_ocr_a_buf = m_ocr_a;
            m_ocr_b_buf = m_ocr_b;
            if (m_ocr_c_enabled) m_ocr_c_buf = m_ocr_c;
            break;
        
        default:
            logerror("%s: switched to illegal/reserved timer mode %02x\n", machine().describe_context(), m_wgm);
            break;
    };
}

void avr8_generic_t1_timer_device::cs_w(uint8_t data)
{
    if (data == m_cs) return;

    // Like Timer 1, this always runs in sync with the I/O clock,
    // and m_cs should contain the same structure.
    m_cs = BIT(data, 0, 3);

    printf("m_cs: switching to mode %d\n", m_cs);
}

void avr8_generic_t1_timer_device::tcnt_h_w(uint8_t data)
{
    m_temp = data;
}

uint8_t avr8_generic_t1_timer_device::tcnt_h_r()
{
    return m_temp;
}

void avr8_generic_t1_timer_device::tcnt_l_w(uint8_t data)
{
    m_tcnt = (m_temp << 8) | data;
    LOGMASKED(LOG_TIMER1, "%s: tcnt changing to %04x\n", machine().describe_context(), m_tcnt);
}

uint8_t avr8_generic_t1_timer_device::tcnt_l_r()
{
    if (!machine().side_effects_disabled())
        m_temp = BIT(m_tcnt, 8, 8);
    return BIT(m_tcnt, 0, 8);
}

void avr8_generic_t1_timer_device::ocr_a_h_w(uint8_t data)
{
    m_temp = data;
}

void avr8_generic_t1_timer_device::ocr_a_l_w(uint8_t data)
{
    m_ocr_a_buf = (m_temp << 8) | data;

    LOGMASKED(LOG_TIMER1, "%s: OCR A switching to %04x\n", machine().describe_context(), m_ocr_a_buf);

    switch (m_wgm)
    {
        case GENERIC_TIMER1_WGM_NORMAL:
        case GENERIC_TIMER1_WGM_CTC:
        case GENERIC_TIMER1_WGM_CTC_ICR1:
            m_ocr_a = m_ocr_a_buf;
            break;
        
        default:
            break;
    }
}

uint8_t avr8_generic_t1_timer_device::ocr_a_h_r()
{
    return BIT(m_ocr_a_buf, 8, 8);
}

uint8_t avr8_generic_t1_timer_device::ocr_a_l_r()
{
    return BIT(m_ocr_a_buf, 0, 8);
}

void avr8_generic_t1_timer_device::ocr_b_h_w(uint8_t data)
{
    m_temp = data;
}

void avr8_generic_t1_timer_device::ocr_b_l_w(uint8_t data)
{
    m_ocr_b_buf = (m_temp << 8) | data;

    switch (m_wgm)
    {
        case GENERIC_TIMER1_WGM_NORMAL:
        case GENERIC_TIMER1_WGM_CTC:
        case GENERIC_TIMER1_WGM_CTC_ICR1:
            m_ocr_b = m_ocr_b_buf;
            break;
        
        default:
            break;
    }
}

uint8_t avr8_generic_t1_timer_device::ocr_b_h_r()
{
    return BIT(m_ocr_b_buf, 8, 8);
}

uint8_t avr8_generic_t1_timer_device::ocr_b_l_r()
{
    return BIT(m_ocr_b_buf, 0, 8);
}

void avr8_generic_t1_timer_device::ocr_c_h_w(uint8_t data)
{
    if (!m_ocr_c_enabled) return;
    m_temp = data;
}

void avr8_generic_t1_timer_device::ocr_c_l_w(uint8_t data)
{
    if (!m_ocr_c_enabled) return;
    m_ocr_c_buf = (m_temp << 8) | data;

    switch (m_wgm)
    {
        case GENERIC_TIMER1_WGM_NORMAL:
        case GENERIC_TIMER1_WGM_CTC:
        case GENERIC_TIMER1_WGM_CTC_ICR1:
            m_ocr_c = m_ocr_c_buf;
            break;
        
        default:
            break;
    }
}

uint8_t avr8_generic_t1_timer_device::ocr_c_h_r()
{
    return BIT(m_ocr_c_buf, 8, 8);
}

uint8_t avr8_generic_t1_timer_device::ocr_c_l_r()
{
    return BIT(m_ocr_c_buf, 0, 8);
}

void avr8_generic_t1_timer_device::tccr_b_w(uint8_t data)
{
    cs_w(BIT(data,0,3));
    wgm_w(BIT(wgm_r(), 0, 2) | (BIT(data, 3, 2) << 2));

    // TODO: bit 7 = ICNC1 (noise canceller)
    // TODO: bit 6 = ICES1
}

uint8_t avr8_generic_t1_timer_device::tccr_b_r()
{
    // bits 0,1,2 = CS
    // bits 3,4 = WGMx3,2
    return 
        BIT(cs_r(), 0, 3) |
        (BIT(wgm_r(), 2, 2) << 3);

    // TODO: bit 7 = ICNC1 (noise canceller)
    // TODO: bit 6 = ICES1
}

void avr8_generic_t1_timer_device::tccr_c_w(uint8_t data)
{
    // TODO: FOC1A, FOC1B, FOC1C
}

uint8_t avr8_generic_t1_timer_device::tccr_c_r()
{
    // all zero; FOCxA, FOCxB, FOCxC bits will flip to 0 when written
    return 0;
}

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
