/*
 * NEC PCI USB controllers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci/pci_device.h"
#include "hw/usb/nec-usb.h"
#include "qemu/module.h"

#define TYPE_PCI_OHCI "pci-ohci"
#define TYPE_PCI_EHCI "pci-ehci-usb"

#define PCI_DEVICE_ID_NEC_USB_OHCI 0x0035
#define PCI_DEVICE_ID_NEC_USB_EHCI 0x00e0

static void nec_usb_ohci_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    (void)data;
    pc->vendor_id = PCI_VENDOR_ID_NEC;
    pc->device_id = PCI_DEVICE_ID_NEC_USB_OHCI;
    pc->revision = 0x43;
    dc->desc = "NEC USB OHCI controller";
}

static void nec_usb_ehci_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    (void)data;
    pc->vendor_id = PCI_VENDOR_ID_NEC;
    pc->device_id = PCI_DEVICE_ID_NEC_USB_EHCI;
    pc->revision = 0x04;
    dc->desc = "NEC USB EHCI controller";
    dc->hotpluggable = false;
}

static const TypeInfo nec_usb_types[] = {
    {
        .name = TYPE_NEC_USB_OHCI,
        .parent = TYPE_PCI_OHCI,
        .class_init = nec_usb_ohci_class_init,
    }, {
        .name = TYPE_NEC_USB_EHCI,
        .parent = TYPE_PCI_EHCI,
        .class_init = nec_usb_ehci_class_init,
    },
};

DEFINE_TYPES(nec_usb_types)
