
#ifndef MAME_MACHINE_DEC21154_H
#define MAME_MACHINE_DEC21154_H

#pragma once

class dec21154_device : public pci_bridge_device
{
public:
    dec21154_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
    dec21154_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

    virtual void device_start() override ATTR_COLD;
    virtual void device_reset() override ATTR_COLD;
    
private:

};


DECLARE_DEVICE_TYPE(DEC21154, dec21154_device)

#endif // MAME_MACHINE_DEC21154_H
