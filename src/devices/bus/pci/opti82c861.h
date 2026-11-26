// license:BSD-3-Clause
// copyright-holders: R. Belmont

#ifndef MAME_BUS_PCI_OPTI82C861_H
#define MAME_BUS_PCI_OPTI82C861_H

#pragma once

#include "pci_slot.h"

class opti_82c861_device : public pci_card_device
{
public:
	opti_82c861_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
	opti_82c861_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void mem_map(address_map &map) ATTR_COLD;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	void map_extra(u64 memory_window_start, u64 memory_window_end, u64 memory_offset, address_space *memory_space,
				   u64 io_window_start, u64 io_window_end, u64 io_offset, address_space *io_space) override;
	void config_map(address_map &map) override;

private:
	static constexpr u32 NUM_PORTS = 2;

	emu_timer *m_frame_timer;

	u32 m_HcRhDescriptorA = 0;
	u32 m_HcControl;
	u32 m_HcCommandStatus;
	u32 m_HcInterruptStatus;
	u32 m_HcInterruptEnable;
	u32 m_HcHCCA;
	u32 m_HcControlHeadED;
	u32 m_HcControlCurrentED;
	u32 m_HcBulkHeadED;
	u32 m_HcBulkCurrentED;
	u32 m_HcFmInterval;
	u16 m_HcFmNumber;
	bool m_frame_remaining_toggle;
	u32 m_HcPeriodicStart;
	u32 m_HcLSThreshold;
	u32 m_HcRhDescriptorB;
	u32 m_HcRhPortStatus[NUM_PORTS];

	void reset_registers(bool hardware);
	bool operational() const { return BIT(m_HcControl, 6, 2) == 2; }
	attotime frame_period() const;
	void update_irq();
	TIMER_CALLBACK_MEMBER(frame_tick);

	void control_w(offs_t offset, u32 data, u32 mem_mask);
	void command_status_w(offs_t offset, u32 data, u32 mem_mask);
	void interrupt_status_w(offs_t offset, u32 data, u32 mem_mask);
	void interrupt_enable_w(offs_t offset, u32 data, u32 mem_mask);
	void interrupt_disable_w(offs_t offset, u32 data, u32 mem_mask);
	u32 fm_remaining_r();
	template <u32 N> void port_status_w(offs_t offset, u32 data, u32 mem_mask);
};

DECLARE_DEVICE_TYPE(OPTI_82C861, opti_82c861_device)

#endif
