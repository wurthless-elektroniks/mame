/****************************************************************************

    Apple "Yosemite" hardware, based on the New World Gossamer architecture

    Systems:
    Power Macintosh G3 Blue & White ("Yosemite"), PowerMac1,1
    Power Mac G4 PCI Graphics ("Yikes!"), PowerMac1,2

    The G3 B&W is essentially the tower version of the iMac G3, complete with
    much of the same chipset and a friendly candy-colored plastic case.
    Unlike the iMac and beige G3s, there is no integrated graphics card;
    it is instead moved to a dedicated PCI slot, and by default is an ATI Rage 128.

    The "Yikes!" machines were part of Apple's mad dash to get the G4 machines to market
    and were Yosemite boards with a G4 processor card, but without an ADB port.
    Further headaches arose when Apple could not deliver on their promised CPU speeds
    that customers had pre-ordered. Both the Yikes! and Yosemite machines were discontinued by the
    end of 1999, and the Sawtooth machines eventually shipped with their promised clock rates.

    These machines were overall the last of the Gossamer machines, with the Sawtooth
    heralding the arrival of the Core99 platform, based around the fancy new UniNorth
    northbridge.

    The B&W machines will not work with a G4 processor card because Apple deliberately
    blocked it in firmware to force people to buy new machines. But a G3 CPU will work
    on both machines, and that's good news for us, because we can start emulating the
    hardware now and worry about AltiVec and AGP graphics later.

    PCI device tree from a real Yikes!:
   
    | Address | Open Firmware path    | Device
    |---------|-----------------------|----------------------------------------------
    | 0d      | /pci/pci-bridge@d     | DEC 21154 PCI-to-PCI bridge
    | 0d:00   | |-> firewire@0        | Texas Instruments PCILynx IEEE-1394 Firewire Host Controller
    | 0d:01   | |-> pci-ata@1         | Silicon Image, Inc. PCI-0646 UltraDMA IDE Controller
    | 0d:02   | |-> ...various...     | PCI-X 64-bit slot at J11
    | 0d:03   | |-> ...various...     | PCI-X 64-bit slot at J10
    | 0d:04   | |-> ...various...     | PCI-X 64-bit slot at J9
    | 0d:05   | |-> mac-io@5          | Apple 343S1211 "Paddington" I/O ASIC
    | 0d:06   | '-> usb@6             | OPTi 82C861 FireLink PCI-USB Host Bridge, same as on the trayloading iMacs
    | 10      | /pci/ATY,Rage128y@10  | PCI 32-bit graphics card slot (normally an ATI Rage 128)

    Overclocking:
    ------------------------------------------------------------------
    
    The Yosemite board has a jumper block near the CPU that configures the
    Grackle's clock speeds and multipliers. It is covered under a conspicuous
    "Warranty void if removed" label. The configuration lines are pulled up
    by resistors on the motherboard; the jumpers simply ground those lines.
    Ideally we would implement these as dipswitches, but that means having
    to add all the dipswitch configurations to the other Gossamer-based Macs.

    The G3 B&Ws shipped stock in 350, 400 or 450 MHz configurations.
    The G4 Yikes only shipped with 350 or 400 MHz; a 450 MHz Yikes was advertised
    but never released. The 400 MHz Yikes overclocks quite well to 450 MHz, though,
    so we might as well include the 450 MHz configuration.

****************************************************************************/

#include "emu.h"

#include "bus/adb/adb.h"
#include "bus/adb/cards.h"
#include "bus/pci/opti82c861.h"
#include "cpu/powerpc/ppc.h"
#include "machine/dec21154.h"
#include "machine/dimm_spd.h"
#include "machine/i2cmem.h"
#include "machine/input_merger.h"
#include "machine/mpc106.h"
#include "machine/pci.h"
#include "machine/ram.h"
#include "sound/cdda.h"
#include "video/atirage.h"

#include "burgundy.h"
#include "cuda.h"
#include "heathrow.h"
#include "softlist.h"
#include "softlist_dev.h"

class macyosemite_state : public driver_device
{
public:
	macyosemite_state(const machine_config &mconfig, device_type type, const char *tag);


    void yosemite_base(machine_config &config);
  

private:
	required_device<ppc750_device> m_maincpu; // G4 7400 with CPU version 0x0205 on Yikes
	required_device<mpc106_host_device> m_mpc106;
    required_device<dec21154_device> m_dec21154;

	required_device<cuda_device> m_cuda; // 341S0262
	required_device<ram_device> m_ram; // up to 2 Gbytes
	required_device<dimm_spd_device> m_dimm0, m_dimm1, m_dimm2, m_dimm3;
};


void macyosemite_state::yosemite_base(machine_config &config)
{
	PPC750(config, m_maincpu, 400'000'000);
	m_maincpu->ppcdrc_set_options(PPCDRC_COMPATIBLE_OPTIONS | PPCDRC_MACOS_CACHE_HACK);
	m_maincpu->set_bus_frequency(100'000'000);
	m_maincpu->set_tb_divisor(14); // probably wrong
	// m_maincpu->set_addrmap(AS_PROGRAM, &imac_state::imac_map);

	PCI_ROOT(config, "pci");
	MPC106(config, m_mpc106, mpc106_host_device::MAP_TYPE_B, "maincpu", "bootrom");

    DEC21154(config, "pci:0d.0", 0 );
    
	paddington_device &paddington(PADDINGTON(config, "pci:0d.0:5"));
	paddington.set_maincpu_tag("maincpu");

	OPTI_82C861(config, "pci:0d.0:6", 0);
}



ROM_START( pmacg4pci )
	ROM_REGION64_BE(0x100000, "bootrom", 0)
ROM_END



COMP( 1999, pmacg4pci, pmac8100, 0,      pmac8100_100,  macpdm, macpdm_state, driver_init, "Apple Computer", "Power Macintosh G4 (PCI Graphics, 400 MHz)", MACHINE_NOT_WORKING )
