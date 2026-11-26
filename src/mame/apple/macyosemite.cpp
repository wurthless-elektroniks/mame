// license:BSD-3-Clause
// copyright-holders:R. Belmont
/****************************************************************************

    macyosemite.cpp
    Power Macintosh G3 (Blue & White), "Yosemite" board
    Preliminary driver, adapted from imacg3.cpp

    A "New World" Gossamer-family machine.  The Power Mac G4 (PCI Graphics),
    "Yikes!", uses the same board with a G4 CPU.

    CPU: PowerPC 750 "G3" @ 300, 350, 400 or 450 MHz, 100 MHz system bus
    Memory controller/PCI host bridge: Motorola MPC106 "Grackle"
    PCI-PCI bridge: DEC 21154, secondary bus 1
    I/O: Paddington PCI I/O ASIC (see heathrow.cpp), behind the 21154
    USB: OPTi 82C861, behind the 21154
    ATA: Paddington ATA for the optical drive, CMD646 for the hard disks
    FireWire: TI PCILynx
    Audio: Burgundy
    Video: ATI Rage 128 card in the 66 MHz slot J12, plus three 33 MHz slots
           (J9-J11) behind the 21154
    RAM: 4 PC100 DIMM slots, max 1 GiB

    PCI layout, from an ioreg dump of a PowerMac1,2:

    bus 0  00.0  MPC106                               1057:0002
           0d.0  DEC 21154 PCI-PCI bridge             1011:0026 rev 02
           10.0  slot J12 (66 MHz), ATI Rage 128 GL   1002:5245      IRQ 22
    bus 1  00.0  TI PCILynx FireWire                  104c:8000      IRQ 21
           01.0  CMD646 ATA                           1095:0646 rev 07  IRQ 26
           02.0  slot J11                                            IRQ 23
           03.0  slot J10                                            IRQ 24
           04.0  slot J9                                             IRQ 25
           05.0  Paddington                           106b:0017
           06.0  OPTi 82C861 USB                      1045:c861 rev 10  IRQ 28

    TODO:
    - Boot ROM: only the Yikes! ROM (1.2f2, built 08/19/99) is dumped.  It
      reports the machine as PowerMac1,2, so Mac OS will think this is a
      Yikes!.  Add a genuine PowerMac1,1 ROM when one is dumped, and watch for
      G4-only SPR accesses from the Yikes! ROM on the 750.
    - The Rage 128 card ROM has only been checked structurally; Open Firmware
      hasn't been seen probing it under MAME yet.
    - TI PCILynx FireWire (pci:0d.0:00.0, IRQ 21) is a skeleton that reports
      an empty bus.
    - The generic PCI_BRIDGE reads the 21154's secondary status as 0xffff and
      doesn't implement the DEC-specific config registers at 0x40-0xff.
    - Each slot has one interrupt line shared by INTA#-INTD#; a card driving
      more than one pin needs a wired-OR here.
    - DIMM sizing: the iMac ROM probes each module through two Grackle banks;
      check whether this ROM does the same.
    - Verify the Cuda version and the DIMM SPD addresses on this board.

****************************************************************************/

#include "emu.h"

#include "burgundy.h"
#include "cuda.h"
#include "heathrow.h"

#include "bus/adb/adb.h"
#include "bus/adb/cards.h"
#include "bus/pci/opti82c861.h"
#include "bus/pci/pci_slot.h"
#include "bus/pci/pcilynx.h"
#include "bus/pci/rage128.h"
#include "cpu/powerpc/ppc.h"
#include "machine/cmd646.h"
#include "machine/dimm_spd.h"
#include "machine/input_merger.h"
#include "machine/mpc106.h"
#include "machine/pci.h"
#include "machine/ram.h"
#include "sound/cdda.h"

#include "softlist_dev.h"
#include "speaker.h"


namespace {

class yosemite_state : public driver_device
{
public:
	yosemite_state(const machine_config &mconfig, device_type type, const char *tag);

	void pwrmacg3bw(machine_config &config) ATTR_COLD;
	void pwrmacg3bw350(machine_config &config) ATTR_COLD;
	void pwrmacg3bw400(machine_config &config) ATTR_COLD;
	void pwrmacg3bw450(machine_config &config) ATTR_COLD;

private:
	static constexpr u32 BUS_CLOCK = 100'000'000;

	required_device<ppc_device> m_maincpu;
	required_device<pci_root_device> m_pci_root;
	required_device<mpc106_host_device> m_mpc106;
	required_device<paddington_device> m_paddington;
	required_device<cuda_device> m_cuda;
	required_device<adb_bus_device> m_adbbus;
	required_device_array<dimm_spd_device, 4> m_dimms;
	required_device<ram_device> m_ram;

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void yosemite_map(address_map &map) ATTR_COLD;

	void set_cpu_clock(u32 clock) ATTR_COLD;

	void cuda_reset_w(int state);
	void cuda_power_w(int state);

	// Cuda and the PPC poll each other's VIA handshake lines, so they have to
	// interleave tightly while they're talking: each edge or poll keeps the
	// scheduler at its finest quantum for this long.
	static constexpr attotime CUDA_BOOST = attotime::from_msec(10);
	enum cuda_source : unsigned { CUDA_TREQ_POLL, CUDA_BYTEACK, CUDA_TIP, CUDA_DATA_OUT, CUDA_CLOCK_IN, CUDA_DATA_IN, CUDA_RESET, CUDA_SOURCES };
	void cuda_activity(cuda_source source);

	// TODO: remove once the boost is tuned.  Logs once per emulated second what
	// triggered the Cuda boost and how much of the second it was in effect.
	static constexpr bool CUDA_BOOST_STATS = true;
	emu_timer *m_boost_stats_timer = nullptr;
	attotime m_boost_until, m_boost_covered;
	u32 m_boost_calls[CUDA_SOURCES] = { };
	TIMER_CALLBACK_MEMBER(boost_stats);

	void irq_w(int state) { m_maincpu->set_input_line(PPC_IRQ, state); }

	// slot interrupt pins are numbered with the Paddington IRQ they're wired to
	int map_pci_pin(int pin) { return pin; }
	void pci_irq_w(int line, int state);
};

yosemite_state::yosemite_state(const machine_config &mconfig, device_type type, const char *tag) :
	driver_device(mconfig, type, tag),
	m_maincpu(*this, "maincpu"),
	m_pci_root(*this, "pci"),
	m_mpc106(*this, "pci:00.0"),
	m_paddington(*this, "pci:0d.0:05.0"),
	m_cuda(*this, "cuda"),
	m_adbbus(*this, "adb"),
	m_dimms(*this, "dimm%u", 0U),
	m_ram(*this, RAM_TAG)
{
}

void yosemite_state::machine_start()
{
	if (CUDA_BOOST_STATS)
	{
		m_boost_stats_timer = timer_alloc(FUNC(yosemite_state::boost_stats), this);
		m_boost_stats_timer->adjust(attotime::from_seconds(1), 0, attotime::from_seconds(1));
	}

	m_mpc106->set_ram_info((u8 *)m_ram->pointer(), m_ram->size());

	// fill the DIMM slots largest module first; SPD only describes up to 256 MiB per module
	static constexpr std::pair<u32, dimm_spd_device::dimm_size_t> SIZES[] =
	{
		{ 256, dimm_spd_device::SIZE_256_MIB },
		{ 128, dimm_spd_device::SIZE_128_MIB },
		{  64, dimm_spd_device::SIZE_64_MIB },
		{  32, dimm_spd_device::SIZE_32_MIB }
	};

	u32 remaining = m_ram->size() >> 20;
	for (auto &dimm : m_dimms)
	{
		dimm->set_dimm_size(dimm_spd_device::SIZE_SLOT_EMPTY);
		for (auto const &[mib, size] : SIZES)
		{
			if (remaining >= mib)
			{
				dimm->set_dimm_size(size);
				remaining -= mib;
				break;
			}
		}
	}
}

void yosemite_state::machine_reset()
{
	// the PPC can't be allowed to run until Cuda's ready
	m_maincpu->set_input_line(INPUT_LINE_HALT, ASSERT_LINE);
}

void yosemite_state::yosemite_map(address_map &map)
{
	map.unmap_value_high();
}

// Every model runs the same 100 MHz bus; only the CPU multiplier changes.
void yosemite_state::set_cpu_clock(u32 clock)
{
	m_maincpu->set_clock(clock);
}

void yosemite_state::cuda_reset_w(int state)
{
	m_maincpu->set_input_line(INPUT_LINE_HALT, state);
	m_maincpu->set_input_line(INPUT_LINE_RESET, state);
	cuda_activity(CUDA_RESET);
}

void yosemite_state::cuda_activity(cuda_source source)
{
	if (machine().side_effects_disabled())
		return;
	machine().scheduler().perfect_quantum(CUDA_BOOST);

	// let Cuda see the PPC's change now rather than at the end of its timeslice
	if ((source == CUDA_BYTEACK) || (source == CUDA_TIP) || (source == CUDA_DATA_OUT))
		m_maincpu->abort_timeslice();

	if (CUDA_BOOST_STATS)
	{
		const attotime now = machine().time();
		const attotime until = now + CUDA_BOOST;
		m_boost_covered += (now >= m_boost_until) ? CUDA_BOOST : (until - m_boost_until);
		m_boost_until = until;
		m_boost_calls[source]++;
	}
}

TIMER_CALLBACK_MEMBER(yosemite_state::boost_stats)
{
	logerror("Cuda boost: %3.0f%% of the last second; TREQ polls %u, BYTEACK %u, TIP %u, data out %u, clock in %u, data in %u, reset %u\n",
			std::min(m_boost_covered.as_double(), 1.0) * 100.0,
			m_boost_calls[CUDA_TREQ_POLL], m_boost_calls[CUDA_BYTEACK], m_boost_calls[CUDA_TIP], m_boost_calls[CUDA_DATA_OUT],
			m_boost_calls[CUDA_CLOCK_IN], m_boost_calls[CUDA_DATA_IN], m_boost_calls[CUDA_RESET]);
	m_boost_covered = attotime::zero;
	std::fill(std::begin(m_boost_calls), std::end(m_boost_calls), 0);
}

void yosemite_state::cuda_power_w(int state)
{
	if (state)
	{
		machine().schedule_exit();
	}
}

void yosemite_state::pci_irq_w(int line, int state)
{
	switch (line)
	{
		case 21: m_paddington->set_irq_line<21>(state); break;  // FireWire
		case 22: m_paddington->set_irq_line<22>(state); break;  // J12
		case 23: m_paddington->set_irq_line<23>(state); break;  // J11
		case 24: m_paddington->set_irq_line<24>(state); break;  // J10
		case 25: m_paddington->set_irq_line<25>(state); break;  // J9
		case 28: m_paddington->set_irq_line<28>(state); break;  // USB
	}
}

// The 66 MHz slot J12, which normally holds the graphics card.
// MAME's generic pci_cards list isn't used: some of those cards hardcode x86 system details
// (see maccatalyst.cpp).  A PC card also has no Open Firmware FCode, so the Mac firmware
// won't drive it; only Mac editions with an FCode ROM are useful here.
static void yosemite_gfx_cards(device_slot_interface &device)
{
	// ATI Rage 128 GL (1002:5245, ATY,Rage128y), the stock card.  The boot ROM has no
	// Rage 128 driver: the card's FCode ROM (113-57407-204) builds the display node and
	// carries the Mac OS ndrv.
	device.option_add("rage128gl_mac", ATI_RAGE128_GL_PCI_MAC);

	// TODO: 3dfx Voodoo cards.  voodoo_pci.cpp's devices derive from pci_device, not
	// pci_card_device, so they can't sit in a slot yet, and each needs a Mac FCode ROM
	// (3dfx shipped Mac editions of the Voodoo3).
	// device.option_add("voodoo3", VOODOO_3_PCI);
}

// the three 33 MHz slots behind the 21154
static void yosemite_pci_cards(device_slot_interface &device)
{
	// TODO: the same graphics cards as J12 also fit here, plus Mac SCSI/network cards
	// once they're shown to pass validation on a non-x86 machine
}

void yosemite_state::pwrmacg3bw(machine_config &config)
{
	PPC750(config, m_maincpu, 300'000'000);
	m_maincpu->ppcdrc_set_options(PPCDRC_COMPATIBLE_OPTIONS | PPCDRC_MACOS_CACHE_HACK);
	m_maincpu->set_bus_frequency(BUS_CLOCK);
	m_maincpu->set_tb_divisor(4); // the timebase counts bus clocks divided by 4
	m_maincpu->set_addrmap(AS_PROGRAM, &yosemite_state::yosemite_map);
	set_cpu_clock(300'000'000);

	PCI_ROOT(config, m_pci_root, 0);
	m_pci_root->set_pin_mapper(pci_pin_mapper(*this, FUNC(yosemite_state::map_pci_pin)));
	m_pci_root->set_irq_handler(pci_irq_handler(*this, FUNC(yosemite_state::pci_irq_w)));

	MPC106(config, m_mpc106, mpc106_host_device::MAP_TYPE_B, "maincpu", "bootrom");

	// 66 MHz graphics slot on the host bus
	PCI_SLOT(config, "pci:j12", yosemite_gfx_cards, 0x10, 22, 22, 22, 22, "rage128gl_mac");

	// DEC 21154 bridge to the secondary bus
	PCI_BRIDGE(config, "pci:0d.0", 0, 0x10110026, 0x02);

	// TI_PCILYNX(config, "pci:0d.0:00.0").set_irq_map(21);
	// CMD646U2 in native mode: hard disk on the primary channel, nothing else fitted.  Bus
	// master DMA goes through Grackle's PCI memory space (its space 1), where it maps system RAM.
	cmd646_device &cmd646(CMD646(config, "pci:0d.0:01.0", 0, 0x07, m_mpc106, AS_DATA));
	cmd646.set_pif(0x8f);
	cmd646.irq_handler().set(m_paddington, FUNC(heathrow_device::set_irq_line<26>));

	PCI_SLOT(config, "pci:0d.0:j11", yosemite_pci_cards, 2, 23, 23, 23, 23, nullptr);
	PCI_SLOT(config, "pci:0d.0:j10", yosemite_pci_cards, 3, 24, 24, 24, 24, nullptr);
	PCI_SLOT(config, "pci:0d.0:j9", yosemite_pci_cards, 4, 25, 25, 25, 25, nullptr);

	PADDINGTON(config, m_paddington);
	m_paddington->set_maincpu_tag("maincpu");
	m_paddington->irq_callback().set(FUNC(yosemite_state::irq_w));

	// Paddington's first ATA bus has the optical drive; the hard disks are on the CMD646
	m_paddington->ata(0).slot(0).set_default_option("cdrom");
	m_paddington->ata(0).slot(0).set_option_machine_config("cdrom", [](device_t *device)
	{
		device->subdevice<cdda_device>("cdda")->add_route(0, ":speaker", 1.0, 0);
		device->subdevice<cdda_device>("cdda")->add_route(1, ":speaker", 1.0, 1);
	});

	OPTI_82C861(config, "pci:0d.0:06.0", 0).set_irq_map(28);

	ADB_BUS(config, m_adbbus);
	ADB_CONNECTOR(config, "adb:0", adb_devices, "hle_keyboard");
	ADB_CONNECTOR(config, "adb:1", adb_devices, "hle_mouse");

	CUDA_V2XX(config, m_cuda, XTAL(32'768));
	m_cuda->set_default_bios_tag("341s0060");
	m_cuda->reset_callback().set(FUNC(yosemite_state::cuda_reset_w));
	m_cuda->poweroff_callback().set(FUNC(yosemite_state::cuda_power_w));
	m_cuda->linechange_callback().set(m_adbbus, FUNC(adb_bus_device::adb_host_line_w));
	m_cuda->via_clock_callback().set([this] (int state) { cuda_activity(CUDA_CLOCK_IN); m_paddington->cb1_w(state); });
	m_cuda->via_data_callback().set([this] (int state) { cuda_activity(CUDA_DATA_IN); m_paddington->cb2_w(state); });
	m_adbbus->out_adb_callback().set(m_cuda, FUNC(cuda_device::set_adb_line));
	m_adbbus->out_poweron_callback().set(m_cuda, FUNC(cuda_device::set_adb_power));
	// Perfect quantum only while Cuda and the PPC are exchanging data (see
	// cuda_activity); permanently it costs most of the PPC recompiler's speed.

	m_paddington->pb3_callback().set([this] () { cuda_activity(CUDA_TREQ_POLL); return int(m_cuda->get_treq()); });
	m_paddington->pb4_callback().set([this] (int state) { m_cuda->set_byteack(state); cuda_activity(CUDA_BYTEACK); });
	m_paddington->pb5_callback().set([this] (int state) { m_cuda->set_tip(state); cuda_activity(CUDA_TIP); });
	m_paddington->cb2_callback().set([this] (int state) { m_cuda->set_via_data(state); cuda_activity(CUDA_DATA_OUT); });

	// ALL_HIGH logically ANDs all sources, which is what we want for I2C/SMBus
	input_merger_device &sda_merger(INPUT_MERGER_ALL_HIGH(config, "sda"));
	sda_merger.output_handler().append(m_cuda, FUNC(cuda_device::set_iic_sda));
	m_cuda->iic_sda_callback().set(sda_merger, FUNC(input_merger_device::in_w<0>));

	DIMM_SPD(config, m_dimms[0]).set_address(0x50);
	DIMM_SPD(config, m_dimms[1]).set_address(0x51);
	DIMM_SPD(config, m_dimms[2]).set_address(0x52);
	DIMM_SPD(config, m_dimms[3]).set_address(0x53);

	m_cuda->iic_sda_callback().append(m_dimms[0], FUNC(dimm_spd_device::sda_write));
	m_cuda->iic_sda_callback().append(m_dimms[1], FUNC(dimm_spd_device::sda_write));
	m_cuda->iic_sda_callback().append(m_dimms[2], FUNC(dimm_spd_device::sda_write));
	m_cuda->iic_sda_callback().append(m_dimms[3], FUNC(dimm_spd_device::sda_write));
	m_cuda->iic_scl_callback().set(m_dimms[0], FUNC(dimm_spd_device::scl_write));
	m_cuda->iic_scl_callback().append(m_dimms[1], FUNC(dimm_spd_device::scl_write));
	m_cuda->iic_scl_callback().append(m_dimms[2], FUNC(dimm_spd_device::scl_write));
	m_cuda->iic_scl_callback().append(m_dimms[3], FUNC(dimm_spd_device::scl_write));
	m_dimms[0]->sda_callback().set(sda_merger, FUNC(input_merger_device::in_w<1>));
	m_dimms[1]->sda_callback().set(sda_merger, FUNC(input_merger_device::in_w<2>));
	m_dimms[2]->sda_callback().set(sda_merger, FUNC(input_merger_device::in_w<3>));
	m_dimms[3]->sda_callback().set(sda_merger, FUNC(input_merger_device::in_w<4>));

	RAM(config, m_ram);
	m_ram->set_default_size("128M");
	m_ram->set_extra_options("64M,256M,512M,768M,1G");

	burgundy_device &burgundy(BURGUNDY(config, "codec", 45.1584_MHz_XTAL));
	burgundy.dma_output().set(m_paddington, FUNC(heathrow_device::codec_dma_read));
	burgundy.dma_input().set(m_paddington, FUNC(heathrow_device::codec_dma_write));

	m_paddington->codec_r_callback().set(burgundy, FUNC(burgundy_device::read_macrisc));
	m_paddington->codec_w_callback().set(burgundy, FUNC(burgundy_device::write_macrisc));

	SPEAKER(config, "speaker", 2).front();
	burgundy.add_route(0, "speaker", 1.0, 0);
	burgundy.add_route(1, "speaker", 1.0, 1);

	SOFTWARE_LIST(config, "hdd_list").set_original("mac_hdd");
	SOFTWARE_LIST(config, "cd_list").set_original("mac_cdrom");
}

void yosemite_state::pwrmacg3bw350(machine_config &config)
{
	pwrmacg3bw(config);
	set_cpu_clock(350'000'000);
}

void yosemite_state::pwrmacg3bw400(machine_config &config)
{
	pwrmacg3bw(config);
	set_cpu_clock(400'000'000);
}

void yosemite_state::pwrmacg3bw450(machine_config &config)
{
	pwrmacg3bw(config);
	set_cpu_clock(450'000'000);
}

// Power Mac G4 (PCI Graphics) "Yikes!" ROM, which reports PowerMac1,2.  Same board.
#define YOSEMITE_BOOTROM \
	ROM_REGION(0x100000, "bootrom", ROMREGION_64BIT | ROMREGION_BE) \
	ROM_LOAD( "powermac12_1.2f2.bin", 0x000000, 0x100000, CRC(35be26bf) SHA1(d171d00177820d1a797e385b2071a36261c8537a) )

ROM_START(pwrmacg3bw)
	YOSEMITE_BOOTROM
ROM_END

ROM_START(pwrmacg3bw350)
	YOSEMITE_BOOTROM
ROM_END

ROM_START(pwrmacg3bw400)
	YOSEMITE_BOOTROM
ROM_END

ROM_START(pwrmacg3bw450)
	YOSEMITE_BOOTROM
ROM_END

static INPUT_PORTS_START(pwrmacg3bw)
INPUT_PORTS_END

} // anonymous namespace


//    YEAR  NAME           PARENT      COMPAT  MACHINE        INPUT       CLASS           INIT        COMPANY           FULLNAME                                       FLAGS
COMP( 1999, pwrmacg3bw,    0,          0,      pwrmacg3bw,    pwrmacg3bw, yosemite_state, empty_init, "Apple Computer", "Power Macintosh G3 (Blue & White, 300 MHz)", MACHINE_NOT_WORKING )
COMP( 1999, pwrmacg3bw350, pwrmacg3bw, 0,      pwrmacg3bw350, pwrmacg3bw, yosemite_state, empty_init, "Apple Computer", "Power Macintosh G3 (Blue & White, 350 MHz)", MACHINE_NOT_WORKING )
COMP( 1999, pwrmacg3bw400, pwrmacg3bw, 0,      pwrmacg3bw400, pwrmacg3bw, yosemite_state, empty_init, "Apple Computer", "Power Macintosh G3 (Blue & White, 400 MHz)", MACHINE_NOT_WORKING )
COMP( 1999, pwrmacg3bw450, pwrmacg3bw, 0,      pwrmacg3bw450, pwrmacg3bw, yosemite_state, empty_init, "Apple Computer", "Power Macintosh G3 (Blue & White, 450 MHz)", MACHINE_NOT_WORKING )
