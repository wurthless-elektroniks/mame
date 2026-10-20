/**************************************************************************************************

    DEC 21154 PCI-PCI bridge

**************************************************************************************************/

#include "emu.h"
#include "pci.h"
#include "dec21154.h"

DEFINE_DEVICE_TYPE(DEC21154, dec21154_device, "dec21554", "DEC 21154 PCI-PCI Bridge");

dec21154_device::dec21154_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock):
	pci_bridge_device(mconfig, tag, owner, clock)
{
	dec21154_device(mconfig, DEC21154, tag, owner, clock);
}

dec21154_device::dec21154_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock) :
	pci_bridge_device(mconfig, tag, owner, clock)
{
	set_ids_bridge(0x10110026, 0x00);
}

void dec21154_device::device_start()
{
}

void dec21154_device::device_reset()
{
}
