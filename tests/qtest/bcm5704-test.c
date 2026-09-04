/*
 * Broadcom BCM5701/BCM5704 PCI tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci.h"
#include "libqos/generic-pcihost.h"
#include "libqos/pci.h"
#include "libqtest.h"

#define IA64_PCI_CONFIG_BASE UINT64_C(0x0000007ff0000000)
#define BCM5701_TEST_SLOT 7U
#define BCM5704_TEST_SLOT 8U
#define BCM57XX_TEST_ROM_BASE (IA64_PCI_MMIO_BASE + 0x02000000)

#define BCM57XX_TEST_PCIX_CAP 0x40
#define BCM57XX_TEST_MISC_HOST_CTRL 0x68
#define BCM57XX_TEST_MISC_HOST_CTRL_RW_MASK 0x000003feU
#define BCM57XX_TEST_PCIX_COMMAND_RW_MASK \
    (PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO | PCI_X_CMD_MAX_READ | \
     PCI_X_CMD_MAX_SPLIT)
#define BCM5704_TEST_PCIX_STATUS_CAPS \
    (PCI_X_STATUS_64BIT | PCI_X_STATUS_133MHZ | (2U << 21) | (1U << 26))

static void bcm5704_qpci_init(QGenericPCIBus *gbus, QTestState *qts)
{
    qpci_init_generic(gbus, qts, NULL, false);
    gbus->ecam_alloc_ptr = IA64_PCI_CONFIG_BASE;
    gbus->bus.mmio_alloc_ptr = IA64_PCI_MMIO_BASE + 0x01000000;
    gbus->bus.mmio_limit = IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE;
}

static void bcm57xx_assert_identity(QPCIDevice *dev, uint16_t device_id,
                                    uint8_t revision,
                                    uint16_t subsystem_vendor_id,
                                    uint16_t subsystem_id,
                                    bool multifunction)
{
    uint8_t header_type;

    g_assert_nonnull(dev);
    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==,
                    BCM57XX_PCI_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==,
                    device_id);
    g_assert_cmphex(qpci_config_readb(dev, PCI_REVISION_ID), ==,
                    revision);
    g_assert_cmphex(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_NETWORK_ETHERNET);
    g_assert_cmphex(qpci_config_readb(dev, PCI_INTERRUPT_PIN), ==, 1);
    if (subsystem_vendor_id) {
        g_assert_cmphex(qpci_config_readw(dev, PCI_SUBSYSTEM_VENDOR_ID), ==,
                        subsystem_vendor_id);
        g_assert_cmphex(qpci_config_readw(dev, PCI_SUBSYSTEM_ID), ==,
                        subsystem_id);
    }

    header_type = qpci_config_readb(dev, PCI_HEADER_TYPE);
    if (multifunction) {
        g_assert_cmphex(header_type & PCI_HEADER_TYPE_MULTI_FUNCTION, !=, 0);
    } else {
        g_assert_cmphex(header_type & PCI_HEADER_TYPE_MULTI_FUNCTION, ==, 0);
    }
}

static void bcm57xx_assert_config_surface(QPCIDevice *dev,
                                          uint16_t chiprev_id,
                                          uint32_t pcix_status)
{
    g_assert_cmphex(qpci_config_readw(dev, PCI_STATUS) &
                    PCI_STATUS_CAP_LIST, ==, PCI_STATUS_CAP_LIST);
    g_assert_cmphex(qpci_config_readb(dev, PCI_CAPABILITY_LIST), ==,
                    BCM57XX_TEST_PCIX_CAP);
    g_assert_cmphex(qpci_config_readb(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_CAP_LIST_ID), ==, PCI_CAP_ID_PCIX);
    g_assert_cmphex(qpci_config_readb(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_CAP_LIST_NEXT), ==, 0);
    g_assert_cmphex(qpci_config_readw(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_CMD), ==, 0);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_MISC_HOST_CTRL), ==,
                    (uint32_t)chiprev_id << 16);
}

static void bcm57xx_mutate_config_surface(QPCIDevice *dev,
                                          uint16_t chiprev_id,
                                          uint32_t pcix_status)
{
    qpci_config_writew(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_CMD, UINT16_MAX);
    g_assert_cmphex(qpci_config_readw(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_CMD), ==,
                    BCM57XX_TEST_PCIX_COMMAND_RW_MASK);

    qpci_config_writel(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_STATUS, 0);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);
    qpci_config_writel(dev, BCM57XX_TEST_PCIX_CAP + PCI_X_STATUS,
                       UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_PCIX_CAP +
                                     PCI_X_STATUS), ==, pcix_status);

    qpci_config_writel(dev, BCM57XX_TEST_MISC_HOST_CTRL, UINT32_MAX);
    g_assert_cmphex(qpci_config_readl(dev, BCM57XX_TEST_MISC_HOST_CTRL), ==,
                    ((uint32_t)chiprev_id << 16) |
                    BCM57XX_TEST_MISC_HOST_CTRL_RW_MASK);
}

static uint32_t bcm57xx_probe_rom_size(QPCIDevice *dev)
{
    uint32_t saved_rom = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    uint32_t rom_mask;

    qpci_config_writel(dev, PCI_ROM_ADDRESS, UINT32_MAX);
    rom_mask = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    qpci_config_writel(dev, PCI_ROM_ADDRESS, saved_rom);

    rom_mask &= PCI_ROM_ADDRESS_MASK;
    return ~rom_mask + 1;
}

static void bcm57xx_assert_apertures(QTestState *qts, QPCIDevice *dev,
                                     uint64_t rom_base)
{
    QPCIBar bar;
    uint64_t bar_size;
    uint32_t bar_value;
    uint32_t saved_rom;

    bar_value = qpci_config_readl(dev, PCI_BASE_ADDRESS_0);
    g_assert_cmphex(bar_value & PCI_BASE_ADDRESS_SPACE, ==,
                    PCI_BASE_ADDRESS_SPACE_MEMORY);
    g_assert_cmphex(bar_value & PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_64);

    bar = qpci_iomap(dev, 0, &bar_size);
    g_assert_cmpuint(bar_size, ==, BCM57XX_MMIO_SIZE);
    qpci_device_enable(dev);
    g_assert_cmphex(qpci_io_readb(dev, bar, 0), ==, UINT8_MAX);
    g_assert_cmphex(qpci_io_readw(dev, bar, 2), ==, UINT16_MAX);
    g_assert_cmphex(qpci_io_readl(dev, bar, 4), ==, UINT32_MAX);
    g_assert_cmphex(qpci_io_readq(dev, bar, 8), ==, UINT64_MAX);

    qpci_io_writel(dev, bar, 0x100, 0x12345678);
    g_assert_cmphex(qpci_io_readl(dev, bar, 0x100), ==, UINT32_MAX);

    g_assert_cmpuint(bcm57xx_probe_rom_size(dev), ==, BCM57XX_ROM_SIZE);
    saved_rom = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    qpci_config_writel(dev, PCI_ROM_ADDRESS,
                       rom_base | PCI_ROM_ADDRESS_ENABLE);
    g_assert_cmphex(qtest_readl(qts, rom_base), ==, UINT32_MAX);
    qtest_writel(qts, rom_base, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, rom_base), ==, UINT32_MAX);
    qpci_config_writel(dev, PCI_ROM_ADDRESS, saved_rom);
}

static void test_bcm57xx_enumeration(void)
{
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *bcm5701;
    QPCIDevice *functions[2];
    unsigned int function;

    qts = qtest_init(
        "-machine ia64-vpc,nvram=none -m 4G -nodefaults -bios none -S "
        "-device bcm5701,bus=pci,addr=7.0,mac=52:54:00:57:01:00 "
        "-device bcm5704,bus=pci,addr=8.0,multifunction=on,"
        "mac=52:54:00:57:04:00 "
        "-device bcm5704,bus=pci,addr=8.1,mac=52:54:00:57:04:01");
    bcm5704_qpci_init(&gbus, qts);

    bcm5701 = qpci_device_find(&gbus.bus,
                               QPCI_DEVFN(BCM5701_TEST_SLOT, 0));
    bcm57xx_assert_identity(bcm5701, BCM5701_PCI_DEVICE_ID,
                            BCM5701_PCI_REVISION,
                            BCM5701_PCI_SUBSYSTEM_VENDOR_ID,
                            BCM5701_PCI_SUBSYSTEM_ID, false);
    bcm57xx_assert_config_surface(bcm5701, 0x0105, 0);

    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        functions[function] = qpci_device_find(
            &gbus.bus, QPCI_DEVFN(BCM5704_TEST_SLOT, function));
        bcm57xx_assert_identity(functions[function], BCM5704_PCI_DEVICE_ID,
                                BCM5704_PCI_REVISION,
                                BCM5704_PCI_SUBSYSTEM_VENDOR_ID,
                                BCM5704_PCI_SUBSYSTEM_ID, function == 0);
        bcm57xx_assert_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
    }

    bcm57xx_assert_apertures(qts, bcm5701, BCM57XX_TEST_ROM_BASE);
    bcm57xx_assert_apertures(qts, functions[0],
                             BCM57XX_TEST_ROM_BASE + BCM57XX_ROM_SIZE);

    bcm57xx_mutate_config_surface(bcm5701, 0x0105, 0);
    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        bcm57xx_mutate_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
    }

    qtest_system_reset(qts);
    bcm57xx_assert_identity(bcm5701, BCM5701_PCI_DEVICE_ID,
                            BCM5701_PCI_REVISION,
                            BCM5701_PCI_SUBSYSTEM_VENDOR_ID,
                            BCM5701_PCI_SUBSYSTEM_ID, false);
    bcm57xx_assert_config_surface(bcm5701, 0x0105, 0);
    g_free(bcm5701);
    for (function = 0; function < G_N_ELEMENTS(functions); function++) {
        bcm57xx_assert_identity(functions[function], BCM5704_PCI_DEVICE_ID,
                                BCM5704_PCI_REVISION,
                                BCM5704_PCI_SUBSYSTEM_VENDOR_ID,
                                BCM5704_PCI_SUBSYSTEM_ID, function == 0);
        bcm57xx_assert_config_surface(
            functions[function], 0x2100,
            BCM5704_TEST_PCIX_STATUS_CAPS |
            QPCI_DEVFN(BCM5704_TEST_SLOT, function));
        g_free(functions[function]);
    }
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm57xx/enumeration", test_bcm57xx_enumeration);
    return g_test_run();
}
