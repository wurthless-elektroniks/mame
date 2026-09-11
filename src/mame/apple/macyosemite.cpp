/****************************************************************************

    Power Macintosh G3 Blue & White / Power Mac G4 PCI Graphics "Yosemite"/"Yikes!" hardware

    Partly inspired by the DingusPPC Yosemite driver

    G3 B&W identifies as PowerMac1,1
    G4 Yikes identifies as PowerMac1,2

    The G3 B&W is essentially the workstation version of the iMac G3, complete with
    much of the same chipset and a friendly candy-colored plastic case.
    Unlike the iMac and Gossamer G3s, there is no integrated graphics card;
    it is instead moved to a dedicated PCI slot, and by default is an ATI Rage 128.

    The "Yikes!" machines were part of Apple's mad dash to get the G4 machines to market
    and were Yosemite boards with a G4 processor card, but without an ADB port.
    Further headaches arose when Apple could not deliver on their promised CPU speeds
    that customers had pre-ordered. Both the Yikes! and Yosemite machines were discontinued by the
    end of 1999, and the Sawtooth machines eventually shipped with their promised clock rates.

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
    | 0d:06   | '-> usb@6             | OPTi 82C861 FireLink PCI-USB Host Bridge
    | 10      | /pci/ATY,Rage128y@10  | PCI 32-bit graphics card slot (ATI Rage 128 on G4 Yikes)

****************************************************************************/


