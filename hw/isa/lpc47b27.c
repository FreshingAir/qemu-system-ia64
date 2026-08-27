/*
 * LPC47B27x ISA integration
 *
 * Models configuration ports 0x2e/0x2f, Serial 1, and the keyboard logical
 * device. Other logical devices are not implemented. Serial and keyboard
 * CRF0 mode values are retained for readback and migration, but their effects
 * on device behavior are not implemented.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/char/serial.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/input/i8042.h"
#include "hw/isa/isa.h"
#include "hw/isa/lpc47b27.h"
#include "hw/isa/lpc47b27_config.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#define LPC47B27_CONFIG_IO_SIZE          2
#define LPC47B27_UART_IO_SIZE            8
#define LPC47B27_I8042_MMIO_SIZE         5
#define LPC47B27_I8042_COMMAND_MASK      4
#define LPC47B27_I8042_DATA_PORT         UINT16_C(0x0060)
#define LPC47B27_I8042_COMMAND_PORT      UINT16_C(0x0064)
#define LPC47B27_UART_CLOCK_DIVISOR       16U
#define LPC47B27_UART_INPUT_CLOCK_HZ      UINT32_C(1843200)

enum LPC47B27IRQSource {
    LPC47B27_IRQ_SOURCE_UART,
    LPC47B27_IRQ_SOURCE_I8042_KBD,
    LPC47B27_IRQ_SOURCE_I8042_MOUSE,
    LPC47B27_IRQ_SOURCE_COUNT = LPC47B27_IRQ_ROUTER_SOURCE_COUNT,
};

struct LPC47B27ISAState {
    ISADevice parent_obj;

    LPC47B27ConfigState config;
    MemoryRegion config_io;
    SerialState uart;
    MMIOKBDState i8042;
    MemoryRegion i8042_data_alias;
    MemoryRegion i8042_command_alias;

    ISABus *isa_bus;
    MemoryRegion *io_space;
    uint16_t config_iobase;
    uint16_t uart_mapped_base;
    uint32_t uart_input_clock_hz;
    uint8_t routed_irq[LPC47B27_IRQ_SOURCE_COUNT];
    bool source_level[LPC47B27_IRQ_SOURCE_COUNT];
    bool irq_level[ISA_NUM_IRQS];

    bool config_mapped;
    bool uart_mapped;
    bool i8042_mapped;
    bool uart_realized;
    bool i8042_realized;
    bool resources_ready;
};

static void lpc47b27_update_irq(LPC47B27ISAState *s, uint8_t irq)
{
    bool level;

    if (!irq) {
        return;
    }
    level = lpc47b27_irq_router_level(s->routed_irq, s->source_level, irq);
    if (s->irq_level[irq] != level) {
        s->irq_level[irq] = level;
        qemu_set_irq(isa_bus_get_irq(s->isa_bus, irq), level);
    }
}

static void lpc47b27_set_irq_target(LPC47B27ISAState *s,
                                    enum LPC47B27IRQSource source,
                                    uint8_t irq, bool drive)
{
    uint8_t old_irq = s->routed_irq[source];

    g_assert(irq < ISA_NUM_IRQS);
    if (old_irq == irq) {
        return;
    }

    s->routed_irq[source] = irq;
    if (drive) {
        lpc47b27_update_irq(s, old_irq);
        lpc47b27_update_irq(s, irq);
    }
}

static void lpc47b27_set_irq_source(void *opaque, int n, int level)
{
    LPC47B27ISAState *s = LPC47B27_ISA(opaque);

    g_assert(n >= 0 && n < LPC47B27_IRQ_SOURCE_COUNT);
    level = !!level;
    if (s->source_level[n] == level) {
        return;
    }

    s->source_level[n] = level;
    if (s->routed_irq[n]) {
        lpc47b27_update_irq(s, s->routed_irq[n]);
    }
}

static void lpc47b27_disconnect_irqs(LPC47B27ISAState *s)
{
    unsigned i;

    for (i = 0; i < LPC47B27_IRQ_SOURCE_COUNT; i++) {
        lpc47b27_set_irq_target(s, i, 0, true);
    }
}

static void lpc47b27_rebuild_irq_cache(LPC47B27ISAState *s)
{
    unsigned irq;

    memset(s->irq_level, 0, sizeof(s->irq_level));
    for (irq = 1; irq < ISA_NUM_IRQS; irq++) {
        s->irq_level[irq] = lpc47b27_irq_router_level(
            s->routed_irq, s->source_level, irq);
    }
}

static void lpc47b27_unmap_uart(LPC47B27ISAState *s)
{
    if (s->uart_mapped) {
        memory_region_del_subregion(s->io_space, &s->uart.io);
        s->uart_mapped = false;
    }
}

static void lpc47b27_unmap_i8042(LPC47B27ISAState *s)
{
    if (s->i8042_mapped) {
        memory_region_del_subregion(s->io_space,
                                    &s->i8042_command_alias);
        memory_region_del_subregion(s->io_space, &s->i8042_data_alias);
        s->i8042_mapped = false;
    }
}

static void lpc47b27_sync_uart(LPC47B27ISAState *s, bool drive_irq)
{
    LPC47B27UartConfig config;
    bool decode;

    lpc47b27_config_get_uart(&s->config, &config);
    decode = config.active && config.base_valid;

    lpc47b27_set_irq_target(s, LPC47B27_IRQ_SOURCE_UART,
                            decode ? config.irq : 0, drive_irq);

    memory_region_transaction_begin();
    if (s->uart_mapped &&
        (!decode || s->uart_mapped_base != config.base)) {
        lpc47b27_unmap_uart(s);
    }
    if (decode && !s->uart_mapped) {
        memory_region_add_subregion(s->io_space, config.base, &s->uart.io);
        s->uart_mapped_base = config.base;
        s->uart_mapped = true;
    }
    memory_region_transaction_commit();
}

static void lpc47b27_sync_i8042(LPC47B27ISAState *s, bool drive_irq)
{
    LPC47B27I8042Config config;

    lpc47b27_config_get_i8042(&s->config, &config);

    lpc47b27_set_irq_target(s, LPC47B27_IRQ_SOURCE_I8042_KBD,
                            config.active ? config.keyboard_irq : 0,
                            drive_irq);
    lpc47b27_set_irq_target(s, LPC47B27_IRQ_SOURCE_I8042_MOUSE,
                            config.active ? config.mouse_irq : 0,
                            drive_irq);

    memory_region_transaction_begin();
    if (!config.active) {
        lpc47b27_unmap_i8042(s);
    } else if (!s->i8042_mapped) {
        memory_region_add_subregion(s->io_space, LPC47B27_I8042_DATA_PORT,
                                    &s->i8042_data_alias);
        memory_region_add_subregion(s->io_space,
                                    LPC47B27_I8042_COMMAND_PORT,
                                    &s->i8042_command_alias);
        s->i8042_mapped = true;
    }
    memory_region_transaction_commit();
}

static uint64_t lpc47b27_config_io_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    LPC47B27ISAState *s = opaque;

    return lpc47b27_config_read(&s->config,
        addr ? LPC47B27_CONFIG_DATA_PORT : LPC47B27_CONFIG_INDEX_PORT);
}

static void lpc47b27_config_io_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    LPC47B27ISAState *s = opaque;
    LPC47B27ConfigChange change;

    change = lpc47b27_config_write(&s->config,
        addr ? LPC47B27_CONFIG_DATA_PORT : LPC47B27_CONFIG_INDEX_PORT,
        value);
    if (change & LPC47B27_CONFIG_CHANGE_UART_RESOURCES) {
        lpc47b27_sync_uart(s, true);
    }
    if (change & LPC47B27_CONFIG_CHANGE_I8042_RESOURCES) {
        lpc47b27_sync_i8042(s, true);
    }
}

static const MemoryRegionOps lpc47b27_config_io_ops = {
    .read = lpc47b27_config_io_read,
    .write = lpc47b27_config_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static bool lpc47b27_migration_config_valid(const LPC47B27ConfigState *c)
{
    return c->serial1_activate <= 1 &&
           !(c->serial1_base_msb & ~0x0fU) &&
           !(c->serial1_base_lsb & ~0xf8U) &&
           c->serial1_irq < ISA_NUM_IRQS &&
           !(c->serial1_mode & ~0x83U) &&
           c->keyboard_activate <= 1 &&
           c->keyboard_irq < ISA_NUM_IRQS &&
           c->mouse_irq < ISA_NUM_IRQS &&
           !(c->keyboard_mode & ~0xfcU);
}

static int lpc47b27_pre_load(void *opaque)
{
    LPC47B27ISAState *s = opaque;

    /* Clear derived decode and routing state before loading migration fields. */
    memset(s->routed_irq, 0, sizeof(s->routed_irq));
    memset(s->irq_level, 0, sizeof(s->irq_level));
    memory_region_transaction_begin();
    lpc47b27_unmap_uart(s);
    lpc47b27_unmap_i8042(s);
    memory_region_transaction_commit();
    return 0;
}

static int lpc47b27_post_load(void *opaque, int version_id)
{
    LPC47B27ISAState *s = opaque;

    if (!lpc47b27_migration_config_valid(&s->config)) {
        error_report("invalid LPC47B27x configuration in migration stream");
        return -EINVAL;
    }

    /* Rebuild derived decode and routing state without driving IRQ sinks. */
    lpc47b27_sync_uart(s, false);
    lpc47b27_sync_i8042(s, false);
    lpc47b27_rebuild_irq_cache(s);
    return 0;
}

static const VMStateDescription vmstate_lpc47b27_isa = {
    .name = "lpc47b27-isa",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = lpc47b27_pre_load,
    .post_load = lpc47b27_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_EQUAL(config_iobase, LPC47B27ISAState),
        VMSTATE_UINT32_EQUAL(uart_input_clock_hz, LPC47B27ISAState),
        VMSTATE_BOOL(config.config_mode, LPC47B27ISAState),
        VMSTATE_UINT8(config.index, LPC47B27ISAState),
        VMSTATE_UINT8(config.ldn, LPC47B27ISAState),
        VMSTATE_UINT8(config.serial1_activate, LPC47B27ISAState),
        VMSTATE_UINT8(config.serial1_base_msb, LPC47B27ISAState),
        VMSTATE_UINT8(config.serial1_base_lsb, LPC47B27ISAState),
        VMSTATE_UINT8(config.serial1_irq, LPC47B27ISAState),
        VMSTATE_UINT8(config.serial1_mode, LPC47B27ISAState),
        VMSTATE_UINT8(config.keyboard_activate, LPC47B27ISAState),
        VMSTATE_UINT8(config.keyboard_irq, LPC47B27ISAState),
        VMSTATE_UINT8(config.mouse_irq, LPC47B27ISAState),
        VMSTATE_UINT8(config.keyboard_mode, LPC47B27ISAState),
        VMSTATE_BOOL_ARRAY(source_level, LPC47B27ISAState,
                           LPC47B27_IRQ_SOURCE_COUNT),
        VMSTATE_STRUCT(uart, LPC47B27ISAState, 0, vmstate_serial,
                       SerialState),
        VMSTATE_END_OF_LIST()
    },
};

static void lpc47b27_reset(DeviceState *dev)
{
    LPC47B27ISAState *s = LPC47B27_ISA(dev);

    lpc47b27_config_reset(&s->config);
    if (s->resources_ready) {
        lpc47b27_sync_uart(s, true);
        lpc47b27_sync_i8042(s, true);
    }
}

static void lpc47b27_realize(DeviceState *dev, Error **errp)
{
    LPC47B27ISAState *s = LPC47B27_ISA(dev);
    ISADevice *isadev = ISA_DEVICE(dev);
    MemoryRegion *i8042_region;

    s->isa_bus = isa_bus_from_device(isadev);
    if (!s->isa_bus->irqs_in) {
        error_setg(errp,
                   "LPC47B27x requires registered ISA interrupt inputs");
        return;
    }
    if (s->config_iobase == UINT16_MAX) {
        error_setg(errp, "LPC47B27x configuration port pair overflows");
        return;
    }
    if (!s->uart_input_clock_hz ||
        s->uart_input_clock_hz % LPC47B27_UART_CLOCK_DIVISOR) {
        error_setg(errp,
                   "LPC47B27x UART input clock must be a nonzero multiple "
                   "of %u Hz", LPC47B27_UART_CLOCK_DIVISOR);
        return;
    }
    s->io_space = isa_address_space_io(isadev);

    s->uart.irq = qdev_get_gpio_in_named(dev, "irq-source",
                                         LPC47B27_IRQ_SOURCE_UART);
    qdev_prop_set_uint32(DEVICE(&s->uart), "baudbase",
                         s->uart_input_clock_hz /
                         LPC47B27_UART_CLOCK_DIVISOR);
    if (!qdev_realize(DEVICE(&s->uart), NULL, errp)) {
        return;
    }
    s->uart_realized = true;

    qdev_prop_set_uint64(DEVICE(&s->i8042), "mask",
                         LPC47B27_I8042_COMMAND_MASK);
    qdev_prop_set_uint32(DEVICE(&s->i8042), "size",
                         LPC47B27_I8042_MMIO_SIZE);
    qdev_connect_gpio_out(DEVICE(&s->i8042), I8042_KBD_IRQ,
        qdev_get_gpio_in_named(dev, "irq-source",
                               LPC47B27_IRQ_SOURCE_I8042_KBD));
    qdev_connect_gpio_out(DEVICE(&s->i8042), I8042_MOUSE_IRQ,
        qdev_get_gpio_in_named(dev, "irq-source",
                               LPC47B27_IRQ_SOURCE_I8042_MOUSE));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i8042), errp)) {
        qdev_unrealize(DEVICE(&s->uart));
        s->uart_realized = false;
        return;
    }
    s->i8042_realized = true;

    memory_region_init_io(&s->uart.io, OBJECT(s), &serial_io_ops, &s->uart,
                          "lpc47b27-uart", LPC47B27_UART_IO_SIZE);
    i8042_region = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i8042), 0);
    memory_region_init_alias(&s->i8042_data_alias, OBJECT(s),
                             "lpc47b27-i8042-data", i8042_region, 0, 1);
    memory_region_init_alias(&s->i8042_command_alias, OBJECT(s),
                             "lpc47b27-i8042-command", i8042_region,
                             LPC47B27_I8042_COMMAND_MASK, 1);

    isa_register_ioport(isadev, &s->config_io, s->config_iobase);
    s->config_mapped = true;
    s->resources_ready = true;
    lpc47b27_sync_uart(s, true);
    lpc47b27_sync_i8042(s, true);
}

static void lpc47b27_unrealize(DeviceState *dev)
{
    LPC47B27ISAState *s = LPC47B27_ISA(dev);

    lpc47b27_disconnect_irqs(s);
    memory_region_transaction_begin();
    lpc47b27_unmap_uart(s);
    lpc47b27_unmap_i8042(s);
    if (s->config_mapped) {
        memory_region_del_subregion(s->io_space, &s->config_io);
        s->config_mapped = false;
    }
    memory_region_transaction_commit();
    s->resources_ready = false;

    if (s->i8042_realized) {
        qdev_unrealize(DEVICE(&s->i8042));
        s->i8042_realized = false;
    }
    if (s->uart_realized) {
        qdev_unrealize(DEVICE(&s->uart));
        s->uart_realized = false;
    }
    s->isa_bus = NULL;
    s->io_space = NULL;
}

static void lpc47b27_init(Object *obj)
{
    LPC47B27ISAState *s = LPC47B27_ISA(obj);

    lpc47b27_config_reset(&s->config);
    memory_region_init_io(&s->config_io, obj, &lpc47b27_config_io_ops, s,
                          "lpc47b27-config", LPC47B27_CONFIG_IO_SIZE);

    object_initialize_child(obj, "uart", &s->uart, TYPE_SERIAL);
    object_property_add_alias(obj, "chardev", OBJECT(&s->uart), "chardev");
    object_property_add_alias(obj, "wakeup", OBJECT(&s->uart), "wakeup");
    object_initialize_child(obj, "i8042", &s->i8042, TYPE_I8042_MMIO);
    qdev_init_gpio_in_named(DEVICE(obj), lpc47b27_set_irq_source,
                            "irq-source", LPC47B27_IRQ_SOURCE_COUNT);
}

static const Property lpc47b27_properties[] = {
    DEFINE_PROP_UINT16(LPC47B27_ISA_PROP_CONFIG_IOBASE, LPC47B27ISAState,
                       config_iobase, LPC47B27_CONFIG_INDEX_PORT),
    DEFINE_PROP_UINT32(LPC47B27_ISA_PROP_UART_INPUT_CLOCK_HZ,
                       LPC47B27ISAState, uart_input_clock_hz,
                       LPC47B27_UART_INPUT_CLOCK_HZ),
};

static void lpc47b27_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "LPC47B27x ISA integration";
    dc->realize = lpc47b27_realize;
    dc->unrealize = lpc47b27_unrealize;
    dc->vmsd = &vmstate_lpc47b27_isa;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, lpc47b27_reset);
    device_class_set_props(dc, lpc47b27_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo lpc47b27_info = {
    .name = TYPE_LPC47B27_ISA,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(LPC47B27ISAState),
    .instance_init = lpc47b27_init,
    .class_init = lpc47b27_class_init,
};

static void lpc47b27_register_types(void)
{
    type_register_static(&lpc47b27_info);
}

type_init(lpc47b27_register_types)
