// license:BSD-3-Clause
// copyright-holders: R. Belmont
/*
    OPTi 82C861 "FireLink" USB 1.1 OHCI controller
    Skeleton by R. Belmont

    Implemented: frame timing (HcFmInterval, HcFmRemaining, HcFmNumber, the
    HCCA frame number), the interrupt registers, software reset, and root hub
    ports with nothing connected.

    TODO:
    - Schedule processing: the endpoint descriptor lists are never walked, so
      no transfers complete and WritebackDoneHead never sets
    - Devices on the root hub ports
*/

#include "emu.h"
#include "opti82c861.h"

#include <algorithm>
#include <iterator>

#define LOG_REGISTERS   (1U << 1)

#define VERBOSE (LOG_GENERAL)
#include "logmacro.h"

#define LOGREGS(...) LOGMASKED(LOG_REGISTERS, __VA_ARGS__)

namespace {

// HcInterruptStatus/Enable/Disable
constexpr u32 INT_SO   = 1U << 0;  // scheduling overrun
constexpr u32 INT_WDH  = 1U << 1;  // writeback done head
constexpr u32 INT_SF   = 1U << 2;  // start of frame
constexpr u32 INT_RD   = 1U << 3;  // resume detected
constexpr u32 INT_UE   = 1U << 4;  // unrecoverable error
constexpr u32 INT_FNO  = 1U << 5;  // frame number overflow
constexpr u32 INT_RHSC = 1U << 6;  // root hub status change
constexpr u32 INT_OC   = 1U << 30; // ownership change
constexpr u32 INT_MIE  = 1U << 31; // master interrupt enable
constexpr u32 INT_ALL = INT_SO | INT_WDH | INT_SF | INT_RD | INT_UE | INT_FNO | INT_RHSC | INT_OC;

// HcRhPortStatus
constexpr u32 PORT_PPS = 1U << 8;   // port power status
constexpr u32 PORT_CSC = 1U << 16;  // connect status change
constexpr u32 PORT_CHANGE = 0x001f'0000;

constexpr u32 BIT_CLOCK = 12'000'000;

} // anonymous namespace

DEFINE_DEVICE_TYPE(OPTI_82C861, opti_82c861_device, "opti82c861", "OPTi 82C861 \"FireLink\" USB OHCI controller")

opti_82c861_device::opti_82c861_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: pci_card_device(mconfig, type, tag, owner, clock)
{
	// no interrupt unless the host configures one or the device sits in a slot
	set_irq_map(0xff);
}

opti_82c861_device::opti_82c861_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: opti_82c861_device(mconfig, OPTI_82C861, tag, owner, clock)
{
}

void opti_82c861_device::mem_map(address_map& map)
{
	// HcRevision: OpenHCI v1.0 with legacy support
	map(0x000, 0x003).lr32(NAME([]() { return 0x00000110; }));
	map(0x004, 0x007).lr32(NAME([this] () -> u32 { return m_HcControl; })).w(FUNC(opti_82c861_device::control_w));
	map(0x008, 0x00b).lr32(NAME([this] () -> u32 { return m_HcCommandStatus; })).w(FUNC(opti_82c861_device::command_status_w));
	map(0x00c, 0x00f).lr32(NAME([this] () -> u32 { return m_HcInterruptStatus; })).w(FUNC(opti_82c861_device::interrupt_status_w));
	map(0x010, 0x013).lr32(NAME([this] () -> u32 { return m_HcInterruptEnable; })).w(FUNC(opti_82c861_device::interrupt_enable_w));
	map(0x014, 0x017).lr32(NAME([this] () -> u32 { return m_HcInterruptEnable; })).w(FUNC(opti_82c861_device::interrupt_disable_w));
	map(0x018, 0x01b).lrw32(
		NAME([this] () -> u32 { return m_HcHCCA; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcHCCA); m_HcHCCA &= 0xffff'ff00; }));
	map(0x01c, 0x01f).lr32(NAME([] () -> u32 { return 0; })); // HcPeriodCurrentED
	map(0x020, 0x023).lrw32(
		NAME([this] () -> u32 { return m_HcControlHeadED; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcControlHeadED); m_HcControlHeadED &= 0xffff'fff0; }));
	map(0x024, 0x027).lrw32(
		NAME([this] () -> u32 { return m_HcControlCurrentED; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcControlCurrentED); m_HcControlCurrentED &= 0xffff'fff0; }));
	map(0x028, 0x02b).lrw32(
		NAME([this] () -> u32 { return m_HcBulkHeadED; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcBulkHeadED); m_HcBulkHeadED &= 0xffff'fff0; }));
	map(0x02c, 0x02f).lrw32(
		NAME([this] () -> u32 { return m_HcBulkCurrentED; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcBulkCurrentED); m_HcBulkCurrentED &= 0xffff'fff0; }));
	map(0x030, 0x033).lr32(NAME([] () -> u32 { return 0; })); // HcDoneHead
	map(0x034, 0x037).lrw32(
		NAME([this] () -> u32 { return m_HcFmInterval; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcFmInterval); m_HcFmInterval &= 0xffff'3fff; }));
	map(0x038, 0x03b).r(FUNC(opti_82c861_device::fm_remaining_r));
	map(0x03c, 0x03f).lr32(NAME([this] () -> u32 { return u32(m_HcFmNumber); }));
	map(0x040, 0x043).lrw32(
		NAME([this] () -> u32 { return m_HcPeriodicStart; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcPeriodicStart); m_HcPeriodicStart &= 0x3fff; }));
	map(0x044, 0x047).lrw32(
		NAME([this] () -> u32 { return m_HcLSThreshold; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcLSThreshold); m_HcLSThreshold &= 0x0fff; }));
	map(0x048, 0x04b).lrw32(
		NAME([this] () -> u32 {
			// 2 downstream ports (always?)
			return (m_HcRhDescriptorA & 0xff001b00) | NUM_PORTS;
		}),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) {
			COMBINE_DATA(&m_HcRhDescriptorA);
			if (ACCESSING_BITS_24_31)
				LOG("HcRhDescriptorA: set Power-On to Power-Good Time %d msec\n", (m_HcRhDescriptorA >> 24) * 2);
			if (ACCESSING_BITS_8_15)
				LOG("HcRhDescriptorA: set status %02x\n", (m_HcRhDescriptorA & 0x1b00) >> 8);
		})
	);
	map(0x04c, 0x04f).lrw32(
		NAME([this] () -> u32 { return m_HcRhDescriptorB; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_HcRhDescriptorB); }));
	// HcRhStatus: no over-current, nothing to report
	map(0x050, 0x053).lr32(NAME([] () -> u32 { return 0; })).lw32(
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) { LOGREGS("%s: HcRhStatus = %08x\n", machine().describe_context(), data); }));
	map(0x054, 0x057).lr32(NAME([this] () -> u32 { return m_HcRhPortStatus[0]; })).w(FUNC(opti_82c861_device::port_status_w<0>));
	map(0x058, 0x05b).lr32(NAME([this] () -> u32 { return m_HcRhPortStatus[1]; })).w(FUNC(opti_82c861_device::port_status_w<1>));
}

void opti_82c861_device::config_map(address_map &map)
{
	pci_card_device::config_map(map);
//  map(0x4e, 0x4e) i2c Control Register
//  map(0x50, 0x50) PCI Host Feature Control Register
//  map(0x51, 0x51) Interrupt Assignment Register
//  map(0x52, 0x52) Strap Option Enable
//  map(0x54, 0x57) IRQ Driveback Address Register
//  map(0x6c, 0x6f) Test Mode Enable Register
}

void opti_82c861_device::device_start()
{
	pci_card_device::device_start();
	set_ids(0x1045c861, 0x10, 0x0c0310, 0);
	revision = 0x10;
	add_map(0x1000, M_MEM, FUNC(opti_82c861_device::mem_map));  // 4KiB memory map

	command = 0;
	// fast back-to-back, medium DEVSEL#
	status = 0x0280;
	intr_pin = 1;
	intr_line = 0;

	m_frame_timer = timer_alloc(FUNC(opti_82c861_device::frame_tick), this);

	save_item(NAME(m_HcRhDescriptorA));
	save_item(NAME(m_HcControl));
	save_item(NAME(m_HcCommandStatus));
	save_item(NAME(m_HcInterruptStatus));
	save_item(NAME(m_HcInterruptEnable));
	save_item(NAME(m_HcHCCA));
	save_item(NAME(m_HcControlHeadED));
	save_item(NAME(m_HcControlCurrentED));
	save_item(NAME(m_HcBulkHeadED));
	save_item(NAME(m_HcBulkCurrentED));
	save_item(NAME(m_HcFmInterval));
	save_item(NAME(m_HcFmNumber));
	save_item(NAME(m_frame_remaining_toggle));
	save_item(NAME(m_HcPeriodicStart));
	save_item(NAME(m_HcLSThreshold));
	save_item(NAME(m_HcRhDescriptorB));
	save_item(NAME(m_HcRhPortStatus));
}

void opti_82c861_device::device_reset()
{
	pci_card_device::device_reset();
	m_HcRhDescriptorA = 0x01000000;
	m_HcRhDescriptorB = 0;
	std::fill(std::begin(m_HcRhPortStatus), std::end(m_HcRhPortStatus), 0);
	reset_registers(true);
}

// A hardware reset leaves the controller in UsbReset; a software reset (HcCommandStatus.HCR) in
// UsbSuspend.  Neither touches the root hub.
void opti_82c861_device::reset_registers(bool hardware)
{
	m_HcControl = hardware ? 0x0000'0000 : 0x0000'00c0;
	m_HcCommandStatus = 0;
	m_HcInterruptStatus = 0;
	m_HcInterruptEnable = 0;
	m_HcHCCA = 0;
	m_HcControlHeadED = 0;
	m_HcControlCurrentED = 0;
	m_HcBulkHeadED = 0;
	m_HcBulkCurrentED = 0;
	m_HcFmInterval = 0x0000'2edf;
	m_HcFmNumber = 0;
	m_frame_remaining_toggle = false;
	m_HcPeriodicStart = 0;
	m_HcLSThreshold = 0x0628;
	m_frame_timer->adjust(attotime::never);
	update_irq();
}

void opti_82c861_device::map_extra(uint64_t memory_window_start, uint64_t memory_window_end, uint64_t memory_offset, address_space *memory_space,
							uint64_t io_window_start, uint64_t io_window_end, uint64_t io_offset, address_space *io_space)
{
}

attotime opti_82c861_device::frame_period() const
{
	return attotime::from_ticks(BIT(m_HcFmInterval, 0, 14) + 1, BIT_CLOCK);
}

void opti_82c861_device::update_irq()
{
	const bool irq = (m_HcInterruptEnable & INT_MIE) && (m_HcInterruptStatus & m_HcInterruptEnable & INT_ALL);
	if (m_irq_map[0] != 0xff)
		irq_pin_w(0, irq);
}

// At the start of each frame the controller counts the frame, copies the number to the HCCA
// and reports start of frame.
TIMER_CALLBACK_MEMBER(opti_82c861_device::frame_tick)
{
	m_HcFmNumber++;
	if (!BIT(m_HcFmNumber, 0, 15))
		m_HcInterruptStatus |= INT_FNO;
	m_frame_remaining_toggle = BIT(m_HcFmInterval, 31);

	// HccaFrameNumber, and HccaPad1 cleared
	if (BIT(command, 2) && m_HcHCCA)
		get_pci_busmaster_space()->write_dword(m_HcHCCA + 0x80, m_HcFmNumber);

	m_HcInterruptStatus |= INT_SF;
	update_irq();

	m_frame_timer->adjust(frame_period());
}

void opti_82c861_device::control_w(offs_t offset, u32 data, u32 mem_mask)
{
	const bool was_operational = operational();
	COMBINE_DATA(&m_HcControl);
	m_HcControl &= 0x0000'07ff;
	LOGREGS("%s: HcControl = %08x\n", machine().describe_context(), m_HcControl);

	if (operational() && !was_operational)
		m_frame_timer->adjust(frame_period());
	else if (!operational())
		m_frame_timer->adjust(attotime::never);
}

void opti_82c861_device::command_status_w(offs_t offset, u32 data, u32 mem_mask)
{
	data &= mem_mask;
	LOGREGS("%s: HcCommandStatus = %08x\n", machine().describe_context(), data);

	// HostControllerReset completes immediately
	if (BIT(data, 0))
		reset_registers(false);

	// ControlListFilled, BulkListFilled, OwnershipChangeRequest: write 1 to set
	m_HcCommandStatus |= data & 0x0000'000e;
}

void opti_82c861_device::interrupt_status_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_HcInterruptStatus &= ~(data & mem_mask & INT_ALL);
	update_irq();
}

void opti_82c861_device::interrupt_enable_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_HcInterruptEnable |= data & mem_mask & (INT_ALL | INT_MIE);
	LOGREGS("%s: HcInterruptEnable = %08x\n", machine().describe_context(), m_HcInterruptEnable);
	update_irq();
}

void opti_82c861_device::interrupt_disable_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_HcInterruptEnable &= ~(data & mem_mask & (INT_ALL | INT_MIE));
	LOGREGS("%s: HcInterruptEnable = %08x\n", machine().describe_context(), m_HcInterruptEnable);
	update_irq();
}

// FrameRemaining counts bit times down from FrameInterval to the next frame
u32 opti_82c861_device::fm_remaining_r()
{
	u32 remaining = 0;
	if (operational())
		remaining = u32(std::min<u64>(m_frame_timer->remaining().as_ticks(BIT_CLOCK), BIT(m_HcFmInterval, 0, 14)));
	return (m_frame_remaining_toggle ? 0x8000'0000 : 0) | remaining;
}

// Nothing is ever connected: power can be switched, and a request to enable, suspend or reset
// the port reports a connect status change instead.
template <u32 N>
void opti_82c861_device::port_status_w(offs_t offset, u32 data, u32 mem_mask)
{
	data &= mem_mask;
	LOGREGS("%s: HcRhPortStatus[%d] = %08x\n", machine().describe_context(), N, data);

	m_HcRhPortStatus[N] &= ~(data & PORT_CHANGE);
	if (BIT(data, 8))
		m_HcRhPortStatus[N] |= PORT_PPS;
	if (BIT(data, 9))
		m_HcRhPortStatus[N] &= ~PORT_PPS;
	if (data & 0x0000'0016)
	{
		m_HcRhPortStatus[N] |= PORT_CSC;
		m_HcInterruptStatus |= INT_RHSC;
	}
	update_irq();
}
