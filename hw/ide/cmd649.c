/*
 * CMD649 IDE controller
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/ide/cmd649.h"
#include "hw/ide/pci.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/range.h"

#include "ide-internal.h"

#define CMD649_CFR                  0x50
#define CMD649_CFR_INTR_CH0         0x04
#define CMD649_CNTRL                0x51
#define CMD649_CNTRL_EN_CH0         0x04
#define CMD649_CNTRL_EN_CH1         0x08
#define CMD649_CMDTIM               0x52
#define CMD649_ARTTIM0              0x53
#define CMD649_DRWTIM0              0x54
#define CMD649_ARTTIM1              0x55
#define CMD649_DRWTIM1              0x56
#define CMD649_ARTTIM23             0x57
#define CMD649_ARTTIM23_INTR_CH1    0x10
#define CMD649_DRWTIM2              0x58
#define CMD649_BRST                 0x59
#define CMD649_DRWTIM3              0x5b
#define CMD649_PM_CAP               0x60
#define CMD649_BMIDECR0             0x70
#define CMD649_MRDMODE              0x71
#define CMD649_MRDMODE_INTR_CH0     0x04
#define CMD649_MRDMODE_INTR_CH1     0x08
#define CMD649_MRDMODE_BLK_CH0      0x10
#define CMD649_MRDMODE_BLK_CH1      0x20
#define CMD649_MRDMODE_RST_CH0      0x40
#define CMD649_MRDMODE_RST_CH1      0x80
#define CMD649_BMIDESR0             0x72
#define CMD649_UDIDETCR0            0x73
#define CMD649_DTPR0                0x74
#define CMD649_BMIDECR1             0x78
#define CMD649_BMIDECSR             0x79
#define CMD649_BMIDESR1             0x7a
#define CMD649_UDIDETCR1            0x7b
#define CMD649_DTPR1                0x7c

struct CMD649IDEState {
    PCIIDEState parent_obj;

    uint32_t secondary;
    bool primary_cable80;
    bool secondary_cable80;
};

static void cmd649_update_mappings(CMD649IDEState *s);

static bool cmd649_channel_native(const PCIDevice *dev, unsigned channel)
{
    return dev->config[PCI_CLASS_PROG] & (1U << (channel * 2));
}

static bool cmd649_channel_enabled(const CMD649IDEState *s, unsigned channel)
{
    const PCIDevice *dev = PCI_DEVICE(s);
    uint8_t enable = CMD649_CNTRL_EN_CH0 << channel;
    uint8_t reset = CMD649_MRDMODE_RST_CH0 << channel;
    uint16_t pmcsr = pci_get_word(dev->config + CMD649_PM_CAP + PCI_PM_CTRL);

    return !(pmcsr & PCI_PM_CTRL_STATE_MASK) &&
           (dev->config[CMD649_CNTRL] & enable) &&
           !(dev->config[CMD649_MRDMODE] & reset);
}

static void cmd649_sync_interrupt_status(PCIDevice *dev, uint8_t pending)
{
    dev->config[CMD649_MRDMODE] &=
        ~(CMD649_MRDMODE_INTR_CH0 | CMD649_MRDMODE_INTR_CH1);
    dev->config[CMD649_MRDMODE] |= pending &
        (CMD649_MRDMODE_INTR_CH0 | CMD649_MRDMODE_INTR_CH1);

    if (pending & CMD649_MRDMODE_INTR_CH0) {
        dev->config[CMD649_CFR] |= CMD649_CFR_INTR_CH0;
    } else {
        dev->config[CMD649_CFR] &= ~CMD649_CFR_INTR_CH0;
    }

    if (pending & CMD649_MRDMODE_INTR_CH1) {
        dev->config[CMD649_ARTTIM23] |= CMD649_ARTTIM23_INTR_CH1;
    } else {
        dev->config[CMD649_ARTTIM23] &= ~CMD649_ARTTIM23_INTR_CH1;
    }
}

static void cmd649_clear_irq_routes(CMD649IDEState *s)
{
    PCIDevice *dev = PCI_DEVICE(s);

    qemu_set_irq(s->parent_obj.isa_irq[0], 0);
    qemu_set_irq(s->parent_obj.isa_irq[1], 0);
    if (dev->config[PCI_INTERRUPT_PIN]) {
        pci_set_irq(dev, 0);
    }
}

static void cmd649_update_irq(CMD649IDEState *s)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint8_t mrdmode = dev->config[CMD649_MRDMODE];
    bool pci_level = false;
    unsigned channel;

    cmd649_clear_irq_routes(s);
    for (channel = 0; channel < 2; channel++) {
        uint8_t pending = CMD649_MRDMODE_INTR_CH0 << channel;
        uint8_t blocked = CMD649_MRDMODE_BLK_CH0 << channel;

        if (!cmd649_channel_enabled(s, channel) ||
            !(mrdmode & pending) || (mrdmode & blocked)) {
            continue;
        }
        if (cmd649_channel_native(dev, channel)) {
            pci_level = true;
        } else {
            qemu_set_irq(s->parent_obj.isa_irq[channel], 1);
        }
    }
    if (pci_level && dev->config[PCI_INTERRUPT_PIN]) {
        pci_set_irq(dev, 1);
    }
}

static void cmd649_set_irq(void *opaque, int channel, int level)
{
    CMD649IDEState *s = opaque;
    PCIDevice *dev = PCI_DEVICE(s);
    uint8_t pending = dev->config[CMD649_MRDMODE] &
        (CMD649_MRDMODE_INTR_CH0 | CMD649_MRDMODE_INTR_CH1);

    if (level) {
        pending |= CMD649_MRDMODE_INTR_CH0 << channel;
    } else {
        pending &= ~(CMD649_MRDMODE_INTR_CH0 << channel);
    }
    cmd649_sync_interrupt_status(dev, pending);
    cmd649_update_irq(s);
}

static void cmd649_sync_bmdma_config(CMD649IDEState *s)
{
    PCIIDEState *ide = PCI_IDE(s);
    PCIDevice *dev = PCI_DEVICE(s);
    unsigned channel;

    for (channel = 0; channel < 2; channel++) {
        unsigned base = CMD649_BMIDECR0 + channel * 8;
        BMDMAState *bm = &ide->bmdma[channel];

        dev->config[base] = bm->cmd;
        dev->config[base + 2] = bm->status;
        pci_set_long(dev->config + base + 4, bm->addr);
    }
}

static bool cmd649_write_has_bit(uint32_t addr, uint32_t val, int len,
                                 uint32_t reg, uint8_t mask)
{
    if (reg < addr || reg >= addr + len) {
        return false;
    }
    return (val >> ((reg - addr) * 8)) & mask;
}

static uint8_t cmd649_write_byte(uint32_t addr, uint32_t val, uint32_t reg)
{
    return val >> ((reg - addr) * 8);
}

static void cmd649_config_write(PCIDevice *dev, uint32_t addr, uint32_t val,
                                int len)
{
    CMD649IDEState *s = CMD649_IDE(dev);
    PCIIDEState *ide = PCI_IDE(dev);
    uint8_t pending = dev->config[CMD649_MRDMODE] &
        (CMD649_MRDMODE_INTR_CH0 | CMD649_MRDMODE_INTR_CH1);
    uint8_t old_reset = dev->config[CMD649_MRDMODE] &
        (CMD649_MRDMODE_RST_CH0 | CMD649_MRDMODE_RST_CH1);
    unsigned channel;

    pci_default_write_config(dev, addr, val, len);

    if (cmd649_write_has_bit(addr, val, len, CMD649_CFR,
                             CMD649_CFR_INTR_CH0) ||
        cmd649_write_has_bit(addr, val, len, CMD649_MRDMODE,
                             CMD649_MRDMODE_INTR_CH0)) {
        pending &= ~CMD649_MRDMODE_INTR_CH0;
    }
    if (cmd649_write_has_bit(addr, val, len, CMD649_ARTTIM23,
                             CMD649_ARTTIM23_INTR_CH1) ||
        cmd649_write_has_bit(addr, val, len, CMD649_MRDMODE,
                             CMD649_MRDMODE_INTR_CH1)) {
        pending &= ~CMD649_MRDMODE_INTR_CH1;
    }
    cmd649_sync_interrupt_status(dev, pending);

    for (channel = 0; channel < 2; channel++) {
        unsigned base = CMD649_BMIDECR0 + channel * 8;
        BMDMAState *bm = &ide->bmdma[channel];

        if (range_covers_byte(addr, len, base)) {
            bmdma_cmd_writeb(bm, cmd649_write_byte(addr, val, base));
            dev->config[base] = bm->cmd;
        }
        if (range_covers_byte(addr, len, base + 2)) {
            bmdma_status_writeb(bm,
                                cmd649_write_byte(addr, val, base + 2));
            dev->config[base + 2] = bm->status;
        }
        if (ranges_overlap(addr, len, base + 4, 4)) {
            bm->addr = pci_get_long(dev->config + base + 4) & ~3U;
            pci_set_long(dev->config + base + 4, bm->addr);
        }
    }

    for (channel = 0; channel < 2; channel++) {
        uint8_t reset = CMD649_MRDMODE_RST_CH0 << channel;

        if (!(old_reset & reset) &&
            (dev->config[CMD649_MRDMODE] & reset)) {
            ide_bus_reset(&ide->bus[channel]);
        }
    }

    if (range_covers_byte(addr, len, PCI_CLASS_PROG) ||
        range_covers_byte(addr, len, CMD649_CNTRL) ||
        range_covers_byte(addr, len, CMD649_MRDMODE) ||
        ranges_overlap(addr, len, CMD649_PM_CAP + PCI_PM_CTRL, 2)) {
        cmd649_update_mappings(s);
    } else {
        cmd649_update_irq(s);
    }
}

static uint32_t cmd649_config_read(PCIDevice *dev, uint32_t addr, int len)
{
    CMD649IDEState *s = CMD649_IDE(dev);
    uint32_t value;
    unsigned channel;
    int i;

    cmd649_sync_bmdma_config(s);
    value = pci_default_read_config(dev, addr, len);

    for (channel = 0; channel < 2; channel++) {
        uint32_t bars = PCI_BASE_ADDRESS_0 + channel * 8;

        if (cmd649_channel_native(dev, channel) ||
            !ranges_overlap(addr, len, bars, 8)) {
            continue;
        }
        for (i = 0; i < len; i++) {
            if (addr + i >= bars && addr + i < bars + 8) {
                value &= ~(0xffU << (i * 8));
            }
        }
    }

    if (range_covers_byte(addr, len, CMD649_MRDMODE)) {
        value &= ~(3U << ((CMD649_MRDMODE - addr) * 8));
    }
    return value;
}

static uint64_t cmd649_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    BMDMAState *bm = opaque;
    PCIIDEState *ide = bm->pci_dev;
    CMD649IDEState *s = CMD649_IDE(ide);
    PCIDevice *dev = PCI_DEVICE(s);
    unsigned channel = bm == &ide->bmdma[1];

    if (size != 1) {
        return MAKE_64BIT_MASK(0, size * 8);
    }

    switch (addr & 3) {
    case 0:
        return bm->cmd;
    case 1:
        if (channel == 0) {
            return dev->config[CMD649_MRDMODE] & ~3U;
        }
        return dev->config[CMD649_BMIDECSR];
    case 2:
        return bm->status;
    case 3:
        return dev->config[channel ? CMD649_UDIDETCR1 : CMD649_UDIDETCR0];
    default:
        return 0xff;
    }
}

static void cmd649_bmdma_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    BMDMAState *bm = opaque;
    PCIIDEState *ide = bm->pci_dev;
    PCIDevice *dev = PCI_DEVICE(ide);
    unsigned channel = bm == &ide->bmdma[1];
    unsigned reg;

    if (size != 1) {
        return;
    }

    switch (addr & 3) {
    case 0:
        reg = channel ? CMD649_BMIDECR1 : CMD649_BMIDECR0;
        break;
    case 1:
        reg = channel ? CMD649_BMIDECSR : CMD649_MRDMODE;
        break;
    case 2:
        reg = channel ? CMD649_BMIDESR1 : CMD649_BMIDESR0;
        break;
    case 3:
        reg = channel ? CMD649_UDIDETCR1 : CMD649_UDIDETCR0;
        break;
    default:
        return;
    }
    cmd649_config_write(dev, reg, value, 1);
}

static const MemoryRegionOps cmd649_bmdma_ops = {
    .read = cmd649_bmdma_read,
    .write = cmd649_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void cmd649_bmdma_setup_bar(CMD649IDEState *s)
{
    PCIIDEState *ide = PCI_IDE(s);
    unsigned channel;

    memory_region_init(&ide->bmdma_bar, OBJECT(s), "cmd649-bmdma", 16);
    for (channel = 0; channel < 2; channel++) {
        BMDMAState *bm = &ide->bmdma[channel];

        memory_region_init_io(&bm->extra_io, OBJECT(s), &cmd649_bmdma_ops,
                              bm, "cmd649-bmdma-registers", 4);
        memory_region_add_subregion(&ide->bmdma_bar, channel * 8,
                                    &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(s),
                              &bmdma_addr_ioport_ops, bm,
                              "cmd649-bmdma-descriptor", 4);
        memory_region_add_subregion(&ide->bmdma_bar, channel * 8 + 4,
                                    &bm->addr_ioport);
    }
}

static void cmd649_set_legacy_ports(CMD649IDEState *s, unsigned channel,
                                    bool enabled)
{
    static const uint16_t data_port[] = { 0x1f0, 0x170 };
    static const uint16_t control_port[] = { 0x3f6, 0x376 };
    PCIIDEState *ide = PCI_IDE(s);
    PCIDevice *dev = PCI_DEVICE(s);
    IDEBus *bus = &ide->bus[channel];

    if (enabled && !bus->portio_list.owner) {
        portio_list_init(&bus->portio_list, OBJECT(s), ide_portio_list, bus,
                         "cmd649-ide");
        portio_list_add(&bus->portio_list, pci_address_space_io(dev),
                        data_port[channel]);
    } else if (bus->portio_list.owner) {
        portio_list_set_enabled(&bus->portio_list, enabled);
    }

    if (enabled && !bus->portio2_list.owner) {
        portio_list_init(&bus->portio2_list, OBJECT(s), ide_portio2_list, bus,
                         "cmd649-ide-control");
        portio_list_add(&bus->portio2_list, pci_address_space_io(dev),
                        control_port[channel]);
    } else if (bus->portio2_list.owner) {
        portio_list_set_enabled(&bus->portio2_list, enabled);
    }
}

static void cmd649_update_mappings(CMD649IDEState *s)
{
    PCIIDEState *ide = PCI_IDE(s);
    PCIDevice *dev = PCI_DEVICE(s);
    bool native_irq = false;
    unsigned channel;

    cmd649_clear_irq_routes(s);
    for (channel = 0; channel < 2; channel++) {
        bool enabled = cmd649_channel_enabled(s, channel);
        bool native = cmd649_channel_native(dev, channel);

        memory_region_set_enabled(&ide->data_bar[channel], enabled && native);
        memory_region_set_enabled(&ide->cmd_bar[channel], enabled && native);
        memory_region_set_enabled(&ide->bmdma[channel].extra_io, enabled);
        memory_region_set_enabled(&ide->bmdma[channel].addr_ioport, enabled);
        cmd649_set_legacy_ports(s, channel, enabled && !native);
        native_irq |= enabled && native;
    }
    pci_config_set_interrupt_pin(dev->config, native_irq ? 1 : 0);
    cmd649_update_irq(s);
}

static void cmd649_configure_masks(PCIDevice *dev)
{
    memset(dev->wmask + PCI_CONFIG_HEADER_SIZE, 0,
           PCI_CONFIG_SPACE_SIZE - PCI_CONFIG_HEADER_SIZE);
    memset(dev->w1cmask + PCI_CONFIG_HEADER_SIZE, 0,
           PCI_CONFIG_SPACE_SIZE - PCI_CONFIG_HEADER_SIZE);

    dev->wmask[PCI_CLASS_PROG] = 0x05;
    dev->w1cmask[CMD649_CFR] = CMD649_CFR_INTR_CH0;
    dev->wmask[CMD649_CNTRL] = 0xcc;
    /* Transfer-timing fields are readback latches; effects are not implemented. */
    dev->wmask[CMD649_CMDTIM] = 0xff;
    dev->wmask[CMD649_ARTTIM0] = 0xc0;
    dev->wmask[CMD649_DRWTIM0] = 0xff;
    dev->wmask[CMD649_ARTTIM1] = 0xc0;
    dev->wmask[CMD649_DRWTIM1] = 0xff;
    dev->wmask[CMD649_ARTTIM23] = 0xcc;
    dev->w1cmask[CMD649_ARTTIM23] = CMD649_ARTTIM23_INTR_CH1;
    dev->wmask[CMD649_DRWTIM2] = 0xff;
    dev->wmask[CMD649_BRST] = 0xff;
    dev->wmask[CMD649_DRWTIM3] = 0xff;

    pci_set_word(dev->wmask + CMD649_PM_CAP + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK | PCI_PM_CTRL_DATA_SEL_MASK);

    dev->wmask[CMD649_BMIDECR0] = BM_CMD_START | BM_CMD_READ;
    dev->wmask[CMD649_MRDMODE] = 0xf3;
    dev->w1cmask[CMD649_MRDMODE] =
        CMD649_MRDMODE_INTR_CH0 | CMD649_MRDMODE_INTR_CH1;
    dev->wmask[CMD649_BMIDESR0] = 0x60;
    dev->w1cmask[CMD649_BMIDESR0] = BM_STATUS_ERROR | BM_STATUS_INT;
    dev->wmask[CMD649_UDIDETCR0] = 0xff;
    dev->wmask[CMD649_DTPR0] = 0xfc;
    memset(dev->wmask + CMD649_DTPR0 + 1, 0xff, 3);

    dev->wmask[CMD649_BMIDECR1] = BM_CMD_START | BM_CMD_READ;
    dev->wmask[CMD649_BMIDECSR] = 0xf0;
    dev->wmask[CMD649_BMIDESR1] = 0x60;
    dev->w1cmask[CMD649_BMIDESR1] = BM_STATUS_ERROR | BM_STATUS_INT;
    dev->wmask[CMD649_UDIDETCR1] = 0xff;
    dev->wmask[CMD649_DTPR1] = 0xfc;
    memset(dev->wmask + CMD649_DTPR1 + 1, 0xff, 3);
}

static void cmd649_configure_defaults(CMD649IDEState *s)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint8_t *config = dev->config;

    pci_config_set_prog_interface(config, 0x8f);
    config[PCI_CACHE_LINE_SIZE] = 0;
    config[PCI_LATENCY_TIMER] = 0;
    config[PCI_INTERRUPT_LINE] = 0x0e;
    config[PCI_MIN_GNT] = 0x02;
    config[PCI_MAX_LAT] = 0x04;

    memset(config + CMD649_CFR, 0, 0x30);
    config[CMD649_CFR] = 0x40;
    config[CMD649_CNTRL] = 0xe4 |
        (s->secondary ? CMD649_CNTRL_EN_CH1 : 0);
    config[CMD649_ARTTIM0] = 0x80;
    config[CMD649_ARTTIM1] = 0xc0;
    config[CMD649_ARTTIM23] = 0x8c;
    config[CMD649_BRST] = 0x40;

    config[CMD649_PM_CAP + PCI_CAP_LIST_ID] = PCI_CAP_ID_PM;
    config[CMD649_PM_CAP + PCI_CAP_LIST_NEXT] = 0;
    pci_set_word(config + CMD649_PM_CAP + PCI_PM_PMC,
                 0x0002 | PCI_PM_CAP_DSI | PCI_PM_CAP_D1 | PCI_PM_CAP_D2);
    pci_set_word(config + CMD649_PM_CAP + PCI_PM_CTRL,
                 PCI_PM_CTRL_DATA_SCALE_MASK);
    config[CMD649_PM_CAP + PCI_PM_PPB_EXTENSIONS] = 0;
    config[CMD649_PM_CAP + PCI_PM_DATA_REGISTER] = 0xf0;

    config[CMD649_MRDMODE] = 0;
    config[CMD649_UDIDETCR0] = 0xf0;
    config[CMD649_BMIDECSR] =
        (s->primary_cable80 ? 0x01 : 0) |
        (s->secondary_cable80 ? 0x02 : 0);
    config[CMD649_UDIDETCR1] = 0xf0;
    cmd649_sync_bmdma_config(s);
    cmd649_sync_interrupt_status(dev, 0);
}

static void cmd649_reset(DeviceState *dev)
{
    CMD649IDEState *s = CMD649_IDE(dev);
    PCIIDEState *ide = PCI_IDE(dev);
    unsigned channel;

    cmd649_clear_irq_routes(s);
    for (channel = 0; channel < 2; channel++) {
        ide_bus_reset(&ide->bus[channel]);
    }
    cmd649_configure_defaults(s);
    cmd649_update_mappings(s);
}

static int cmd649_post_load(void *opaque, int version_id)
{
    CMD649IDEState *s = opaque;

    cmd649_sync_bmdma_config(s);
    cmd649_update_mappings(s);
    return 0;
}

static const VMStateDescription vmstate_cmd649 = {
    .name = "cmd649-ide",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = cmd649_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, CMD649IDEState, 0, vmstate_ide_pci,
                       PCIIDEState),
        VMSTATE_END_OF_LIST()
    },
};

static void cmd649_realize(PCIDevice *dev, Error **errp)
{
    CMD649IDEState *s = CMD649_IDE(dev);
    PCIIDEState *ide = PCI_IDE(dev);
    DeviceState *ds = DEVICE(dev);
    unsigned channel;

    if (pci_pm_init(dev, CMD649_PM_CAP, errp) < 0) {
        return;
    }
    cmd649_configure_masks(dev);

    memory_region_init_io(&ide->data_bar[0], OBJECT(s),
                          &pci_ide_data_le_ops, &ide->bus[0],
                          "cmd649-primary-data", 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &ide->data_bar[0]);
    memory_region_init_io(&ide->cmd_bar[0], OBJECT(s), &pci_ide_cmd_le_ops,
                          &ide->bus[0], "cmd649-primary-control", 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &ide->cmd_bar[0]);

    memory_region_init_io(&ide->data_bar[1], OBJECT(s),
                          &pci_ide_data_le_ops, &ide->bus[1],
                          "cmd649-secondary-data", 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, &ide->data_bar[1]);
    memory_region_init_io(&ide->cmd_bar[1], OBJECT(s), &pci_ide_cmd_le_ops,
                          &ide->bus[1], "cmd649-secondary-control", 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, &ide->cmd_bar[1]);

    cmd649_bmdma_setup_bar(s);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &ide->bmdma_bar);

    qdev_init_gpio_in(ds, cmd649_set_irq, 2);
    for (channel = 0; channel < 2; channel++) {
        ide_bus_init(&ide->bus[channel], sizeof(ide->bus[channel]), ds,
                     channel, MAX_IDE_DEVS);
        ide_bus_init_output_irq(&ide->bus[channel],
                                qdev_get_gpio_in(ds, channel));
        bmdma_init(&ide->bus[channel], &ide->bmdma[channel], ide);
        ide_bus_register_restart_cb(&ide->bus[channel]);
    }
    cmd649_configure_defaults(s);
    cmd649_update_mappings(s);
}

static void cmd649_exit(PCIDevice *dev)
{
    CMD649IDEState *s = CMD649_IDE(dev);
    PCIIDEState *ide = PCI_IDE(dev);
    unsigned channel;

    cmd649_clear_irq_routes(s);
    for (channel = 0; channel < 2; channel++) {
        IDEBus *bus = &ide->bus[channel];

        if (bus->portio_list.owner) {
            portio_list_del(&bus->portio_list);
            portio_list_destroy(&bus->portio_list);
        }
        if (bus->portio2_list.owner) {
            portio_list_del(&bus->portio2_list);
            portio_list_destroy(&bus->portio2_list);
        }
        memory_region_del_subregion(&ide->bmdma_bar,
                                    &ide->bmdma[channel].extra_io);
        memory_region_del_subregion(&ide->bmdma_bar,
                                    &ide->bmdma[channel].addr_ioport);
    }
}

static const Property cmd649_properties[] = {
    DEFINE_PROP_UINT32("secondary", CMD649IDEState, secondary, 1),
    DEFINE_PROP_BOOL("primary-cable80", CMD649IDEState, primary_cable80,
                     false),
    DEFINE_PROP_BOOL("secondary-cable80", CMD649IDEState, secondary_cable80,
                     false),
};

static void cmd649_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, cmd649_reset);
    dc->vmsd = &vmstate_cmd649;
    device_class_set_props(dc, cmd649_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    pc->realize = cmd649_realize;
    pc->exit = cmd649_exit;
    pc->vendor_id = PCI_VENDOR_ID_CMD;
    pc->device_id = PCI_DEVICE_ID_CMD_649;
    pc->revision = 0x02;
    pc->class_id = PCI_CLASS_STORAGE_IDE;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_CMD;
    pc->subsystem_id = PCI_DEVICE_ID_CMD_649;
    pc->config_read = cmd649_config_read;
    pc->config_write = cmd649_config_write;
}

static const TypeInfo cmd649_info = {
    .name = TYPE_CMD649_IDE,
    .parent = TYPE_PCI_IDE,
    .instance_size = sizeof(CMD649IDEState),
    .class_init = cmd649_class_init,
};

static void cmd649_register_types(void)
{
    type_register_static(&cmd649_info);
}

type_init(cmd649_register_types)
