// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    CMD Technology PCI0646 PCI-IDE controller

    Two ATA channels with bus master DMA.  Each channel runs in
    compatibility mode (fixed ISA addresses, IRQ14 or IRQ15) or native mode
    (BAR0-BAR3, INTA#), as bits 0 and 2 of the programming interface select.
    Bits 1 and 3 say whether software may change them; boards strap the
    reset value.

    The bus master registers in BAR4 also appear in configuration space at
    0x70-0x7f, with CMD registers in the bytes the PCI IDE bus master
    specification leaves reserved.

    Configuration registers with an effect here:
    0x50  CFR       bit 2: primary channel interrupt, write 1 to clear
    0x51  CNTRL     bit 3: secondary channel enable
    0x57  ARTTIM23  bit 4: secondary channel interrupt, write 1 to clear
    0x71  MRDMODE   bits 2-3: channel interrupts, write 1 to clear
                    bits 4-5: block the channel's interrupt output
                    bits 0-1: read mode, write-only
    The other registers in 0x40-0x5f, BMIDECSR (0x79) and UDIDETCR0/1
    (0x73/0x7b) hold PIO, DMA and Ultra DMA timing and are only stored.

    TODO:
    - Check against the PCI0646U2 datasheet: reset values of CFR and CNTRL,
      whether CFR/ARTTIM23 and MRDMODE report the same interrupt latch, and
      whether the interrupt outputs follow INTRQ or the latch
    - Revision differences (Ultra DMA only from the PCI0646U2, DMA quirks
      of revision 1)
    - Bus master enable (command register bit 2) doesn't gate DMA

***************************************************************************/

#include "emu.h"
#include "cmd646.h"

#include <algorithm>
#include <iterator>

#define LOG_REG (1U << 1)
#define LOG_IRQ (1U << 2)
#define LOG_MAP (1U << 3)
#define LOG_ATA (1U << 4)
#define LOG_TASKFILE (1U << 5)

#define VERBOSE (LOG_GENERAL | LOG_REG | LOG_IRQ | LOG_MAP | LOG_ATA | LOG_TASKFILE)
#include "logmacro.h"

#define LOGREG(...) LOGMASKED(LOG_REG, __VA_ARGS__)
#define LOGIRQ(...) LOGMASKED(LOG_IRQ, __VA_ARGS__)
#define LOGMAP(...) LOGMASKED(LOG_MAP, __VA_ARGS__)
#define LOGATA(...) LOGMASKED(LOG_ATA, __VA_ARGS__)
#define LOGTASKFILE(...) LOGMASKED(LOG_TASKFILE, __VA_ARGS__)


namespace {

constexpr char const *const TASKFILE_READ_NAMES[8] = { "data", "error", "sector count", "LBA low", "LBA mid", "LBA high", "device", "status" };
constexpr char const *const TASKFILE_WRITE_NAMES[8] = { "data", "features", "sector count", "LBA low", "LBA mid", "LBA high", "device", "command" };

} // anonymous namespace


DEFINE_DEVICE_TYPE(CMD646, cmd646_device, "cmd646", "CMD PCI0646 PCI-IDE controller")

cmd646_device::cmd646_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	pci_device(mconfig, CMD646, tag, owner, clock),
	m_ide(*this, "ide%u", 1U),
	m_bus_master_space(*this, finder_base::DUMMY_TAG, AS_PROGRAM),
	m_irq_cb(*this),
	m_irq_pri_cb(*this),
	m_irq_sec_cb(*this),
	m_reset_pif(0x8a),
	m_intrq{ false, false },
	m_inta(false),
	m_irq_compat{ false, false }
{
	set_ids(0x1095'0646, 0x07, 0x01'01'8a, 0x1095'0646);
}

void cmd646_device::device_add_mconfig(machine_config &config)
{
	BUS_MASTER_IDE_CONTROLLER(config, m_ide[0]).options(ata_devices, "hdd", nullptr, false);
	m_ide[0]->irq_handler().set(FUNC(cmd646_device::ide_irq<0>));
	m_ide[0]->set_bus_master_space(m_bus_master_space);

	BUS_MASTER_IDE_CONTROLLER(config, m_ide[1]).options(ata_devices, nullptr, nullptr, false);
	m_ide[1]->irq_handler().set(FUNC(cmd646_device::ide_irq<1>));
	m_ide[1]->set_bus_master_space(m_bus_master_space);
}

void cmd646_device::device_start()
{
	pci_device::device_start();

	add_map(8, M_IO, FUNC(cmd646_device::primary_command_map));
	add_map(4, M_IO, FUNC(cmd646_device::primary_control_map));
	add_map(8, M_IO, FUNC(cmd646_device::secondary_command_map));
	add_map(4, M_IO, FUNC(cmd646_device::secondary_control_map));
	add_map(16, M_IO, FUNC(cmd646_device::bus_master_map));

	intr_pin = 1;
	minimum_grant = 0x02;
	maximum_latency = 0x04;

	save_item(NAME(pclass));
	save_item(NAME(m_bar));
	save_item(NAME(m_regs));
	save_item(NAME(m_cfr));
	save_item(NAME(m_cntrl));
	save_item(NAME(m_arttim23));
	save_item(NAME(m_mrdmode));
	save_item(NAME(m_bmidecsr));
	save_item(NAME(m_udidetcr));
	save_item(NAME(m_intr_status));
	save_item(NAME(m_intrq));
	save_item(NAME(m_inta));
	save_item(NAME(m_irq_compat));
}

void cmd646_device::device_reset()
{
	pci_device::device_reset();

	command = 0x0000;
	status = 0x0280; // medium DEVSEL timing, fast back-to-back capable
	pclass = 0x01'01'00 | m_reset_pif;

	std::fill(std::begin(m_bar), std::end(m_bar), 0);
	bank_infos[4].adr = 0;
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	m_cfr = 0x00;
	m_cntrl = 0x0c;
	m_arttim23 = 0x00;
	m_mrdmode = 0x00;
	m_bmidecsr = 0x00;
	std::fill(std::begin(m_udidetcr), std::end(m_udidetcr), 0);
	m_intr_status = 0;

	update_bars();
	update_irq();
}

void cmd646_device::config_map(address_map &map)
{
	pci_device::config_map(map);
	map(0x09, 0x09).w(FUNC(cmd646_device::prog_if_w));
	map(0x10, 0x1f).rw(FUNC(cmd646_device::bar_r), FUNC(cmd646_device::bar_w));
	map(0x40, 0x5f).rw(FUNC(cmd646_device::reg_r), FUNC(cmd646_device::reg_w));
	map(0x50, 0x50).rw(FUNC(cmd646_device::cfr_r), FUNC(cmd646_device::cfr_w));
	map(0x51, 0x51).rw(FUNC(cmd646_device::cntrl_r), FUNC(cmd646_device::cntrl_w));
	map(0x57, 0x57).rw(FUNC(cmd646_device::arttim23_r), FUNC(cmd646_device::arttim23_w));
	map(0x70, 0x7f).m(*this, FUNC(cmd646_device::bus_master_map));
}

void cmd646_device::primary_command_map(address_map &map)
{
	map(0x0, 0x7).rw(FUNC(cmd646_device::cs0_r<0>), FUNC(cmd646_device::cs0_w<0>));
}

// the control block register is at offset 2 of the 4-byte block
void cmd646_device::primary_control_map(address_map &map)
{
	map(0x2, 0x2).rw(FUNC(cmd646_device::cs1_r<0>), FUNC(cmd646_device::cs1_w<0>));
}

void cmd646_device::secondary_command_map(address_map &map)
{
	map(0x0, 0x7).rw(FUNC(cmd646_device::cs0_r<1>), FUNC(cmd646_device::cs0_w<1>));
}

void cmd646_device::secondary_control_map(address_map &map)
{
	map(0x2, 0x2).rw(FUNC(cmd646_device::cs1_r<1>), FUNC(cmd646_device::cs1_w<1>));
}

void cmd646_device::bus_master_map(address_map &map)
{
	map(0x0, 0x3).rw(FUNC(cmd646_device::bm_command_r<0>), FUNC(cmd646_device::bm_command_w<0>));
	map(0x4, 0x7).rw(FUNC(cmd646_device::bm_prd_r<0>), FUNC(cmd646_device::bm_prd_w<0>));
	map(0x8, 0xb).rw(FUNC(cmd646_device::bm_command_r<1>), FUNC(cmd646_device::bm_command_w<1>));
	map(0xc, 0xf).rw(FUNC(cmd646_device::bm_prd_r<1>), FUNC(cmd646_device::bm_prd_w<1>));
}

// In compatibility mode a channel decodes its ISA addresses whatever its BARs hold.  Its
// control block register is installed on its own, so that the floppy controller's registers
// at 0x3f4, 0x3f5 and 0x3f7 stay visible.
void cmd646_device::map_extra(u64 memory_window_start, u64 memory_window_end, u64 memory_offset, address_space *memory_space,
		u64 io_window_start, u64 io_window_end, u64 io_offset, address_space *io_space)
{
	if (!BIT(command, 0))
		return;

	if (!native_mode(0))
		install_compat_control<0>(*io_space, 0x3f6 + io_offset, io_window_start, io_window_end);
	if (!native_mode(1))
		install_compat_control<1>(*io_space, 0x376 + io_offset, io_window_start, io_window_end);
}

template <u32 N>
void cmd646_device::install_compat_control(address_space &space, u64 address, u64 window_start, u64 window_end)
{
	if ((address >= window_start) && (address <= window_end))
		space.install_readwrite_handler(address, address, read8smo_delegate(*this, FUNC(cmd646_device::cs1_r<N>)), write8smo_delegate(*this, FUNC(cmd646_device::cs1_w<N>)));
}

void cmd646_device::update_bars()
{
	static constexpr u32 COMPAT_COMMAND[2] = { 0x1f0, 0x170 };

	for (int channel = 0; channel < 2; channel++)
	{
		const int cmd = channel * 2;
		const int ctl = cmd + 1;
		if (native_mode(channel))
		{
			bank_infos[cmd].adr = m_bar[cmd];
			bank_infos[ctl].adr = m_bar[ctl];
			bank_infos[ctl].flags = M_IO;
		}
		else
		{
			bank_infos[cmd].adr = COMPAT_COMMAND[channel];
			bank_infos[ctl].flags = M_IO | M_DISABLED;
		}
	}
	remap_cb();
}

// The interrupt outputs follow the drives' INTRQ, gated by the MRDMODE block bits; the
// status bits latch an interrupt until software clears them.
template <u32 N>
void cmd646_device::ide_irq(int state)
{
	if (bool(state) == m_intrq[N])
		return;

	LOGIRQ("%s channel INTRQ %s at %s\n", N ? "secondary" : "primary", state ? "asserted" : "cleared", machine().time().as_string(6));
	m_intrq[N] = bool(state);
	if (state)
		m_intr_status |= 1 << N;
	update_irq();
}

void cmd646_device::update_irq()
{
	bool inta = false;
	bool compat[2] = { false, false };
	for (int channel = 0; channel < 2; channel++)
	{
		const bool active = m_intrq[channel] && !BIT(m_mrdmode, 4 + channel) && channel_enabled(channel);
		if (native_mode(channel))
			inta = inta || active;
		else
			compat[channel] = active;
	}

	if (inta != m_inta)
	{
		LOGIRQ("INTA# %s\n", inta ? "asserted" : "cleared");
		m_inta = inta;
		m_irq_cb(inta);
	}
	if (compat[0] != m_irq_compat[0])
	{
		m_irq_compat[0] = compat[0];
		m_irq_pri_cb(compat[0]);
	}
	if (compat[1] != m_irq_compat[1])
	{
		m_irq_compat[1] = compat[1];
		m_irq_sec_cb(compat[1]);
	}
}

void cmd646_device::prog_if_w(u8 data)
{
	const u32 writable = (BIT(pclass, 1) ? 0x01 : 0x00) | (BIT(pclass, 3) ? 0x04 : 0x00);
	const u32 old = pclass;
	pclass = (pclass & ~writable) | (data & writable);
	LOGMAP("%s: programming interface %02x\n", machine().describe_context(), pclass & 0xff);

	if ((old ^ pclass) & 0x05)
	{
		update_bars();
		update_irq();
	}
}

u32 cmd646_device::bar_r(offs_t offset)
{
	return (m_bar[offset] & ~(bank_infos[offset].size - 1)) | 1;
}

void cmd646_device::bar_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_bar[offset]);
	LOGMAP("%s: BAR%u = %08x\n", machine().describe_context(), offset, m_bar[offset]);

	if (native_mode(offset >> 1))
	{
		bank_infos[offset].adr = m_bar[offset];
		remap_cb();
	}
}

u8 cmd646_device::reg_r(offs_t offset)
{
	return m_regs[offset];
}

void cmd646_device::reg_w(offs_t offset, u8 data)
{
	LOGREG("%s: [%02x] = %02x\n", machine().describe_context(), offset + 0x40, data);
	m_regs[offset] = data;
}

u8 cmd646_device::cfr_r()
{
	return (m_cfr & ~0x04) | (BIT(m_intr_status, 0) ? 0x04 : 0x00);
}

void cmd646_device::cfr_w(u8 data)
{
	LOGREG("%s: CFR = %02x\n", machine().describe_context(), data);
	m_cfr = data & ~0x04;
	if (BIT(data, 2))
		m_intr_status &= ~0x01;
}

u8 cmd646_device::cntrl_r()
{
	return m_cntrl;
}

void cmd646_device::cntrl_w(u8 data)
{
	LOGREG("%s: CNTRL = %02x\n", machine().describe_context(), data);
	m_cntrl = data;
	update_irq();
}

u8 cmd646_device::arttim23_r()
{
	return (m_arttim23 & ~0x10) | (BIT(m_intr_status, 1) ? 0x10 : 0x00);
}

void cmd646_device::arttim23_w(u8 data)
{
	LOGREG("%s: ARTTIM23 = %02x\n", machine().describe_context(), data);
	m_arttim23 = data & ~0x10;
	if (BIT(data, 4))
		m_intr_status &= ~0x02;
}

u8 cmd646_device::mrdmode_r()
{
	return (m_mrdmode & 0xf0) | (m_intr_status << 2);
}

void cmd646_device::mrdmode_w(u8 data)
{
	LOGREG("%s: MRDMODE = %02x\n", machine().describe_context(), data);
	m_mrdmode = data & 0xf3;
	m_intr_status &= ~((data >> 2) & 0x03);
	update_irq();
}

// Byte 0 is the channel's command register and byte 2 its status register.  Byte 1 is MRDMODE
// in the primary channel's block and BMIDECSR in the secondary's; byte 3 is the channel's
// UDIDETCR.
template <u32 N>
u32 cmd646_device::bm_command_r(offs_t offset, u32 mem_mask)
{
	const u8 byte1 = N ? m_bmidecsr : mrdmode_r();
	return (m_ide[N]->bmdma_r(0, mem_mask) & 0x00ff'00ff) | (u32(byte1) << 8) | (u32(m_udidetcr[N]) << 24);
}

template <u32 N>
void cmd646_device::bm_command_w(offs_t offset, u32 data, u32 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		LOGATA("%s: %s bus master command %02x\n", machine().describe_context(), N ? "secondary" : "primary", u8(data));
	if (mem_mask & 0x00ff'00ff)
		m_ide[N]->bmdma_w(0, data, mem_mask & 0x00ff'00ff);

	if (ACCESSING_BITS_8_15)
	{
		if (N)
		{
			LOGREG("%s: BMIDECSR = %02x\n", machine().describe_context(), u8(data >> 8));
			m_bmidecsr = u8(data >> 8);
		}
		else
		{
			mrdmode_w(u8(data >> 8));
		}
	}

	if (ACCESSING_BITS_24_31)
	{
		LOGREG("%s: UDIDETCR%u = %02x\n", machine().describe_context(), N, u8(data >> 24));
		m_udidetcr[N] = u8(data >> 24);
	}
}

template <u32 N>
u32 cmd646_device::bm_prd_r()
{
	return m_ide[N]->bmdma_r(1);
}

// bus_master_ide_controller_device stores the whole descriptor table register on any write
template <u32 N>
void cmd646_device::bm_prd_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 prd = m_ide[N]->bmdma_r(1);
	COMBINE_DATA(&prd);
	LOGATA("%s: %s descriptor table %08x\n", machine().describe_context(), N ? "secondary" : "primary", prd);
	m_ide[N]->bmdma_w(1, prd);
}

// a disabled channel doesn't respond, so the bus floats high
template <u32 N>
u32 cmd646_device::cs0_r(offs_t offset, u32 mem_mask)
{
	const u32 data = channel_enabled(N) ? m_ide[N]->read_cs0(offset, mem_mask) : 0xffff'ffff;

	// every task file register except the data port
	if ((VERBOSE & LOG_TASKFILE) && !machine().side_effects_disabled())
	{
		for (int lane = 0; lane < 4; lane++)
		{
			const int reg = offset * 4 + lane;
			if (BIT(mem_mask, lane * 8, 8) && reg && !((reg == 1) && BIT(mem_mask, 0, 8)))
				LOGTASKFILE("%s: %s %s read %02x at %s\n", machine().describe_context(), N ? "secondary" : "primary",
						TASKFILE_READ_NAMES[reg], BIT(data, lane * 8, 8), machine().time().as_string(6));
		}
	}

	return data;
}

template <u32 N>
void cmd646_device::cs0_w(offs_t offset, u32 data, u32 mem_mask)
{
	if (VERBOSE & LOG_TASKFILE)
	{
		for (int lane = 0; lane < 4; lane++)
		{
			const int reg = offset * 4 + lane;
			if (BIT(mem_mask, lane * 8, 8) && (reg >= 1) && (reg <= 5) && !((reg == 1) && BIT(mem_mask, 0, 8)))
				LOGTASKFILE("%s: %s %s write %02x at %s\n", machine().describe_context(), N ? "secondary" : "primary",
						TASKFILE_WRITE_NAMES[reg], BIT(data, lane * 8, 8), machine().time().as_string(6));
		}
	}

	if (offset == 1)
	{
		if (ACCESSING_BITS_16_23)
			LOGATA("%s: %s device/head %02x\n", machine().describe_context(), N ? "secondary" : "primary", u8(data >> 16));
		if (ACCESSING_BITS_24_31)
			LOGATA("%s: %s command %02x at %s\n", machine().describe_context(), N ? "secondary" : "primary", u8(data >> 24), machine().time().as_string(6));
	}

	if (channel_enabled(N))
		m_ide[N]->write_cs0(offset, data, mem_mask);
}

template <u32 N>
u8 cmd646_device::cs1_r()
{
	const u8 data = channel_enabled(N) ? u8(m_ide[N]->read_cs1(1, 0x00ff'0000) >> 16) : 0xff;
	if (!machine().side_effects_disabled())
		LOGTASKFILE("%s: %s alternate status read %02x at %s\n", machine().describe_context(), N ? "secondary" : "primary", data, machine().time().as_string(6));
	return data;
}

template <u32 N>
void cmd646_device::cs1_w(u8 data)
{
	LOGATA("%s: %s device control %02x at %s\n", machine().describe_context(), N ? "secondary" : "primary", data, machine().time().as_string(6));
	if (channel_enabled(N))
		m_ide[N]->write_cs1(1, u32(data) << 16, 0x00ff'0000);
}
