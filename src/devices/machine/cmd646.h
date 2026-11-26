// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    cmd646.h

    CMD Technology PCI0646 PCI-IDE controller

***************************************************************************/

#ifndef MAME_MACHINE_CMD646_H
#define MAME_MACHINE_CMD646_H

#pragma once

#include "idectrl.h"
#include "pci.h"

#include <utility>


class cmd646_device : public pci_device
{
public:
	// bmtag and bmspace name the address space that bus master DMA reads and writes
	template <typename T>
	cmd646_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock, u8 revision, T &&bmtag, int bmspace) :
		cmd646_device(mconfig, tag, owner, clock)
	{
		set_ids(0x1095'0646, revision, 0x01'01'8a, 0x0000'0000);
		m_bus_master_space.set_tag(std::forward<T>(bmtag), bmspace);
	}

	cmd646_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// INTA# for channels in native mode; IRQ14 and IRQ15 for channels in compatibility mode
	auto irq_handler() { return m_irq_cb.bind(); }
	auto irq_pri() { return m_irq_pri_cb.bind(); }
	auto irq_sec() { return m_irq_sec_cb.bind(); }

	// programming interface at reset: bits 1 and 3 make bits 0 and 2 writable
	void set_pif(u8 pif) { m_reset_pif = pif; }

	// pci_device implementation
	virtual void config_map(address_map &map) override ATTR_COLD;
	virtual void map_extra(u64 memory_window_start, u64 memory_window_end, u64 memory_offset, address_space *memory_space,
			u64 io_window_start, u64 io_window_end, u64 io_offset, address_space *io_space) override;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	required_device_array<bus_master_ide_controller_device, 2> m_ide;
	required_address_space m_bus_master_space;
	devcb_write_line m_irq_cb;
	devcb_write_line m_irq_pri_cb;
	devcb_write_line m_irq_sec_cb;

	u8 m_reset_pif;

	u32 m_bar[4]; // BAR0-BAR3 as written, still readable in compatibility mode
	u8 m_regs[0x20]; // 0x40-0x5f
	u8 m_cfr;
	u8 m_cntrl;
	u8 m_arttim23;
	u8 m_mrdmode;
	u8 m_bmidecsr;
	u8 m_udidetcr[2];
	u8 m_intr_status; // latched channel interrupts: bit 0 primary, bit 1 secondary
	bool m_intrq[2];
	bool m_inta;
	bool m_irq_compat[2];

	void primary_command_map(address_map &map) ATTR_COLD;
	void primary_control_map(address_map &map) ATTR_COLD;
	void secondary_command_map(address_map &map) ATTR_COLD;
	void secondary_control_map(address_map &map) ATTR_COLD;
	void bus_master_map(address_map &map) ATTR_COLD;

	bool native_mode(int channel) const { return BIT(pclass, channel * 2); }
	bool channel_enabled(int channel) const { return !channel || BIT(m_cntrl, 3); }
	void update_bars();
	void update_irq();
	template <u32 N> void install_compat_control(address_space &space, u64 address, u64 window_start, u64 window_end);

	template <u32 N> void ide_irq(int state);

	void prog_if_w(u8 data);
	u32 bar_r(offs_t offset);
	void bar_w(offs_t offset, u32 data, u32 mem_mask = ~0);
	u8 reg_r(offs_t offset);
	void reg_w(offs_t offset, u8 data);
	u8 cfr_r();
	void cfr_w(u8 data);
	u8 cntrl_r();
	void cntrl_w(u8 data);
	u8 arttim23_r();
	void arttim23_w(u8 data);
	u8 mrdmode_r();
	void mrdmode_w(u8 data);

	template <u32 N> u32 bm_command_r(offs_t offset, u32 mem_mask = ~0);
	template <u32 N> void bm_command_w(offs_t offset, u32 data, u32 mem_mask = ~0);
	template <u32 N> u32 bm_prd_r();
	template <u32 N> void bm_prd_w(offs_t offset, u32 data, u32 mem_mask = ~0);

	template <u32 N> u32 cs0_r(offs_t offset, u32 mem_mask = ~0);
	template <u32 N> void cs0_w(offs_t offset, u32 data, u32 mem_mask = ~0);
	template <u32 N> u8 cs1_r();
	template <u32 N> void cs1_w(u8 data);
};

DECLARE_DEVICE_TYPE(CMD646, cmd646_device)

#endif // MAME_MACHINE_CMD646_H
