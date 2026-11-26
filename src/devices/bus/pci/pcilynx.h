// license:BSD-3-Clause
// copyright-holders:<author>
#ifndef MAME_BUS_PCI_PCILYNX_H
#define MAME_BUS_PCI_PCILYNX_H

#pragma once

#include "pci_slot.h"

#include <array>


class ti_pcilynx_device : public pci_card_device
{
public:
	// the 1394 bus is only modelled far enough to show that nothing is connected
	static constexpr feature_type unemulated_features() { return feature::COMMS; }

	ti_pcilynx_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// pci_device implementation
	virtual void config_map(address_map &map) override ATTR_COLD;
	virtual uint8_t cache_line_size_r() override;
	virtual uint8_t latency_timer_r() override;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	struct dma_channel
	{
		u32 prev_pcl = 0;
		u32 current_pcl = 0;
		u32 data_buf = 0;
		u32 status = 0;
		u32 control = 0;
		u32 ready = 0;
		u32 cmp[4] = { };       // word 0 value/enable, word 1 value/enable
	};

	static constexpr int DMA_CHANNELS = 5;

	void regs_map(address_map &map) ATTR_COLD;
	void misc_map(address_map &map) ATTR_COLD;
	void ram_map(address_map &map) ATTR_COLD;
	void aux_map(address_map &map) ATTR_COLD;

	u32 header_mirror_r(offs_t offset);
	void misc_control_w(offs_t offset, u32 data, u32 mem_mask);
	u32 eeprom_control_r();
	void eeprom_control_w(offs_t offset, u32 data, u32 mem_mask);
	u32 pci_int_status_r();
	void pci_int_status_w(offs_t offset, u32 data, u32 mem_mask);
	u32 pci_int_enable_r();
	void pci_int_enable_w(offs_t offset, u32 data, u32 mem_mask);
	void link_control_w(offs_t offset, u32 data, u32 mem_mask);
	u32 cycle_timer_r();
	void cycle_timer_w(offs_t offset, u32 data, u32 mem_mask);
	void link_phy_w(offs_t offset, u32 data, u32 mem_mask);
	void link_int_status_w(offs_t offset, u32 data, u32 mem_mask);
	void link_int_enable_w(offs_t offset, u32 data, u32 mem_mask);

	void soft_reset();

	// interrupts
	void update_irq();
	void link_int(u32 bits);

	// PHY
	u8 phy_r(int reg);
	void phy_w(int reg, u8 data);
	void bus_reset(bool initiated, bool short_reset);
	u32 own_self_id() const;
	TIMER_CALLBACK_MEMBER(self_id_done);

	// DMA
	void dma_control_w(int ch, u32 data);
	void dma_halt(int ch);
	bool dma_receive(int ch, const u32 *quadlets, u32 count, u32 status);
	u32 bm_read32(u32 addr);
	void bm_write32(u32 addr, u32 data);
	void bm_write8(u32 addr, u8 data);

	emu_timer *m_self_id_timer;

	u8 m_cache_line_size;
	u8 m_latency_timer;

	u32 m_misc_control;
	u32 m_eeprom_control;
	u32 m_pci_int_status;
	u32 m_pci_int_enable;
	u32 m_misc_regs[0xb0 / 4];  // 0x50-0xff: subsystem access, local bus, GPIO

	std::array<dma_channel, DMA_CHANNELS> m_dma;
	u32 m_dma_global;
	u32 m_fifo_sizes;
	u32 m_fifo_control;
	u32 m_fifo_threshold;

	u32 m_link_id;
	u32 m_link_control;
	u32 m_link_phy;
	u32 m_diag;
	u32 m_link_int_status;
	u32 m_link_int_enable;
	u32 m_cycle_timer;          // value at m_cycle_timer_time
	attotime m_cycle_timer_time;

	u8 m_phy_regs[8];           // base PHY registers; the paged registers are generated
	bool m_phy_initiated_reset;
};

DECLARE_DEVICE_TYPE(TI_PCILYNX, ti_pcilynx_device)

#endif // MAME_BUS_PCI_PCILYNX_H
