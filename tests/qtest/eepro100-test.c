/*
 * QTest testcase for eepro100 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QEEPRO100 QEEPRO100;

struct QEEPRO100 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

enum {
    E100_SCB_STATUS = 0,
    E100_SCB_COMMAND = 2,
    E100_SCB_POINTER = 4,
    E100_EEPROM_SEMAPHORE = 30,
    E100_CU_HP_START = 0x30,
    E100_RU_RESERVED = 0x08,
    E100_CU_STATE_MASK = 0xc0,
    E100_CB_STATUS_COMPLETE = 0xa000,
    E100_CB_COMMAND_EL = 0x8000,
};

static const char *models[] = {
    "i82550",
    "i82551",
    "i82557a",
    "i82557b",
    "i82557c",
    "i82558a",
    "i82558b",
    "i82559a",
    "i82559b",
    "i82559c",
    "i82559er",
    "i82562",
    "i82801",
};

static void *eepro100_get_driver(void *obj, const char *interface)
{
    QEEPRO100 *eepro100 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &eepro100->dev;
    }

    fprintf(stderr, "%s not present in eepro100\n", interface);
    g_assert_not_reached();
}

static void *eepro100_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QEEPRO100 *eepro100 = g_new0(QEEPRO100, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&eepro100->dev, bus, addr);
    eepro100->obj.get_driver = eepro100_get_driver;

    return &eepro100->obj;
}

static void eepro100_extended_commands(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QEEPRO100 *eepro100 = obj;
    QPCIDevice *dev = &eepro100->dev;
    QTestState *qts = dev->bus->qts;
    QPCIBar bar;
    uint64_t cb_address;
    uint16_t cb[4] = {
        0,
        cpu_to_le16(E100_CB_COMMAND_EL),
        0,
        0,
    };

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    g_assert_cmphex(qpci_io_readw(dev, bar, E100_EEPROM_SEMAPHORE), ==, 0);
    qpci_io_writew(dev, bar, E100_EEPROM_SEMAPHORE, BIT(7));
    g_assert_cmphex(qpci_io_readw(dev, bar, E100_EEPROM_SEMAPHORE), ==,
                    BIT(7));
    qpci_io_writew(dev, bar, E100_EEPROM_SEMAPHORE, 0);

    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_RU_RESERVED);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_COMMAND), ==, 0);

    cb_address = guest_alloc(alloc, sizeof(cb));
    g_assert_cmpuint(cb_address, <=, UINT32_MAX);
    qtest_memwrite(qts, cb_address, cb, sizeof(cb));
    qpci_io_writel(dev, bar, E100_SCB_POINTER, cb_address);
    qpci_io_writeb(dev, bar, E100_SCB_COMMAND, E100_CU_HP_START);
    qtest_memread(qts, cb_address, cb, sizeof(cb));
    g_assert_cmphex(le16_to_cpu(cb[0]), ==, E100_CB_STATUS_COMPLETE);
    g_assert_cmphex(qpci_io_readb(dev, bar, E100_SCB_STATUS) &
                    E100_CU_STATE_MASK, ==, 0);

    guest_free(alloc, cb_address);
}

static void eepro100_register_nodes(void)
{
    int i;
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });
    for (i = 0; i < ARRAY_SIZE(models); i++) {
        qos_node_create_driver(models[i], eepro100_create);
        qos_node_consumes(models[i], "pci-bus", &opts);
        qos_node_produces(models[i], "pci-device");
    }

    qos_add_test("extended-commands", "i82559c",
                 eepro100_extended_commands, NULL);
}

libqos_init(eepro100_register_nodes);
