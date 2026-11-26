// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    Texas Instruments TSB12LV21 "PCILynx" IEEE 1394 link layer controller

    A pre-OHCI FireWire link controller.  The host driver builds Packet
    Control Lists (PCLs) in memory and runs them on five DMA channels; the
    PHY sits behind the PHY access register at 0xf0c.

    PCI resources:
        BAR0    4 KiB of internal registers.  0x000-0x0ff mirrors the PCI
                configuration space, including the miscellaneous registers
                at 0x40-0xff.
        BAR1    64 KiB local bus RAM port
        BAR2    64 KiB local bus AUX/ZV port
        ROM     local bus ROM (autoboot)

    This is a skeleton for machines whose firmware or OS probes the
    controller.  It models a bus with nothing plugged in: a 3-port 1394a
    PHY with every port disconnected, so a bus reset always ends with this
    node as root, physical ID 0, and a single self-ID packet (its own).
    That packet is delivered through whichever DMA channel has self-ID
    reception enabled, followed by the PHY's register 0 status transfer.

    TODO:
    - Asynchronous and isochronous transmit, and reception of anything
      other than self-ID packets.  Transmit PCLs are accepted and left busy.
    - Auxiliary PCL commands (LOAD, STORE, COMPARE, BRANCH, ADD...)
    - Serial EEPROM: none is attached, so the bus info block and GUID
      autoload reports "not present".  Check whether the Power Mac G3
      (Blue & White) board has one.
    - Local bus RAM/ROM/AUX/ZV ports are open bus
    - Cycle start/second interrupts and cycle master operation
    - The PHY part number isn't confirmed for any Mac; vendor and product
      ID are TI's OUI and a placeholder
    - Revision ID: 04 is the TSB12LV21B; check what the Mac boards report

    References:
    - TI TSB12LV21 data manual (register map and bit definitions)
    - Linux drivers/ieee1394/pcilynx.[ch] and drivers/firewire/nosy.h
    - IEEE 1394a-2000 PHY register map and self-ID packet format

***************************************************************************/

#include "emu.h"
#include "pcilynx.h"

#include <algorithm>

#define LOG_REG     (1U << 1)
#define LOG_PHY     (1U << 2)
#define LOG_DMA     (1U << 3)

#define VERBOSE (0)
#include "logmacro.h"

#define LOGREG(...)     LOGMASKED(LOG_REG, __VA_ARGS__)
#define LOGPHY(...)     LOGMASKED(LOG_PHY, __VA_ARGS__)
#define LOGDMA(...)     LOGMASKED(LOG_DMA, __VA_ARGS__)


DEFINE_DEVICE_TYPE(TI_PCILYNX, ti_pcilynx_device, "pcilynx", "TI TSB12LV21 PCILynx IEEE 1394 link layer controller")


namespace {

// MISC_CONTROL (0x40)
constexpr u32 MISC_SWRST            = 1U << 0;
constexpr u32 MISC_SET_FORCE_INT    = 1U << 1;
constexpr u32 MISC_RW_BITS          = 0x0000'f8c8;  // retry count/enable, posted writes, slave burst, pause

// SERIAL_EEPROM_CONTROL (0x44)
constexpr u32 EEP_TIMER_5USEC       = 1U << 0;
constexpr u32 EEP_DAT               = 1U << 4;
constexpr u32 EEP_ENA               = 1U << 5;
constexpr u32 EEP_CLK               = 1U << 6;
constexpr u32 EEP_NOTPRS            = 1U << 7;

// PCI_INT_STATUS / PCI_INT_ENABLE (0x48/0x4c)
constexpr u32 PCI_INT_INT_PEND      = 1U << 31;
constexpr u32 PCI_INT_FRC_INT       = 1U << 30;
constexpr u32 PCI_INT_P1394_INT     = 1U << 16;
constexpr u32 PCI_INT_DMA_HLT(int ch) { return 1U << (ch * 2); }
constexpr u32 PCI_INT_DMA_PCL(int ch) { return 1U << (ch * 2 + 1); }

// DMA channel control and status
constexpr u32 DMA_CTRL_ENA          = 1U << 31;
constexpr u32 DMA_CTRL_BSY          = 1U << 30;
constexpr u32 DMA_CTRL_LINK         = 1U << 29;
constexpr u32 DMA_STAT_SELF_ID      = 1U << 31;
constexpr u32 DMA_STAT_PKT_CMP      = 1U << 27;

// PCL layout: next, async error next, user data, status, remaining count,
// next data buffer, then 13 control/pointer pairs
constexpr u32 PCL_STATUS            = 0x0c;
constexpr u32 PCL_BUFFER            = 0x18;
constexpr int PCL_BUFFERS           = 13;
constexpr u32 PCL_NEXT_INVALID      = 1U << 0;
constexpr u32 PCL_GEN_INTR          = 1U << 19;
constexpr u32 PCL_LAST_BUFF         = 1U << 18;
constexpr u32 PCL_BIGENDIAN         = 1U << 16;
constexpr u32 PCL_CMD_RCV           = 0x1;
constexpr u32 PCL_CMD_RCV_AND_UPDATE = 0xa;

// receive comparator word 1 enable
constexpr u32 CMP1_RCV_SELF_ID_EN   = 1U << 10;

// FIFO_CONTROL flush requests
constexpr u32 FIFO_CONTROL_FLUSH    = 0x0000'001c;

// LINK_CONTROL (0xf04)
constexpr u32 LINK_CONTROL_TX_ASYNC_EN      = 1U << 24;
constexpr u32 LINK_CONTROL_RX_ASYNC_EN      = 1U << 23;
constexpr u32 LINK_CONTROL_RSTTX            = 1U << 21;
constexpr u32 LINK_CONTROL_RSTRX            = 1U << 20;
constexpr u32 LINK_CONTROL_CYCTIMEREN       = 1U << 9;
constexpr u32 LINK_CONTROL_RCV_COMP_VALID   = 1U << 7;

// LINK_PHY (0xf0c)
constexpr u32 LINK_PHY_RDPHY        = 1U << 31;
constexpr u32 LINK_PHY_WRPHY        = 1U << 30;

// LINK_INT_STATUS / LINK_INT_ENABLE (0xf14/0xf18)
constexpr u32 LINK_INT_LINK_INT     = 1U << 31;
constexpr u32 LINK_INT_PHY_REG_RCVD = 1U << 29;
constexpr u32 LINK_INT_PHY_BUSRESET = 1U << 28;

// PHY: 1394a register set, three ports, all disconnected
constexpr int PHY_PORTS = 3;
constexpr u32 PHY_VENDOR_ID = 0x08'0028;    // TI
constexpr u32 PHY_PRODUCT_ID = 0x00'0000;   // unknown
constexpr u8 PHY_REG1_RHB = 0x80;
constexpr u8 PHY_REG1_IBR = 0x40;
constexpr u8 PHY_REG5_ISBR = 0x40;
constexpr u8 PHY_REG5_INT_BITS = 0x3c;      // CTOI, CPSI, STOI and PEI clear on write

// PHY status for a disconnected port: both arbitration comparators read Z
constexpr u8 PHY_PORT_DISCONNECTED = 0xf0;

// a long bus reset lasts at least 166.7 us; arbitration with no other nodes is quick
constexpr attotime LONG_RESET_TIME = attotime::from_usec(170);
constexpr attotime SHORT_RESET_TIME = attotime::from_usec(2);

// 24.576 MHz cycle timer: offset 0-3071, count 0-7999, 7 bits of seconds
constexpr u32 CYCLE_CLOCK = 24'576'000;
constexpr u64 CYCLE_OFFSETS = 3072;
constexpr u64 CYCLES_PER_SECOND = 8000;
constexpr u64 CYCLE_TIMER_WRAP = 128 * CYCLES_PER_SECOND * CYCLE_OFFSETS;

constexpr u8 REVISION = 0x04;

} // anonymous namespace


ti_pcilynx_device::ti_pcilynx_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: pci_card_device(mconfig, TI_PCILYNX, tag, owner, clock)
	, m_self_id_timer(nullptr)
{
	// the subsystem IDs are autoloaded from the serial EEPROM, which isn't fitted
	set_ids(0x104c'8000, REVISION, 0x0c'0000, 0x0000'0000);
}

void ti_pcilynx_device::device_start()
{
	pci_card_device::device_start();

	add_map(0x1000, M_MEM, FUNC(ti_pcilynx_device::regs_map));
	add_map(0x10000, M_MEM, FUNC(ti_pcilynx_device::ram_map));
	add_map(0x10000, M_MEM, FUNC(ti_pcilynx_device::aux_map));

	intr_pin = 1;
	minimum_grant = 0x01;
	maximum_latency = 0x02;

	m_self_id_timer = timer_alloc(FUNC(ti_pcilynx_device::self_id_done), this);

	m_cache_line_size = m_latency_timer = 0;
	std::fill(std::begin(m_misc_regs), std::end(m_misc_regs), 0);
	std::fill(std::begin(m_phy_regs), std::end(m_phy_regs), 0);
	m_phy_initiated_reset = false;

	save_item(NAME(m_cache_line_size));
	save_item(NAME(m_latency_timer));
	save_item(NAME(m_misc_control));
	save_item(NAME(m_eeprom_control));
	save_item(NAME(m_pci_int_status));
	save_item(NAME(m_pci_int_enable));
	save_item(NAME(m_misc_regs));
	save_item(STRUCT_MEMBER(m_dma, prev_pcl));
	save_item(STRUCT_MEMBER(m_dma, current_pcl));
	save_item(STRUCT_MEMBER(m_dma, data_buf));
	save_item(STRUCT_MEMBER(m_dma, status));
	save_item(STRUCT_MEMBER(m_dma, control));
	save_item(STRUCT_MEMBER(m_dma, ready));
	save_item(STRUCT_MEMBER(m_dma, cmp));
	save_item(NAME(m_dma_global));
	save_item(NAME(m_fifo_sizes));
	save_item(NAME(m_fifo_control));
	save_item(NAME(m_fifo_threshold));
	save_item(NAME(m_link_id));
	save_item(NAME(m_link_control));
	save_item(NAME(m_link_phy));
	save_item(NAME(m_diag));
	save_item(NAME(m_link_int_status));
	save_item(NAME(m_link_int_enable));
	save_item(NAME(m_cycle_timer));
	save_item(NAME(m_cycle_timer_time));
	save_item(NAME(m_phy_regs));
	save_item(NAME(m_phy_initiated_reset));
}

void ti_pcilynx_device::device_reset()
{
	pci_card_device::device_reset();

	command = 0x0000;
	command_mask = 0x0156;      // memory, bus master, MWI, parity and SERR enables
	status = 0x0200;            // medium DEVSEL
	m_cache_line_size = 0;
	m_latency_timer = 0;
	std::fill(std::begin(m_misc_regs), std::end(m_misc_regs), 0);

	soft_reset();

	// PHY power-on state: lone node, root, cable power present
	m_phy_regs[0] = 0x03;
	m_phy_regs[1] = 0x3f;       // gap count
	m_phy_regs[2] = 0xe0 | PHY_PORTS;   // extended register set
	m_phy_regs[3] = 0x40;       // S400, no repeater delay
	m_phy_regs[4] = 0x80;       // link active
	m_phy_regs[5] = 0x00;
	m_phy_regs[6] = 0x00;
	m_phy_regs[7] = 0x00;

	// the PHY resets the bus when it powers up
	bus_reset(false, false);
}

// MISC_CONTROL.SWRST resets the link and DMA engines; PCI configuration and the PHY are untouched
void ti_pcilynx_device::soft_reset()
{
	m_misc_control = 0;
	m_eeprom_control = 0;
	m_pci_int_status = 0;
	m_pci_int_enable = 0;

	for (auto &dma : m_dma)
		dma = dma_channel();
	m_dma_global = 0x02U << 24;     // PCI lower bound 20 bytes
	m_fifo_sizes = 0;
	m_fifo_control = 0;
	m_fifo_threshold = 0x0000'ffff;

	m_link_id = 0;
	m_link_control = 0;
	m_link_phy = 0;
	m_diag = 0;
	m_link_int_status = 0;
	m_link_int_enable = 0;
	m_cycle_timer = 0;
	m_cycle_timer_time = machine().time();

	update_irq();
}


/***************************************************************************
    PCI configuration and address maps
***************************************************************************/

void ti_pcilynx_device::config_map(address_map &map)
{
	pci_card_device::config_map(map);

	map(0x0c, 0x0c).lw8(NAME([this] (u8 data) { m_cache_line_size = data; }));
	map(0x0d, 0x0d).lw8(NAME([this] (u8 data) { m_latency_timer = data; }));

	// the miscellaneous registers are shared with BAR0
	misc_map(map);
}

uint8_t ti_pcilynx_device::cache_line_size_r()
{
	return m_cache_line_size;
}

uint8_t ti_pcilynx_device::latency_timer_r()
{
	return m_latency_timer;
}

// configuration space 0x40-0xff, also at BAR0 0x40-0xff
void ti_pcilynx_device::misc_map(address_map &map)
{
	// SWRST and SET_FORCE_INT read as zero; the AUTOBOOT pin is tied low
	map(0x40, 0x43).lr32(NAME([this] () -> u32 { return m_misc_control; }));
	map(0x40, 0x43).w(FUNC(ti_pcilynx_device::misc_control_w));
	map(0x44, 0x47).rw(FUNC(ti_pcilynx_device::eeprom_control_r), FUNC(ti_pcilynx_device::eeprom_control_w));
	map(0x48, 0x4b).rw(FUNC(ti_pcilynx_device::pci_int_status_r), FUNC(ti_pcilynx_device::pci_int_status_w));
	map(0x4c, 0x4f).rw(FUNC(ti_pcilynx_device::pci_int_enable_r), FUNC(ti_pcilynx_device::pci_int_enable_w));

	// TODO: subsystem ID loading through 0x58, ROM and local bus control, GPIO pins
	map(0x50, 0xff).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_misc_regs[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_misc_regs[offset]); }));
}

void ti_pcilynx_device::regs_map(address_map &map)
{
	// the configuration header reads back at 0x00-0x3f
	map(0x000, 0x03f).r(FUNC(ti_pcilynx_device::header_mirror_r));
	map(0x000, 0x03f).nopw();
	misc_map(map);

	for (int ch = 0; ch < DMA_CHANNELS; ch++)
	{
		const offs_t base = 0x100 + ch * 0x20;
		map(base + 0x00, base + 0x03).lrw32(
				NAME(([this, ch] () -> u32 { return m_dma[ch].prev_pcl; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma[ch].prev_pcl); })));
		map(base + 0x04, base + 0x07).lrw32(
				NAME(([this, ch] () -> u32 { return m_dma[ch].current_pcl; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma[ch].current_pcl); })));
		map(base + 0x08, base + 0x0b).lr32(NAME(([this, ch] () -> u32 { return m_dma[ch].data_buf; })));
		map(base + 0x0c, base + 0x0f).lrw32(
				NAME(([this, ch] () -> u32 { return m_dma[ch].status; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma[ch].status); })));
		map(base + 0x10, base + 0x13).lrw32(
				NAME(([this, ch] () -> u32 { return m_dma[ch].control; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask)
				{
					u32 val = m_dma[ch].control;
					COMBINE_DATA(&val);
					dma_control_w(ch, val);
				})));
		map(base + 0x14, base + 0x17).lrw32(
				NAME(([this, ch] () -> u32 { return m_dma[ch].ready; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma[ch].ready); })));
		map(base + 0x18, base + 0x1b).lr32(NAME([] () -> u32 { return 0; }));    // DMA state: idle

		// receive comparators
		map(0xb00 + ch * 0x10, 0xb0f + ch * 0x10).lrw32(
				NAME(([this, ch] (offs_t offset) -> u32 { return m_dma[ch].cmp[offset]; })),
				NAME(([this, ch] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma[ch].cmp[offset]); })));
	}

	map(0x908, 0x90b).lrw32(
			NAME([this] () -> u32 { return m_dma_global; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_dma_global); }));

	map(0xa00, 0xa03).lrw32(
			NAME([this] () -> u32 { return m_fifo_sizes; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_fifo_sizes); }));
	map(0xa10, 0xa13).lrw32(
			NAME([this] () -> u32 { return m_fifo_control; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask)
			{
				// flushes complete at once
				COMBINE_DATA(&m_fifo_control);
				m_fifo_control &= ~FIFO_CONTROL_FLUSH;
			}));
	map(0xa14, 0xa17).lrw32(
			NAME([this] () -> u32 { return m_fifo_threshold; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_fifo_threshold); }));

	map(0xf00, 0xf03).lrw32(
			NAME([this] () -> u32 { return m_link_id; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask)
			{
				COMBINE_DATA(&m_link_id);
				m_link_id &= 0xffff'0000;
			}));
	map(0xf04, 0xf07).lr32(NAME([this] () -> u32 { return m_link_control; }));
	map(0xf04, 0xf07).w(FUNC(ti_pcilynx_device::link_control_w));
	map(0xf08, 0xf0b).rw(FUNC(ti_pcilynx_device::cycle_timer_r), FUNC(ti_pcilynx_device::cycle_timer_w));
	map(0xf0c, 0xf0f).lr32(NAME([this] () -> u32 { return m_link_phy; }));
	map(0xf0c, 0xf0f).w(FUNC(ti_pcilynx_device::link_phy_w));
	map(0xf10, 0xf13).lrw32(
			NAME([this] () -> u32 { return m_diag; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_diag); }));
	map(0xf14, 0xf17).lr32(NAME([this] () -> u32 { return m_link_int_status | (m_link_int_status ? LINK_INT_LINK_INT : 0); }));
	map(0xf14, 0xf17).w(FUNC(ti_pcilynx_device::link_int_status_w));
	map(0xf18, 0xf1b).lr32(NAME([this] () -> u32 { return m_link_int_enable; }));
	map(0xf18, 0xf1b).w(FUNC(ti_pcilynx_device::link_int_enable_w));
}

// nothing is connected to the local bus
void ti_pcilynx_device::ram_map(address_map &map)
{
	map(0x0000, 0xffff).lr32(NAME([] () -> u32 { return 0xffff'ffff; }));
	map(0x0000, 0xffff).nopw();
}

void ti_pcilynx_device::aux_map(address_map &map)
{
	map(0x0000, 0xffff).lr32(NAME([] () -> u32 { return 0xffff'ffff; }));
	map(0x0000, 0xffff).nopw();
}

u32 ti_pcilynx_device::header_mirror_r(offs_t offset)
{
	switch (offset)
	{
	case 0x00 / 4: return (main_id << 16) | (main_id >> 16);
	case 0x04 / 4: return command | (u32(status) << 16);
	case 0x08 / 4: return (pclass << 8) | revision;
	case 0x0c / 4: return m_cache_line_size | (u32(m_latency_timer) << 8) | (u32(header_type_r()) << 16);
	case 0x10 / 4: case 0x14 / 4: case 0x18 / 4: case 0x1c / 4: case 0x20 / 4: case 0x24 / 4:
		return address_base_r(offset - 0x10 / 4);
	case 0x2c / 4: return (subsystem_id << 16) | (subsystem_id >> 16);
	case 0x30 / 4: return expansion_rom_base;
	case 0x3c / 4: return intr_line | (u32(intr_pin) << 8) | (u32(minimum_grant) << 16) | (u32(maximum_latency) << 24);
	default: return 0;
	}
}


/***************************************************************************
    Registers
***************************************************************************/

void ti_pcilynx_device::misc_control_w(offs_t offset, u32 data, u32 mem_mask)
{
	LOGREG("%s: MISC_CONTROL = %08x & %08x\n", machine().describe_context(), data, mem_mask);
	const u32 bits = data & mem_mask;
	m_misc_control = (m_misc_control & ~(mem_mask & MISC_RW_BITS)) | (bits & MISC_RW_BITS);

	if (bits & MISC_SWRST)
	{
		LOG("%s: software reset\n", machine().describe_context());
		soft_reset();
	}
	else if (bits & MISC_SET_FORCE_INT)
	{
		m_pci_int_status |= PCI_INT_FRC_INT;
		update_irq();
	}
}

// with no EEPROM fitted the lines only ever read back what's driven
u32 ti_pcilynx_device::eeprom_control_r()
{
	const u32 lines = (m_eeprom_control & EEP_ENA) ? (m_eeprom_control & (EEP_CLK | EEP_DAT)) : (EEP_CLK | EEP_DAT);
	return (m_eeprom_control & EEP_ENA) | lines | EEP_NOTPRS | EEP_TIMER_5USEC;
}

void ti_pcilynx_device::eeprom_control_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_eeprom_control);
	m_eeprom_control &= EEP_CLK | EEP_ENA | EEP_DAT;
}

// INT_PEND reads back in both the status and the enable register
u32 ti_pcilynx_device::pci_int_status_r()
{
	const bool pend = m_pci_int_status & m_pci_int_enable & ~PCI_INT_INT_PEND;
	return m_pci_int_status | (pend ? PCI_INT_INT_PEND : 0);
}

void ti_pcilynx_device::pci_int_status_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_pci_int_status &= ~(data & mem_mask & ~PCI_INT_INT_PEND);
	update_irq();
}

u32 ti_pcilynx_device::pci_int_enable_r()
{
	const bool pend = m_pci_int_status & m_pci_int_enable & ~PCI_INT_INT_PEND;
	return m_pci_int_enable | (pend ? PCI_INT_INT_PEND : 0);
}

void ti_pcilynx_device::pci_int_enable_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_pci_int_enable);
	m_pci_int_enable &= ~PCI_INT_INT_PEND;
	update_irq();
}

void ti_pcilynx_device::link_control_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 val = m_link_control;
	COMBINE_DATA(&val);
	if ((val ^ m_link_control) & LINK_CONTROL_CYCTIMEREN)
	{
		// freeze or restart the cycle timer at its current value
		m_cycle_timer = cycle_timer_r();
		m_cycle_timer_time = machine().time();
	}

	// the transmitter and receiver resets complete immediately
	m_link_control = val & ~(LINK_CONTROL_RSTTX | LINK_CONTROL_RSTRX);
}

u32 ti_pcilynx_device::cycle_timer_r()
{
	if (!(m_link_control & LINK_CONTROL_CYCTIMEREN))
		return m_cycle_timer;

	const u64 seconds = BIT(m_cycle_timer, 25, 7);
	const u64 cycles = std::min<u64>(BIT(m_cycle_timer, 12, 13), CYCLES_PER_SECOND - 1);
	const u64 offset = std::min<u64>(BIT(m_cycle_timer, 0, 12), CYCLE_OFFSETS - 1);
	const u64 elapsed = (machine().time() - m_cycle_timer_time).as_ticks(CYCLE_CLOCK);
	const u64 ticks = ((seconds * CYCLES_PER_SECOND + cycles) * CYCLE_OFFSETS + offset + elapsed) % CYCLE_TIMER_WRAP;

	const u64 total_cycles = ticks / CYCLE_OFFSETS;
	return (u32(total_cycles / CYCLES_PER_SECOND) << 25) | (u32(total_cycles % CYCLES_PER_SECOND) << 12) | u32(ticks % CYCLE_OFFSETS);
}

void ti_pcilynx_device::cycle_timer_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 val = cycle_timer_r();
	COMBINE_DATA(&val);
	m_cycle_timer = val;
	m_cycle_timer_time = machine().time();
}

void ti_pcilynx_device::link_phy_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 val = m_link_phy;
	COMBINE_DATA(&val);
	m_link_phy = (val & 0x0fff'0000) | (m_link_phy & 0x0000'0fff);

	const int addr = BIT(val, 24, 4);
	if (val & LINK_PHY_WRPHY)
		phy_w(addr, BIT(val, 16, 8));
	if (val & LINK_PHY_RDPHY)
	{
		m_link_phy = (m_link_phy & ~0x0000'0fffU) | (addr << 8) | phy_r(addr);
		link_int(LINK_INT_PHY_REG_RCVD);
	}
}

void ti_pcilynx_device::link_int_status_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_link_int_status &= ~(data & mem_mask);
	update_irq();
}

void ti_pcilynx_device::link_int_enable_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_link_int_enable);
	m_link_int_enable &= ~LINK_INT_LINK_INT;
}


/***************************************************************************
    Interrupts
***************************************************************************/

// Link status bits are only set when enabled.  P1394_INT latches when any
// is set, and comes straight back if cleared while one is still pending.
void ti_pcilynx_device::link_int(u32 bits)
{
	m_link_int_status |= bits & m_link_int_enable;
	update_irq();
}

void ti_pcilynx_device::update_irq()
{
	if (m_link_int_status)
		m_pci_int_status |= PCI_INT_P1394_INT;
	irq_pin_w(0, (m_pci_int_status & m_pci_int_enable & ~PCI_INT_INT_PEND) != 0);
}


/***************************************************************************
    PHY
***************************************************************************/

u8 ti_pcilynx_device::phy_r(int reg)
{
	u8 data = 0;
	if (reg < 8)
	{
		data = m_phy_regs[reg];
	}
	else
	{
		// registers 8-15 are paged by register 7
		const int page = BIT(m_phy_regs[7], 5, 3);
		const int port = BIT(m_phy_regs[7], 0, 4);
		switch (page)
		{
		case 0:     // port status
			if (port < PHY_PORTS && reg == 8)
				data = PHY_PORT_DISCONNECTED;
			break;

		case 1:     // vendor identification
			switch (reg)
			{
			case 8:  data = 0x01; break;    // compliance level: 1394a
			case 10: data = u8(PHY_VENDOR_ID >> 16); break;
			case 11: data = u8(PHY_VENDOR_ID >> 8); break;
			case 12: data = u8(PHY_VENDOR_ID); break;
			case 13: data = u8(PHY_PRODUCT_ID >> 16); break;
			case 14: data = u8(PHY_PRODUCT_ID >> 8); break;
			case 15: data = u8(PHY_PRODUCT_ID); break;
			}
			break;
		}
	}

	LOGPHY("%s: PHY register %d read %02x\n", machine().describe_context(), reg, data);
	return data;
}

void ti_pcilynx_device::phy_w(int reg, u8 data)
{
	LOGPHY("%s: PHY register %d = %02x\n", machine().describe_context(), reg, data);

	switch (reg)
	{
	case 1:
		m_phy_regs[1] = data & ~PHY_REG1_IBR;
		if (data & PHY_REG1_IBR)
			bus_reset(true, false);
		break;

	case 4:
	case 7:
		m_phy_regs[reg] = data;
		break;

	case 5:
		m_phy_regs[5] = (data & ~(PHY_REG5_ISBR | PHY_REG5_INT_BITS)) | (m_phy_regs[5] & ~data & PHY_REG5_INT_BITS);
		if (data & PHY_REG5_ISBR)
			bus_reset(true, true);
		break;

	default:
		// 0, 2, 3 and the port status and vendor pages are read-only
		break;
	}
}

void ti_pcilynx_device::bus_reset(bool initiated, bool short_reset)
{
	LOG("%s: %s bus reset%s\n", machine().describe_context(), short_reset ? "short" : "long", initiated ? " initiated by host" : "");

	m_phy_initiated_reset = initiated;

	// the link stops asynchronous traffic until software has seen the new topology
	m_link_control &= ~(LINK_CONTROL_TX_ASYNC_EN | LINK_CONTROL_RX_ASYNC_EN);
	link_int(LINK_INT_PHY_BUSRESET);

	// tree identify always makes the only node the root, with physical ID 0
	m_phy_regs[0] = 0x03;
	m_phy_regs[1] &= ~PHY_REG1_RHB;

	m_self_id_timer->adjust(short_reset ? SHORT_RESET_TIME : LONG_RESET_TIME);
}

// 1394a self-ID packet 0 for this node, with every port "not connected"
u32 ti_pcilynx_device::own_self_id() const
{
	u32 sid = 0x8000'0000;
	sid |= u32(BIT(m_phy_regs[0], 2, 6)) << 24;     // physical ID
	sid |= u32(BIT(m_phy_regs[4], 7)) << 22;        // link active
	sid |= u32(BIT(m_phy_regs[1], 0, 6)) << 16;     // gap count
	sid |= u32(BIT(m_phy_regs[3], 5, 2)) << 14;     // speed
	sid |= u32(BIT(m_phy_regs[4], 6)) << 11;        // contender
	sid |= u32(BIT(m_phy_regs[4], 0, 3)) << 8;      // power class
	for (int port = 0; port < PHY_PORTS; port++)
		sid |= 1U << (6 - port * 2);
	if (m_phy_initiated_reset)
		sid |= 1U << 1;
	return sid;
}

TIMER_CALLBACK_MEMBER(ti_pcilynx_device::self_id_done)
{
	// A 1394a PHY forwards its own self-ID packet to the link as a quadlet
	// and its inverse.  It goes to the first channel with self-ID reception enabled.
	const u32 sid = own_self_id();
	const u32 packet[2] = { sid, ~sid };

	if (m_link_control & LINK_CONTROL_RCV_COMP_VALID)
	{
		int ch;
		for (ch = 0; ch < DMA_CHANNELS; ch++)
		{
			if (m_dma[ch].cmp[3] & CMP1_RCV_SELF_ID_EN)
				break;
		}
		if (ch == DMA_CHANNELS)
			LOGDMA("self-ID %08x: no channel accepts self-ID packets\n", sid);
		else if (!dma_receive(ch, packet, 2, DMA_STAT_SELF_ID))
			LOG("self-ID %08x dropped: channel %d has no receive PCL\n", sid, ch);
		else
			LOGDMA("self-ID %08x received on channel %d\n", sid, ch);
	}

	// then the PHY sends its register 0 (physical ID and root flag)
	m_link_phy = (m_link_phy & ~0x0000'0fffU) | m_phy_regs[0];
	link_int(LINK_INT_PHY_REG_RCVD);
}


/***************************************************************************
    DMA
***************************************************************************/

u32 ti_pcilynx_device::bm_read32(u32 addr)
{
	address_space *const space = get_pci_busmaster_space();
	return space ? space->read_dword(addr & ~3U) : 0xffff'ffff;
}

void ti_pcilynx_device::bm_write32(u32 addr, u32 data)
{
	address_space *const space = get_pci_busmaster_space();
	if (space)
		space->write_dword(addr & ~3U, data);
}

void ti_pcilynx_device::bm_write8(u32 addr, u8 data)
{
	address_space *const space = get_pci_busmaster_space();
	if (space)
		space->write_byte(addr, data);
}

// CURRENT_PCL points at a word holding the next PCL's address; with LINK set
// the channel follows it.  A next address with bit 0 set halts the channel.
void ti_pcilynx_device::dma_control_w(int ch, u32 data)
{
	dma_channel &dma = m_dma[ch];
	dma.control = data & ~DMA_CTRL_BSY;

	if (!(data & DMA_CTRL_ENA) || !(data & DMA_CTRL_LINK))
		return;

	if (!BIT(command, 2))
	{
		LOG("%s: channel %d started with bus mastering disabled\n", machine().describe_context(), ch);
		return;
	}

	const u32 next = bm_read32(dma.current_pcl);
	if (next & PCL_NEXT_INVALID)
	{
		LOGDMA("%s: channel %d started on an invalid PCL\n", machine().describe_context(), ch);
		dma_halt(ch);
		return;
	}

	dma.control |= DMA_CTRL_BSY;
	const u32 cmd = BIT(bm_read32(next + PCL_BUFFER), 24, 4);
	if (cmd == PCL_CMD_RCV || cmd == PCL_CMD_RCV_AND_UPDATE)
		LOGDMA("%s: channel %d waiting to receive into PCL %08x\n", machine().describe_context(), ch, next);
	else
		logerror("%s: channel %d PCL %08x command %x not emulated, channel stays busy\n", machine().describe_context(), ch, next, cmd);
}

void ti_pcilynx_device::dma_halt(int ch)
{
	m_dma[ch].control &= ~(DMA_CTRL_BSY | DMA_CTRL_LINK);
	m_pci_int_status |= PCI_INT_DMA_HLT(ch);
	update_irq();
}

// run one receive PCL over a packet; returns false if the channel can't take it
bool ti_pcilynx_device::dma_receive(int ch, const u32 *quadlets, u32 count, u32 status)
{
	dma_channel &dma = m_dma[ch];
	if ((dma.control & (DMA_CTRL_ENA | DMA_CTRL_LINK)) != (DMA_CTRL_ENA | DMA_CTRL_LINK) || !BIT(command, 2))
		return false;

	const u32 pcl = bm_read32(dma.current_pcl);
	if (pcl & PCL_NEXT_INVALID)
	{
		dma_halt(ch);
		return false;
	}

	const u32 cmd = BIT(bm_read32(pcl + PCL_BUFFER), 24, 4);
	if (cmd != PCL_CMD_RCV && cmd != PCL_CMD_RCV_AND_UPDATE)
	{
		logerror("channel %d: PCL %08x command %x can't receive\n", ch, pcl, cmd);
		return false;
	}

	// scatter the packet over the buffers; quadlets keep 1394 byte order in big-endian buffers
	const u32 total = count * 4;
	u32 done = 0;
	bool interrupt = false;
	for (int buf = 0; buf < PCL_BUFFERS && done < total; buf++)
	{
		const u32 ctrl = bm_read32(pcl + PCL_BUFFER + buf * 8);
		const u32 ptr = bm_read32(pcl + PCL_BUFFER + buf * 8 + 4);
		const u32 len = ctrl & 0xfff;
		const bool big_endian = ctrl & PCL_BIGENDIAN;
		if (ctrl & PCL_GEN_INTR)
			interrupt = true;

		for (u32 i = 0; i < len && done < total; i++, done++)
		{
			const int shift = big_endian ? (24 - 8 * (done & 3)) : (8 * (done & 3));
			bm_write8(ptr + i, u8(quadlets[done >> 2] >> shift));
		}
		dma.data_buf = ptr;

		if (ctrl & PCL_LAST_BUFF)
			break;
	}
	if (done < total)
		LOGDMA("channel %d: PCL %08x buffers too small, %u of %u bytes stored\n", ch, pcl, done, total);

	dma.status = status | DMA_STAT_PKT_CMP | total;
	bm_write32(pcl + PCL_STATUS, dma.status);
	dma.prev_pcl = pcl;
	dma.current_pcl = pcl;

	if (interrupt)
		m_pci_int_status |= PCI_INT_DMA_PCL(ch);

	// fetch and check the following PCL
	if (bm_read32(pcl) & PCL_NEXT_INVALID)
		dma_halt(ch);
	else
		update_irq();
	return true;
}
