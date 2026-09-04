/*
 * Broadcom BCM5701/BCM5704 PCI Ethernet enumeration model
 *
 * Registers, DMA, PHY, and packet I/O are not implemented.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define BCM57XX_PCI_PCIX_CAP             0x40

#define BCM57XX_PCI_MISC_HOST_CTRL       0x68
#define BCM57XX_MISC_HOST_CTRL_RW_MASK   0x000003feU
#define BCM57XX_MISC_HOST_CTRL_CHIPREV_SHIFT 16

#define BCM5701_CHIPREV_ID_B5            0x0105
#define BCM5704_CHIPREV_ID_B0            0x2100

#define BCM57XX_PCIX_COMMAND_RW_MASK     \
    (PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO | PCI_X_CMD_MAX_READ | \
     PCI_X_CMD_MAX_SPLIT)
#define BCM57XX_PCIX_STATUS_W1C_MASK     \
    (PCI_X_STATUS_SPL_DISC | PCI_X_STATUS_UNX_SPL | PCI_X_STATUS_SPL_ERR)

/* BCM5704: 64-bit/133 MHz, DMMRBC=2, DMOST=0, DMCRS=1. */
#define BCM5704_PCIX_STATUS_CAPS          \
    (PCI_X_STATUS_64BIT | PCI_X_STATUS_133MHZ | (2U << 21) | (1U << 26))

struct BCM57xxState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion pci_rom;
    NICConf conf;
    NICState *nic;
};

static uint64_t bcm57xx_absent_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    return MAKE_64BIT_MASK(0, size * 8);
}

static void bcm57xx_absent_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
}

static const MemoryRegionOps bcm57xx_absent_ops = {
    .read = bcm57xx_absent_read,
    .write = bcm57xx_absent_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static ssize_t bcm57xx_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    return size;
}

static NetClientInfo bcm57xx_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = bcm57xx_receive,
};

static uint16_t bcm57xx_chiprev_id(PCIDevice *pdev)
{
    switch (pci_get_word(pdev->config + PCI_DEVICE_ID)) {
    case BCM5701_PCI_DEVICE_ID:
        return BCM5701_CHIPREV_ID_B5;
    case BCM5704_PCI_DEVICE_ID:
        return BCM5704_CHIPREV_ID_B0;
    default:
        g_assert_not_reached();
    }
}

static uint32_t bcm57xx_pcix_status(PCIDevice *pdev)
{
    if (pci_get_word(pdev->config + PCI_DEVICE_ID) ==
        BCM5701_PCI_DEVICE_ID) {
        /* BCM5701 reports zero here while operating in conventional PCI. */
        return 0;
    }

    return BCM5704_PCIX_STATUS_CAPS |
           ((uint32_t)pci_dev_bus_num(pdev) << 8) | pdev->devfn;
}

static void bcm57xx_reset_config(PCIDevice *pdev)
{
    pci_set_word(pdev->config + BCM57XX_PCI_PCIX_CAP + PCI_X_CMD, 0);
    pci_set_long(pdev->config + BCM57XX_PCI_PCIX_CAP + PCI_X_STATUS,
                 bcm57xx_pcix_status(pdev));
    pci_set_long(pdev->config + BCM57XX_PCI_MISC_HOST_CTRL,
                 (uint32_t)bcm57xx_chiprev_id(pdev) <<
                 BCM57XX_MISC_HOST_CTRL_CHIPREV_SHIFT);
}

static bool bcm57xx_init_config(PCIDevice *pdev, Error **errp)
{
    if (pci_add_capability(pdev, PCI_CAP_ID_PCIX, BCM57XX_PCI_PCIX_CAP,
                           PCI_CAP_PCIX_SIZEOF_V0, errp) < 0) {
        return false;
    }

    pci_set_word(pdev->wmask + BCM57XX_PCI_PCIX_CAP + PCI_X_CMD,
                 BCM57XX_PCIX_COMMAND_RW_MASK);
    pci_set_long(pdev->w1cmask + BCM57XX_PCI_PCIX_CAP + PCI_X_STATUS,
                 BCM57XX_PCIX_STATUS_W1C_MASK);

    /* CLEAR_INT (bit 0) is a write pulse and is not stored. */
    pci_set_long(pdev->wmask + BCM57XX_PCI_MISC_HOST_CTRL,
                 BCM57XX_MISC_HOST_CTRL_RW_MASK);
    pci_set_long(pdev->cmask + BCM57XX_PCI_MISC_HOST_CTRL, UINT32_MAX);
    bcm57xx_reset_config(pdev);
    return true;
}

static void bcm57xx_reset(DeviceState *dev)
{
    BCM57xxState *s = BCM57XX(dev);

    bcm57xx_reset_config(&s->parent_obj);
    pci_set_irq(&s->parent_obj, 0);
}

static int bcm57xx_post_load(void *opaque, int version_id)
{
    BCM57xxState *s = opaque;

    pci_set_irq(&s->parent_obj, 0);
    return 0;
}

static const VMStateDescription vmstate_bcm5701 = {
    .name = TYPE_BCM5701,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bcm57xx_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, BCM57xxState),
        VMSTATE_MACADDR(conf.macaddr, BCM57xxState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_bcm5704 = {
    .name = TYPE_BCM5704,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bcm57xx_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, BCM57xxState),
        VMSTATE_MACADDR(conf.macaddr, BCM57xxState),
        VMSTATE_END_OF_LIST()
    },
};

static void bcm57xx_realize(PCIDevice *pdev, Error **errp)
{
    BCM57xxState *s = BCM57XX(pdev);
    DeviceState *dev = DEVICE(pdev);

    if (pdev->romfile && pdev->romfile[0]) {
        error_setg(errp, "romfile is not supported by the enumeration-only "
                   "BCM57xx model");
        return;
    }

    if (!bcm57xx_init_config(pdev, errp)) {
        return;
    }

    pdev->config[PCI_INTERRUPT_PIN] = 1; /* INTA */
    memory_region_init_io(&s->mmio, OBJECT(s), &bcm57xx_absent_ops, s,
                          TYPE_BCM57XX ".mmio", BCM57XX_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                              PCI_BASE_ADDRESS_MEM_TYPE_64, &s->mmio);

    memory_region_init_io(&s->pci_rom, OBJECT(s), &bcm57xx_absent_ops, s,
                          TYPE_BCM57XX ".pci-rom", BCM57XX_ROM_SIZE);
    pci_register_bar(pdev, PCI_ROM_SLOT, 0, &s->pci_rom);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&bcm57xx_net_info, &s->conf,
                          object_get_typename(OBJECT(s)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    pci_set_irq(pdev, 0);
}

static void bcm57xx_exit(PCIDevice *pdev)
{
    BCM57xxState *s = BCM57XX(pdev);

    pci_set_irq(pdev, 0);
    qemu_del_nic(s->nic);
    s->nic = NULL;
}

static const Property bcm57xx_properties[] = {
    DEFINE_NIC_PROPERTIES(BCM57xxState, conf),
};

static void bcm57xx_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = bcm57xx_realize;
    pc->exit = bcm57xx_exit;
    pc->vendor_id = BCM57XX_PCI_VENDOR_ID;
    pc->class_id = PCI_CLASS_NETWORK_ETHERNET;
    device_class_set_legacy_reset(dc, bcm57xx_reset);
    device_class_set_props(dc, bcm57xx_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void bcm5701_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->device_id = BCM5701_PCI_DEVICE_ID;
    pc->revision = BCM5701_PCI_REVISION;
    pc->subsystem_vendor_id = BCM5701_PCI_SUBSYSTEM_VENDOR_ID;
    pc->subsystem_id = BCM5701_PCI_SUBSYSTEM_ID;
    dc->desc = "Broadcom BCM5701 Gigabit Ethernet "
               "(enumeration only; network datapath unavailable)";
    dc->vmsd = &vmstate_bcm5701;
}

static void bcm5704_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->device_id = BCM5704_PCI_DEVICE_ID;
    pc->revision = BCM5704_PCI_REVISION;
    pc->subsystem_vendor_id = BCM5704_PCI_SUBSYSTEM_VENDOR_ID;
    pc->subsystem_id = BCM5704_PCI_SUBSYSTEM_ID;
    dc->desc = "Broadcom BCM5704 Gigabit Ethernet "
               "(enumeration only; network datapath unavailable)";
    dc->vmsd = &vmstate_bcm5704;
}

static const TypeInfo bcm57xx_info = {
    .name = TYPE_BCM57XX,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(BCM57xxState),
    .abstract = true,
    .class_init = bcm57xx_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo bcm5701_info = {
    .name = TYPE_BCM5701,
    .parent = TYPE_BCM57XX,
    .class_init = bcm5701_class_init,
};

static const TypeInfo bcm5704_info = {
    .name = TYPE_BCM5704,
    .parent = TYPE_BCM57XX,
    .class_init = bcm5704_class_init,
};

static void bcm57xx_register_types(void)
{
    type_register_static(&bcm57xx_info);
    type_register_static(&bcm5701_info);
    type_register_static(&bcm5704_info);
}

type_init(bcm57xx_register_types)
