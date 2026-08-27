/*
 * QEMU IDE Emulation: ISA Bus support.
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
#include "hw/isa/isa.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/block-backend.h"
#include "system/dma.h"

#include "hw/ide/isa.h"
#include "qom/object.h"
#include "ide-internal.h"

/***********************************************************/
/* ISA IDE definitions */

struct ISAIDEState {
    ISADevice parent_obj;

    IDEBus    bus;
    IDEDMA    pio_only_dma;
    uint32_t  iobase;
    uint32_t  iobase2;
    uint32_t  irqnum;
    uint8_t   migration_pio_only;
    bool      pio_only;
    bool      ide_initialized;
    bool      vmstate_registered;
};

static void isa_ide_pio_only_start_dma(const IDEDMA *dma, IDEState *ide,
                                       BlockCompletionFunc *cb)
{
    ISAIDEState *s = container_of(dma, ISAIDEState, pio_only_dma);

    /*
     * A PIO-only host cannot execute DMA.  ide_start_dma() has installed retry
     * state and, for ATA/ATAPI data commands, started block accounting.
     */
    (void)cb;
    assert(ide->bus == &s->bus);
    if (ide->blk) {
        block_acct_failed(blk_get_stats(ide->blk), &ide->acct);
    }
    ide_abort_command(ide);
    ide_set_inactive(ide, false);
    ide_bus_set_irq(ide->bus);
}

static void isa_ide_pio_only_restart_dma(const IDEDMA *dma)
{
    /* Keep the generic IDE PIO error/retry runstate hook registered. */
}

static const IDEDMAOps isa_ide_pio_only_dma_ops = {
    .start_dma = isa_ide_pio_only_start_dma,
    .restart_dma = isa_ide_pio_only_restart_dma,
};

static void isa_ide_reset(DeviceState *d)
{
    ISAIDEState *s = ISA_IDE(d);

    ide_bus_reset(&s->bus);
}

static bool isa_ide_migration_compatible(void *opaque, int version_id)
{
    ISAIDEState *s = opaque;

    /* ISA IDE streams before v4 used DMA without bus-master transfers. */
    return version_id >= 4 || !s->pio_only;
}

static const VMStateDescription vmstate_ide_isa = {
    .name = "isa-ide",
    .version_id = 4,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_VALIDATE("pio-only migration compatibility",
                         isa_ide_migration_compatible),
        VMSTATE_UINT8_EQUAL_V(migration_pio_only, ISAIDEState, 4),
        VMSTATE_IDE_BUS(bus, ISAIDEState),
        VMSTATE_IDE_DRIVES(bus.ifs, ISAIDEState),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ide_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIDEState *s = ISA_IDE(dev);
    int ret;

    ide_bus_init(&s->bus, sizeof(s->bus), dev, 0, 2);
    ret = ide_init_ioport(&s->bus, isadev, s->iobase, s->iobase2);
    if (ret) {
        error_setg_errno(errp, -ret, "Failed to realize ISA IDE I/O ports");
        return;
    }
    ide_bus_init_output_irq(&s->bus, isa_get_irq(isadev, s->irqnum));
    s->ide_initialized = true;
    s->migration_pio_only = s->pio_only;
    if (s->pio_only) {
        s->pio_only_dma.ops = &isa_ide_pio_only_dma_ops;
        s->bus.dma = &s->pio_only_dma;
    }
    vmstate_register_any(VMSTATE_IF(dev), &vmstate_ide_isa, s);
    s->vmstate_registered = true;
    ide_bus_register_restart_cb(&s->bus);
}

static void isa_ide_unrealize(DeviceState *dev)
{
    ISAIDEState *s = ISA_IDE(dev);
    unsigned i;

    if (s->vmstate_registered) {
        vmstate_unregister(VMSTATE_IF(dev), &vmstate_ide_isa, s);
        s->vmstate_registered = false;
    }
    if (s->bus.portio2_list.owner) {
        portio_list_del(&s->bus.portio2_list);
    }
    if (s->bus.portio_list.owner) {
        portio_list_del(&s->bus.portio_list);
    }
    if (s->ide_initialized) {
        ide_bus_reset(&s->bus);
        qemu_irq_lower(s->bus.irq);
    }
    if (s->bus.portio2_list.owner) {
        portio_list_destroy(&s->bus.portio2_list);
        memset(&s->bus.portio2_list, 0, sizeof(s->bus.portio2_list));
    }
    if (s->bus.portio_list.owner) {
        portio_list_destroy(&s->bus.portio_list);
        memset(&s->bus.portio_list, 0, sizeof(s->bus.portio_list));
    }
    if (s->ide_initialized) {
        for (i = 0; i < ARRAY_SIZE(s->bus.ifs); i++) {
            ide_exit(&s->bus.ifs[i]);
        }
        s->ide_initialized = false;
    }
}

IDEBus *isa_ide_bus(ISAIDEState *s)
{
    return &s->bus;
}

ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int irqnum,
                        DriveInfo *hd0, DriveInfo *hd1)
{
    DeviceState *dev;
    ISADevice *isadev;
    ISAIDEState *s;

    isadev = isa_new(TYPE_ISA_IDE);
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "iobase",  iobase);
    qdev_prop_set_uint32(dev, "iobase2", iobase2);
    qdev_prop_set_uint32(dev, "irq",     irqnum);
    isa_realize_and_unref(isadev, bus, &error_fatal);

    s = ISA_IDE(dev);
    if (hd0) {
        ide_bus_create_drive(&s->bus, 0, hd0);
    }
    if (hd1) {
        ide_bus_create_drive(&s->bus, 1, hd1);
    }
    return isadev;
}

static const Property isa_ide_properties[] = {
    DEFINE_PROP_UINT32("iobase",  ISAIDEState, iobase,  0x1f0),
    DEFINE_PROP_UINT32("iobase2", ISAIDEState, iobase2, 0x3f6),
    DEFINE_PROP_UINT32("irq",     ISAIDEState, irqnum,  14),
    DEFINE_PROP_BOOL("pio-only",  ISAIDEState, pio_only, false),
};

static void isa_ide_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = isa_ide_realizefn;
    dc->unrealize = isa_ide_unrealize;
    dc->fw_name = "ide";
    device_class_set_legacy_reset(dc, isa_ide_reset);
    device_class_set_props(dc, isa_ide_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo isa_ide_info = {
    .name          = TYPE_ISA_IDE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIDEState),
    .class_init    = isa_ide_class_initfn,
};

static void isa_ide_register_types(void)
{
    type_register_static(&isa_ide_info);
}

type_init(isa_ide_register_types)
