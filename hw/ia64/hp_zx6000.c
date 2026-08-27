/*
 * HP zx6000 workstation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/display/ati_int.h"
#include "hw/ia64/hp_ia64.h"
#include "hw/ia64/hp_int10.h"
#include "hw/ia64/hp_zx6000.h"
#include "hw/ia64/hp_zx6000_pdh.h"
#include "hw/ide/cmd649.h"
#include "hw/ide/ide-bus.h"
#include "hw/ide/pci.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa.h"
#include "hw/pci-host/hp-zx1-mio.h"
#include "hw/scsi/mptsas.h"
#include "hw/scsi/scsi.h"
#include "hw/usb/nec-usb.h"
#include "hw/usb/usb.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/blockdev.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ia64/cpu-qom.h"

#define HP_ZX6000_LOW_RAM_LIMIT       UINT64_C(0x0000000040000000)
#define HP_ZX6000_HIGH_RAM_BASE       UINT64_C(0x0000000100000000)
#define HP_ZX6000_MIN_RAM_SIZE        (512 * MiB)
#define HP_ZX6000_MAX_RAM_SIZE        (24 * GiB)

#define HP_ZX6000_DESCRIPTOR_GPA      UINT64_C(0x0000000000300000)
#define HP_ZX6000_IVT_BASE            UINT64_C(0x0000000000010000)
#define HP_ZX6000_PIB_BASE            UINT64_C(0x00000000fee00000)
#define HP_ZX6000_PIB_SIZE            UINT64_C(0x0000000000200000)
#define HP_ZX6000_LEGACY_IO_BASE      UINT64_C(0x00000ffffc000000)
#define HP_ZX6000_LEGACY_IO_SIZE      UINT64_C(0x0000000004000000)
#define HP_ZX6000_VGA_LEGACY_BASE     UINT64_C(0x00000000000a0000)
#define HP_ZX6000_VGA_LEGACY_SIZE     UINT64_C(0x0000000000020000)
#define HP_ZX6000_VGA_LEGACY_IO_BASE  UINT64_C(0x00000000000003b0)
#define HP_ZX6000_VGA_LEGACY_IO_SIZE  UINT64_C(0x0000000000000030)
#define HP_ZX6000_VBE_LEGACY_IO_BASE  UINT64_C(0x00000000000001ce)
#define HP_ZX6000_VBE_LEGACY_IO_SIZE  UINT64_C(0x0000000000000004)

#define HP_ZX6000_PDH_UART0_BASE      UINT64_C(0x00000000fec00000)
#define HP_ZX6000_PDH_UART1_BASE      UINT64_C(0x00000000fec02000)
#define HP_ZX6000_PDH_NVRAM_BASE      UINT64_C(0x00000000feb00000)
#define HP_ZX6000_PDH_RTC_BASE        UINT64_C(0x00000000feb80000)
#define HP_ZX6000_PDH_CONTROL_BASE    UINT64_C(0x00000000feb82000)
/* zx6000/rx2600 fixed ACPI register page in the system firmware aperture. */
#define HP_ZX6000_PDH_ACPI_PM_BASE    UINT64_C(0x00000000ff5c0000)

#define HP_ZX6000_MIO_SIZE            UINT64_C(0x0000000000010000)
#define HP_ZX6000_AGP_MMIO_SIZE       UINT64_C(0x0000000010000000)
#define HP_ZX6000_PCI_MMIO_SIZE       UINT64_C(0x0000000008000000)
#define HP_ZX6000_PCI_IO_SIZE         UINT64_C(0x0000000000002000)
#define HP_ZX6000_PCI_INPUT_COUNT     10U
#define HP_ZX6000_AGP_INPUT_COUNT     7U
#define HP_ZX6000_IO_SAPIC_VERSION    UINT32_C(0x000a0020)

#define HP_ZX6000_CORE_PCI_ROOT       0U
#define HP_ZX6000_SCSI_ROOT           1U
#define HP_ZX6000_AGP_ROOT            4U
#define HP_ZX6000_ACPI_SCI_INPUT      7U

#define HP_ZX6000_RV100_SLOT          0U
#define HP_ZX6000_OHCI_SLOT           1U
#define HP_ZX6000_CMD649_SLOT         2U
#define HP_ZX6000_I82550_SLOT         3U
#define HP_ZX6000_LSI53C1030_SLOT     1U

G_STATIC_ASSERT(HP_ZX6000_ACPI_SCI_INPUT < HP_ZX6000_PCI_INPUT_COUNT);

#define HP_ZX6000_RV100_FB_BAR        UINT32_C(0xa0000000)
#define HP_ZX6000_RV100_IO_BAR        UINT32_C(0x00008000)
#define HP_ZX6000_RV100_MMIO_BAR      UINT32_C(0xa8000000)

#define HP_ZX6000_CMD649_DATA_BAR     UINT32_C(0x00000d58)
#define HP_ZX6000_CMD649_CONTROL_BAR  UINT32_C(0x00000d64)
#define HP_ZX6000_CMD649_SECONDARY_DATA_BAR \
                                            UINT32_C(0x00000d50)
#define HP_ZX6000_CMD649_SECONDARY_CONTROL_BAR \
                                            UINT32_C(0x00000d60)
#define HP_ZX6000_CMD649_BMDMA_BAR    UINT32_C(0x00000d40)

#define HP_ZX6000_I82550_MMIO_BAR     UINT32_C(0x80020000)
#define HP_ZX6000_I82550_IO_BAR       UINT32_C(0x00000d00)
#define HP_ZX6000_I82550_FLASH_BAR    UINT32_C(0x80040000)
#define HP_ZX6000_OHCI0_MMIO_BAR      UINT32_C(0x80023000)
#define HP_ZX6000_OHCI1_MMIO_BAR      UINT32_C(0x80022000)
#define HP_ZX6000_EHCI_MMIO_BAR       UINT32_C(0x80021000)

#define HP_ZX6000_LSI0_IO_BAR         UINT32_C(0x00002000)
#define HP_ZX6000_LSI0_MMIO_BAR       UINT32_C(0x88000000)
#define HP_ZX6000_LSI0_DIAG_BAR       UINT32_C(0x88010000)
#define HP_ZX6000_LSI1_IO_BAR         UINT32_C(0x00002100)
#define HP_ZX6000_LSI1_MMIO_BAR       UINT32_C(0x88004000)
#define HP_ZX6000_LSI1_DIAG_BAR       UINT32_C(0x88020000)

#define HP_ZX6000_LSI_FUNCTIONS       2U
#define HP_ZX6000_OHCI_FUNCTIONS      2U

typedef struct HPZX6000RootLayout {
    uint64_t cpu_mmio_base;
    uint64_t mmio_size;
    uint16_t io_base;
    uint8_t first_bus;
    uint8_t last_bus;
    uint8_t rope_mask;
    HPZX1IOAMode mode;
    uint64_t bus_mode;
} HPZX6000RootLayout;

struct HPZX6000MachineState {
    HPIA64MachineState parent_obj;

    MemoryRegion low_ram;
    MemoryRegion high_ram;
    MemoryRegion sparse_io;
    MemoryRegion vga_legacy;
    MemoryRegion root_mmio[HP_ZX6000_PCI_ROOT_COUNT];
    AddressSpace root_io[HP_ZX6000_PCI_ROOT_COUNT];
    bool root_io_initialized[HP_ZX6000_PCI_ROOT_COUNT];

    HPZX1MIOState *mio;
    HPZX1IOAState *ioa[HP_ZX6000_PCI_ROOT_COUNT];
    HPZX6000PDHState *pdh;
    PCIDevice *rv100;
    HPIA64Int10 int10;
    PCIDevice *cmd649;
    PCIDevice *i82550;
    PCIDevice *ohci[HP_ZX6000_OHCI_FUNCTIONS];
    PCIDevice *ehci;
    PCIDevice *lsi53c1030[HP_ZX6000_LSI_FUNCTIONS];

    char *nvram_path;
};

static const HPZX6000RootLayout hp_zx6000_roots[] = {
    {
        .cpu_mmio_base = UINT64_C(0x80000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x0000,
        .first_bus = 0x00,
        .last_bus = 0x1f,
        .rope_mask = 0x20,
        .mode = HP_ZX1_IOA_MODE_PCI,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L,
    }, {
        .cpu_mmio_base = UINT64_C(0x88000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x2000,
        .first_bus = 0x20,
        .last_bus = 0x3f,
        .rope_mask = 0x04,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0x90000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x4000,
        .first_bus = 0x40,
        .last_bus = 0x5f,
        .rope_mask = 0x08,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0x98000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0x6000,
        .first_bus = 0x60,
        .last_bus = 0x7f,
        .rope_mask = 0x10,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    }, {
        .cpu_mmio_base = UINT64_C(0xa0000000),
        .mmio_size = HP_ZX6000_AGP_MMIO_SIZE,
        .io_base = 0x8000,
        .first_bus = 0x80,
        .last_bus = 0x9f,
        .rope_mask = 0x03,
        .mode = HP_ZX1_IOA_MODE_AGP,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_AGP,
    }, {
        .cpu_mmio_base = UINT64_C(0xb0000000),
        .mmio_size = HP_ZX6000_PCI_MMIO_SIZE,
        .io_base = 0xa000,
        .first_bus = 0xc0,
        .last_bus = 0xdf,
        .rope_mask = 0x40,
        .mode = HP_ZX1_IOA_MODE_PCIX,
        .bus_mode = HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    (UINT64_C(1) << HP_ZX1_IOA_BUS_MODE_BUS_SHIFT),
    },
};

G_STATIC_ASSERT(G_N_ELEMENTS(hp_zx6000_roots) ==
                HP_ZX6000_PCI_ROOT_COUNT);

static DeviceState *hp_zx6000_add_child(HPZX6000MachineState *s,
                                        const char *name, const char *type)
{
    DeviceState *dev = qdev_new(type);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_unref(OBJECT(dev));
    return dev;
}

static unsigned int hp_zx6000_route_input(unsigned int root,
                                          unsigned int slot,
                                          unsigned int pin)
{
    unsigned int inputs = root == HP_ZX6000_AGP_ROOT ?
                          HP_ZX6000_AGP_INPUT_COUNT :
                          HP_ZX6000_PCI_INPUT_COUNT;

    /* Onboard functions use direct routes; CMD649 00:02.0 uses INTIN 5. */
    if (root == HP_ZX6000_CORE_PCI_ROOT) {
        if (slot == HP_ZX6000_OHCI_SLOT) {
            return pin < 3 ? pin : 2;
        }
        if (slot == HP_ZX6000_CMD649_SLOT && pin == 0) {
            return 5;
        }
        if (slot == HP_ZX6000_I82550_SLOT && pin == 0) {
            return 4;
        }
    }
    if (root == HP_ZX6000_SCSI_ROOT &&
        slot == HP_ZX6000_LSI53C1030_SLOT) {
        return 1 + pin;
    }
    if (root == HP_ZX6000_AGP_ROOT &&
        slot == HP_ZX6000_RV100_SLOT && pin == 0) {
        return 4;
    }

    return (slot + pin) % inputs;
}

static uint32_t hp_zx6000_gsi_base(unsigned int root)
{
    /* MAX_REDIR == 10 makes each MADT GSI range span eleven values. */
    static const uint32_t base[HP_ZX6000_PCI_ROOT_COUNT] = {
        16, 27, 38, 49, 60, 71,
    };

    return base[root];
}

static uint32_t hp_zx6000_device_gsi(unsigned int root, unsigned int slot,
                                     unsigned int pin)
{
    return hp_zx6000_gsi_base(root) +
           hp_zx6000_route_input(root, slot, pin);
}

static hwaddr hp_zx6000_sparse_io_port(hwaddr encoded)
{
    return ((encoded >> 12) << 2) | (encoded & 3);
}

static AddressSpace *hp_zx6000_io_space_for_port(HPZX6000MachineState *s,
                                                 hwaddr port,
                                                 unsigned int size)
{
    bool vga_legacy = port >= HP_ZX6000_VGA_LEGACY_IO_BASE &&
        port < HP_ZX6000_VGA_LEGACY_IO_BASE +
               HP_ZX6000_VGA_LEGACY_IO_SIZE &&
        size <= HP_ZX6000_VGA_LEGACY_IO_BASE +
                HP_ZX6000_VGA_LEGACY_IO_SIZE - port;
    bool vbe_legacy = port >= HP_ZX6000_VBE_LEGACY_IO_BASE &&
        port < HP_ZX6000_VBE_LEGACY_IO_BASE +
               HP_ZX6000_VBE_LEGACY_IO_SIZE &&
        size <= HP_ZX6000_VBE_LEGACY_IO_BASE +
                HP_ZX6000_VBE_LEGACY_IO_SIZE - port;
    unsigned int root;

    /* ZX1 forwards VGA and its VBE extension cycles to the AGP rope. */
    if (s->rv100 && (vga_legacy || vbe_legacy)) {
        return &s->root_io[HP_ZX6000_AGP_ROOT];
    }

    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        const HPZX6000RootLayout *layout = &hp_zx6000_roots[root];
        uint64_t end = (uint64_t)layout->io_base + HP_ZX6000_PCI_IO_SIZE;

        if (port >= layout->io_base && port < end && size <= end - port) {
            return &s->root_io[root];
        }
    }
    return NULL;
}

static MemTxResult hp_zx6000_io_read(AddressSpace *as, hwaddr addr,
                                     uint64_t *value, unsigned int size,
                                     MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        *value = address_space_ldub(as, addr, attrs, &result);
        break;
    case 2:
        *value = address_space_lduw_le(as, addr, attrs, &result);
        break;
    case 4:
        *value = address_space_ldl_le(as, addr, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static MemTxResult hp_zx6000_io_write(AddressSpace *as, hwaddr addr,
                                      uint64_t value, unsigned int size,
                                      MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_DECODE_ERROR;

    switch (size) {
    case 1:
        address_space_stb(as, addr, value, attrs, &result);
        break;
    case 2:
        address_space_stw_le(as, addr, value, attrs, &result);
        break;
    case 4:
        address_space_stl_le(as, addr, value, attrs, &result);
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static MemTxResult hp_zx6000_sparse_io_read(void *opaque, hwaddr addr,
                                            uint64_t *value,
                                            unsigned int size,
                                            MemTxAttrs attrs)
{
    HPZX6000MachineState *s = opaque;
    hwaddr port = hp_zx6000_sparse_io_port(addr);
    AddressSpace *as = hp_zx6000_io_space_for_port(s, port, size);

    if (!as) {
        *value = UINT64_MAX;
        return MEMTX_DECODE_ERROR;
    }
    return hp_zx6000_io_read(as, port, value, size, attrs);
}

static MemTxResult hp_zx6000_sparse_io_write(void *opaque, hwaddr addr,
                                             uint64_t value,
                                             unsigned int size,
                                             MemTxAttrs attrs)
{
    HPZX6000MachineState *s = opaque;
    hwaddr port = hp_zx6000_sparse_io_port(addr);
    AddressSpace *as = hp_zx6000_io_space_for_port(s, port, size);

    return as ? hp_zx6000_io_write(as, port, value, size, attrs) :
                MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps hp_zx6000_sparse_io_ops = {
    .read_with_attrs = hp_zx6000_sparse_io_read,
    .write_with_attrs = hp_zx6000_sparse_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void hp_zx6000_deliver_interrupt(void *opaque,
                                        const HPIOSAPICMessage *message)
{
    MemTxResult result;

    (void)opaque;
    if ((message->address & 7) || message->address < HP_ZX6000_PIB_BASE ||
        message->address > HP_ZX6000_PIB_BASE + HP_ZX6000_PIB_SIZE - 8) {
        return;
    }
    address_space_stq_le(&address_space_memory, message->address,
                         message->data, MEMTXATTRS_UNSPECIFIED, &result);
}

static void hp_zx6000_map_ram(HPZX6000MachineState *s)
{
    MachineState *machine = MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_ZX6000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;

    memory_region_init_alias(&s->low_ram, OBJECT(s), "hp-zx6000.low-ram",
                             machine->ram, 0, low_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->low_ram);
    if (high_size) {
        memory_region_init_alias(&s->high_ram, OBJECT(s),
                                 "hp-zx6000.high-ram", machine->ram,
                                 low_size, high_size);
        memory_region_add_subregion(get_system_memory(),
                                    HP_ZX6000_HIGH_RAM_BASE, &s->high_ram);
    }
}

static bool hp_zx6000_create_zx1(HPZX6000MachineState *s, Error **errp)
{
    HPZX1MIOIOMMUResetConfig iommu_reset = { 0 };
    DeviceState *dev;
    unsigned int root;

    dev = hp_zx6000_add_child(s, "mio", TYPE_HP_ZX1_MIO);
    s->mio = HP_ZX1_MIO(dev);
    if (!hp_zx1_mio_configure_iommu_reset(s->mio, &iommu_reset, errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, HP_ZX6000_MIO_BASE);

    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        const HPZX6000RootLayout *layout = &hp_zx6000_roots[root];
        HPZX1IOASetup setup = {
            .mode = layout->mode,
            .rope_mask = layout->rope_mask,
            .secondary_bus = layout->first_bus,
            .subordinate_bus = layout->last_bus,
            .pci_reset_asserted = false,
            .bus_mode_reset = layout->bus_mode,
            .deliver = hp_zx6000_deliver_interrupt,
            .delivery_opaque = s,
        };
        g_autofree char *name = g_strdup_printf("ioa%u", root);
        g_autofree char *mmio_name =
            g_strdup_printf("hp-zx6000.root%u-mmio", root);
        g_autofree char *io_name =
            g_strdup_printf("hp-zx6000.root%u-io", root);
        unsigned int pin;
        unsigned int slot;

        for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
            for (pin = 0; pin < PCI_NUM_PINS; pin++) {
                setup.intx_route[slot][pin] =
                    hp_zx6000_route_input(root, slot, pin);
            }
        }

        dev = hp_zx6000_add_child(s, name, TYPE_HP_ZX1_IOA);
        s->ioa[root] = HP_ZX1_IOA(dev);
        if (!hp_zx1_ioa_setup(s->ioa[root], &setup, errp) ||
            !sysbus_realize(SYS_BUS_DEVICE(dev), errp) ||
            !hp_zx1_mio_attach_ioa(s->mio, s->ioa[root], errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                        HP_ZX6000_IOA_ADDRESS(root));

        memory_region_init_alias(&s->root_mmio[root], OBJECT(s), mmio_name,
                                 hp_zx1_ioa_pci_mem(s->ioa[root]),
                                 layout->cpu_mmio_base,
                                 layout->mmio_size);
        memory_region_add_subregion(get_system_memory(),
                                    layout->cpu_mmio_base,
                                    &s->root_mmio[root]);
        address_space_init(&s->root_io[root],
                           hp_zx1_ioa_pci_io(s->ioa[root]), io_name);
        s->root_io_initialized[root] = true;
    }
    return true;
}

static bool hp_zx6000_create_pdh(HPZX6000MachineState *s, Error **errp)
{
    static const hwaddr bases[] = {
        HP_ZX6000_PDH_UART0_BASE,
        HP_ZX6000_PDH_UART1_BASE,
        HP_ZX6000_PDH_NVRAM_BASE,
        HP_ZX6000_PDH_RTC_BASE,
        HP_ZX6000_PDH_CONTROL_BASE,
        HP_ZX6000_PDH_ACPI_PM_BASE,
    };
    g_autofree char *nvram_path = ia64_machine_resolve_nvram_path(
        MACHINE(s), s->nvram_path);
    DeviceState *dev = hp_zx6000_add_child(s, "pdh", TYPE_HP_ZX6000_PDH);
    Chardev *chardev;
    unsigned int region;

    G_STATIC_ASSERT(G_N_ELEMENTS(bases) == HP_ZX6000_PDH_MMIO_COUNT);
    s->pdh = HP_ZX6000_PDH(dev);
    chardev = serial_hd(0);
    if (chardev) {
        qdev_prop_set_chr(dev, "chardev0", chardev);
    }
    chardev = serial_hd(1);
    if (chardev) {
        qdev_prop_set_chr(dev, "chardev1", chardev);
    }
    qdev_prop_set_string(dev, HP_ZX6000_PDH_PROP_NVRAM,
                         nvram_path ?: "none");
    if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
        return false;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX, 8));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX, 9));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
        qdev_get_gpio_in_named(DEVICE(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
                               HP_ZX1_IOA_GPIO_INTX,
                               HP_ZX6000_ACPI_SCI_INPUT));
    for (region = 0; region < G_N_ELEMENTS(bases); region++) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), region, bases[region]);
    }

    /* Fixed platform registers must win over an incorrectly assigned BAR. */
    memory_region_add_subregion_overlap(
        hp_zx1_ioa_pci_io(s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
        IA64_PLATFORM_ACPI_PM_IO_BASE,
        hp_zx6000_pdh_acpi_pm_io(s->pdh), 2);

    return true;
}

static bool hp_zx6000_realize_pci_device(PCIDevice *dev, PCIBus *bus,
                                         Error **errp)
{
    return pci_realize_and_unref(dev, bus, errp);
}

static bool hp_zx6000_create_pci_devices(HPZX6000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    PCIBus *agp = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_AGP_ROOT]);
    PCIBus *core = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_CORE_PCI_ROOT]);
    PCIBus *scsi = hp_zx1_ioa_bus(s->ioa[HP_ZX6000_SCSI_ROOT]);
    DriveInfo *ide_drive;
    SCSIBus *scsi_bus;
    BusState *usb_bus;
    unsigned int channel, function, unit;

    s->rv100 = pci_vga_new();
    if (s->rv100) {
        if (!object_dynamic_cast(OBJECT(s->rv100), TYPE_ATI_VGA)) {
            error_setg(errp, "%s supports ATI VGA or no VGA",
                       TYPE_HP_ZX6000_MACHINE);
            object_unref(OBJECT(s->rv100));
            s->rv100 = NULL;
            return false;
        }
        s->rv100->devfn = PCI_DEVFN(HP_ZX6000_RV100_SLOT, 0);
        qdev_prop_set_string(DEVICE(s->rv100), "model", "rv100");
        qdev_prop_set_string(DEVICE(s->rv100), "romfile", "");
        if (!hp_zx6000_realize_pci_device(s->rv100, agp, errp)) {
            return false;
        }
        memory_region_init_alias(
            &s->vga_legacy, OBJECT(s), "hp-zx6000.vga-legacy",
            hp_zx1_ioa_pci_mem(s->ioa[HP_ZX6000_AGP_ROOT]),
            HP_ZX6000_VGA_LEGACY_BASE, HP_ZX6000_VGA_LEGACY_SIZE);
        memory_region_add_subregion_overlap(
            get_system_memory(), HP_ZX6000_VGA_LEGACY_BASE,
            &s->vga_legacy, 1);
    }

    s->cmd649 = pci_new(PCI_DEVFN(HP_ZX6000_CMD649_SLOT, 0),
                        TYPE_CMD649_IDE);
    qdev_prop_set_bit(DEVICE(s->cmd649), "primary-cable80", true);
    if (!hp_zx6000_realize_pci_device(s->cmd649, core, errp)) {
        return false;
    }
    for (channel = 0; channel < ARRAY_SIZE(PCI_IDE(s->cmd649)->bus);
         channel++) {
        IDEBus *ide_bus = &PCI_IDE(s->cmd649)->bus[channel];

        for (unit = 0; unit < ide_bus->max_units; unit++) {
            ide_drive = drive_get(IF_IDE, channel, unit);
            if (ide_drive) {
                ide_bus_create_drive(ide_bus, unit, ide_drive);
            }
        }
    }

    s->i82550 = pci_new(PCI_DEVFN(HP_ZX6000_I82550_SLOT, 0), "i82550");
    qdev_prop_set_string(DEVICE(s->i82550), "romfile", "");
    qemu_configure_nic_device(DEVICE(s->i82550), true, NULL);
    if (!hp_zx6000_realize_pci_device(s->i82550, core, errp)) {
        return false;
    }

    machine->usb |= defaults_enabled() && !machine->usb_disabled;
    if (machine->usb) {
        s->ehci = pci_new_multifunction(
            PCI_DEVFN(HP_ZX6000_OHCI_SLOT, 2),
            TYPE_NEC_USB_EHCI);
        if (!hp_zx6000_realize_pci_device(s->ehci, core, errp)) {
            return false;
        }
        usb_bus = QLIST_FIRST(&DEVICE(s->ehci)->child_bus);
        if (!usb_bus) {
            error_setg(errp, "%s did not create its USB bus",
                       TYPE_NEC_USB_EHCI);
            return false;
        }
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            s->ohci[function] = pci_new_multifunction(
                PCI_DEVFN(HP_ZX6000_OHCI_SLOT, function),
                TYPE_NEC_USB_OHCI);
            qdev_prop_set_string(DEVICE(s->ohci[function]), "masterbus",
                                 usb_bus->name);
            qdev_prop_set_uint32(DEVICE(s->ohci[function]), "firstport",
                                 function * 3);
            if (!hp_zx6000_realize_pci_device(s->ohci[function], core,
                                               errp)) {
                return false;
            }
        }
        if (defaults_enabled()) {
            usb_create_simple(USB_BUS(usb_bus), "usb-kbd");
            usb_create_simple(USB_BUS(usb_bus), "usb-tablet");
        }
    }

    for (function = 0; function < HP_ZX6000_LSI_FUNCTIONS; function++) {
        s->lsi53c1030[function] = pci_new_multifunction(
            PCI_DEVFN(HP_ZX6000_LSI53C1030_SLOT, function),
            TYPE_LSI53C1030);
        if (!hp_zx6000_realize_pci_device(s->lsi53c1030[function], scsi,
                                           errp)) {
            return false;
        }
        scsi_bus = mpt_fusion_get_scsi_bus(s->lsi53c1030[function]);
        scsi_bus_legacy_handle_cmdline(scsi_bus);
    }
    return true;
}

static void hp_zx6000_write_bar(PCIDevice *dev, unsigned int bar,
                                uint32_t address)
{
    pci_default_write_config(dev, PCI_BASE_ADDRESS_0 + bar * 4,
                             address, sizeof(address));
}

static void hp_zx6000_enable_pci_device(PCIDevice *dev, uint16_t command,
                                        uint8_t interrupt_line)
{
    pci_default_write_config(dev, PCI_COMMAND, command, sizeof(command));
    pci_default_write_config(dev, PCI_INTERRUPT_LINE, interrupt_line,
                             sizeof(interrupt_line));
}

static void hp_zx6000_configure_pci(HPZX6000MachineState *s)
{
    static const uint32_t ohci_bars[] = {
        HP_ZX6000_OHCI0_MMIO_BAR,
        HP_ZX6000_OHCI1_MMIO_BAR,
    };
    static const uint32_t lsi_io_bars[] = {
        HP_ZX6000_LSI0_IO_BAR,
        HP_ZX6000_LSI1_IO_BAR,
    };
    static const uint32_t lsi_mmio_bars[] = {
        HP_ZX6000_LSI0_MMIO_BAR,
        HP_ZX6000_LSI1_MMIO_BAR,
    };
    static const uint32_t lsi_diag_bars[] = {
        HP_ZX6000_LSI0_DIAG_BAR,
        HP_ZX6000_LSI1_DIAG_BAR,
    };
    unsigned int function;

    if (s->rv100) {
        hp_zx6000_write_bar(s->rv100, 0, HP_ZX6000_RV100_FB_BAR);
        hp_zx6000_write_bar(s->rv100, 1, HP_ZX6000_RV100_IO_BAR);
        hp_zx6000_write_bar(s->rv100, 2, HP_ZX6000_RV100_MMIO_BAR);
        hp_zx6000_enable_pci_device(
            s->rv100, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
            PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(HP_ZX6000_AGP_ROOT,
                                 HP_ZX6000_RV100_SLOT, 0));
    }

    hp_zx6000_write_bar(s->cmd649, 0, HP_ZX6000_CMD649_DATA_BAR);
    hp_zx6000_write_bar(s->cmd649, 1, HP_ZX6000_CMD649_CONTROL_BAR);
    hp_zx6000_write_bar(s->cmd649, 2,
                        HP_ZX6000_CMD649_SECONDARY_DATA_BAR);
    hp_zx6000_write_bar(s->cmd649, 3,
                        HP_ZX6000_CMD649_SECONDARY_CONTROL_BAR);
    hp_zx6000_write_bar(s->cmd649, 4, HP_ZX6000_CMD649_BMDMA_BAR);
    hp_zx6000_enable_pci_device(
        s->cmd649, PCI_COMMAND_IO | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(HP_ZX6000_CORE_PCI_ROOT,
                             HP_ZX6000_CMD649_SLOT, 0));

    hp_zx6000_write_bar(s->i82550, 0, HP_ZX6000_I82550_MMIO_BAR);
    hp_zx6000_write_bar(s->i82550, 1, HP_ZX6000_I82550_IO_BAR);
    hp_zx6000_write_bar(s->i82550, 2, HP_ZX6000_I82550_FLASH_BAR);
    hp_zx6000_enable_pci_device(
        s->i82550, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
        hp_zx6000_device_gsi(HP_ZX6000_CORE_PCI_ROOT,
                             HP_ZX6000_I82550_SLOT, 0));

    if (s->ehci) {
        for (function = 0; function < HP_ZX6000_OHCI_FUNCTIONS;
             function++) {
            hp_zx6000_write_bar(s->ohci[function], 0,
                                ohci_bars[function]);
            hp_zx6000_enable_pci_device(
                s->ohci[function], PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                hp_zx6000_device_gsi(HP_ZX6000_CORE_PCI_ROOT,
                                     HP_ZX6000_OHCI_SLOT, 0));
        }
        hp_zx6000_write_bar(s->ehci, 0, HP_ZX6000_EHCI_MMIO_BAR);
        hp_zx6000_enable_pci_device(
            s->ehci, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(HP_ZX6000_CORE_PCI_ROOT,
                                 HP_ZX6000_OHCI_SLOT, 3));
    }

    for (function = 0; function < HP_ZX6000_LSI_FUNCTIONS; function++) {
        hp_zx6000_write_bar(s->lsi53c1030[function], 0,
                            lsi_io_bars[function]);
        hp_zx6000_write_bar(s->lsi53c1030[function], 1,
                            lsi_mmio_bars[function]);
        hp_zx6000_write_bar(s->lsi53c1030[function], 2,
                            lsi_diag_bars[function]);
        hp_zx6000_enable_pci_device(
            s->lsi53c1030[function],
            PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
            hp_zx6000_device_gsi(HP_ZX6000_SCSI_ROOT,
                                 HP_ZX6000_LSI53C1030_SLOT, 0));
    }
}

static bool hp_zx6000_init_int10(HPZX6000MachineState *s, Error **errp)
{
    HPIA64Int10Config config;

    if (!s->rv100) {
        return true;
    }

    config = (HPIA64Int10Config) {
        .owner = OBJECT(s),
        .vga = s->rv100,
        .service_io = hp_zx1_ioa_pci_io(
            s->ioa[HP_ZX6000_CORE_PCI_ROOT]),
        .vga_io = &s->root_io[HP_ZX6000_AGP_ROOT],
        .framebuffer_base = HP_ZX6000_RV100_FB_BAR,
        .region_name = "hp-zx6000.int10-pci-io",
    };
    return hp_ia64_int10_init(&s->int10, &config, errp);
}

static void hp_zx6000_add_route(IA64PlatformPciRoute *route,
                                unsigned int root, unsigned int slot,
                                unsigned int pin)
{
    route->Segment = cpu_to_le16(0);
    route->Bus = hp_zx6000_roots[root].first_bus;
    route->Device = slot;
    route->Pin = pin;
    route->Gsi = cpu_to_le32(hp_zx6000_device_gsi(root, slot, pin));
}

static bool hp_zx6000_install_descriptor(HPZX6000MachineState *s,
                                         Error **errp)
{
    MachineState *machine = MACHINE(s);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_size = MIN(machine->ram_size, HP_ZX6000_LOW_RAM_LIMIT);
    uint64_t high_size = machine->ram_size - low_size;
    IA64PlatformDescriptor header = {
        .Magic = cpu_to_le64(IA64_PLATFORM_DESC_MAGIC),
        .FormatRevision = cpu_to_le32(IA64_PLATFORM_DESC_REVISION),
        .PlatformId = cpu_to_le32(IA64_PLATFORM_ID_HP_ZX6000),
        .Flags = cpu_to_le32(IA64_PLATFORM_FLAG_NO_MCFG |
                             IA64_PLATFORM_FLAG_QEMU_EXTENSION |
                             IA64_PLATFORM_FLAG_IDE_DMA),
        .RamSize = cpu_to_le64(machine->ram_size),
        .LowRamEnd = cpu_to_le64(low_size),
        .FirmwareBase = cpu_to_le64(IA64_PLATFORM_FIRMWARE_BASE),
        .FirmwareSize = cpu_to_le64(IA64_PLATFORM_FIRMWARE_SIZE),
        .ProcessorCount = cpu_to_le32(machine->smp.cpus),
        .SocketCount = cpu_to_le32(machine->smp.sockets),
        .CoresPerSocket = cpu_to_le32(machine->smp.cores),
        .ThreadsPerCore = cpu_to_le32(machine->smp.threads),
        .LegacyIoBase = cpu_to_le64(HP_ZX6000_LEGACY_IO_BASE),
        .LegacyIoSize = cpu_to_le64(HP_ZX6000_LEGACY_IO_SIZE),
        .LocalSapicBase = cpu_to_le64(HP_ZX6000_PIB_BASE),
        .LocalSapicSize = cpu_to_le64(HP_ZX6000_PIB_SIZE),
        .ConsoleBase = cpu_to_le64(HP_ZX6000_PDH_UART0_BASE),
        .ConsoleRegisterStride = cpu_to_le32(1),
        .ConsoleClockHz = cpu_to_le32(
            HP_ZX6000_PDH_UART_INPUT_CLOCK_HZ),
        .ConsoleIrq = cpu_to_le32(
            hp_zx6000_gsi_base(HP_ZX6000_CORE_PCI_ROOT) + 8),
        .NvramBase = cpu_to_le64(HP_ZX6000_PDH_NVRAM_BASE),
        .NvramSize = cpu_to_le64(HP_ZX6000_PDH_NVRAM_SIZE),
        .RtcBase = cpu_to_le64(HP_ZX6000_PDH_RTC_BASE),
        .RtcSize = cpu_to_le64(HP_ZX6000_PDH_RTC_SIZE),
        .ControlBase = cpu_to_le64(HP_ZX6000_PDH_CONTROL_BASE),
        .ControlSize = cpu_to_le64(HP_ZX6000_PDH_CONTROL_SIZE),
        .ResetControlOffset = cpu_to_le32(
            HP_ZX6000_PDH_CONTROL_RESET_OFFSET),
        .PoweroffControlOffset = cpu_to_le32(
            HP_ZX6000_PDH_CONTROL_POWEROFF_OFFSET),
        .ControlValue = cpu_to_le32(HP_ZX6000_PDH_CONTROL_VALUE),
        .AcpiPmBase = cpu_to_le64(HP_ZX6000_PDH_ACPI_PM_BASE),
        .AcpiPmSize = cpu_to_le64(IA64_PLATFORM_ACPI_PM_SIZE),
        .AcpiSciGsi = cpu_to_le32(
            hp_zx6000_gsi_base(HP_ZX6000_CORE_PCI_ROOT) +
            HP_ZX6000_ACPI_SCI_INPUT),
    };
    IA64PlatformRamRange ram[2] = {
        {
            .Base = cpu_to_le64(0),
            .Size = cpu_to_le64(low_size),
        }, {
            .Base = cpu_to_le64(HP_ZX6000_HIGH_RAM_BASE),
            .Size = cpu_to_le64(high_size),
        },
    };
    IA64PlatformPciRoot roots[HP_ZX6000_PCI_ROOT_COUNT] = { 0 };
    IA64PlatformIoSapic sapics[HP_ZX6000_PCI_ROOT_COUNT] = { 0 };
    IA64PlatformPciRoute routes[6] = { 0 };
    IA64PlatformDescriptorArrays arrays = {
        .ram_ranges = ram,
        .ram_range_count = high_size ? 2 : 1,
        .pci_roots = roots,
        .pci_root_count = G_N_ELEMENTS(roots),
        .io_sapics = sapics,
        .io_sapic_count = G_N_ELEMENTS(sapics),
        .pci_routes = routes,
    };
    unsigned int route_count = 0;
    unsigned int root;

    if (hp_zx6000_pdh_nvram_persistent(s->pdh)) {
        header.Flags = cpu_to_le32(le32_to_cpu(header.Flags) |
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT);
    }

    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        const HPZX6000RootLayout *layout = &hp_zx6000_roots[root];

        roots[root].Segment = cpu_to_le16(0);
        roots[root].Bus = layout->first_bus;
        roots[root].ConfigType = IA64_PLATFORM_PCI_CONFIG_ZX1_LBA;
        roots[root].Flags = cpu_to_le32(
            IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
            IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO);
        roots[root].ConfigBase = cpu_to_le64(
            HP_ZX6000_IOA_ADDRESS(root));
        roots[root].IoBase = cpu_to_le64(layout->io_base);
        roots[root].IoSize = cpu_to_le64(HP_ZX6000_PCI_IO_SIZE);
        roots[root].IoTranslationOffset = cpu_to_le64(
            HP_ZX6000_LEGACY_IO_BASE);
        roots[root].Mmio32Base = cpu_to_le64(layout->cpu_mmio_base);
        roots[root].Mmio32Size = cpu_to_le64(layout->mmio_size);
        roots[root].DmaBase = cpu_to_le64(0);
        roots[root].DmaSize = cpu_to_le64(low_size);
        roots[root].Rope = cpu_to_le32(ctz32(layout->rope_mask));
        roots[root].BusEnd = layout->last_bus;
        roots[root].Mmio32TranslationOffset = 0;

        sapics[root].Base = cpu_to_le64(
            HP_ZX6000_IOA_ADDRESS(root) +
            IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
        sapics[root].GsiBase = cpu_to_le32(hp_zx6000_gsi_base(root));
        sapics[root].RedirectionEntries = cpu_to_le32(
            root == HP_ZX6000_AGP_ROOT ? HP_ZX6000_AGP_INPUT_COUNT :
            HP_ZX6000_PCI_INPUT_COUNT);
        sapics[root].Version = cpu_to_le32(HP_ZX6000_IO_SAPIC_VERSION);
        sapics[root].Id = root;
    }

    if (s->rv100) {
        hp_zx6000_add_route(&routes[route_count++], HP_ZX6000_AGP_ROOT,
                            HP_ZX6000_RV100_SLOT, 0);
    }
    hp_zx6000_add_route(&routes[route_count++], HP_ZX6000_CORE_PCI_ROOT,
                        HP_ZX6000_CMD649_SLOT, 0);
    hp_zx6000_add_route(&routes[route_count++], HP_ZX6000_CORE_PCI_ROOT,
                        HP_ZX6000_I82550_SLOT, 0);
    if (s->ehci) {
        hp_zx6000_add_route(&routes[route_count++],
                            HP_ZX6000_CORE_PCI_ROOT,
                            HP_ZX6000_OHCI_SLOT, 0);
        hp_zx6000_add_route(&routes[route_count++],
                            HP_ZX6000_CORE_PCI_ROOT,
                            HP_ZX6000_OHCI_SLOT, 3);
    }
    hp_zx6000_add_route(&routes[route_count++], HP_ZX6000_SCSI_ROOT,
                        HP_ZX6000_LSI53C1030_SLOT, 0);
    arrays.pci_route_count = route_count;

    return hp_ia64_machine_install_platform_descriptor(hp, &header,
                                                        &arrays, errp);
}

static IA64BootInfo hp_zx6000_boot_info(unsigned int cpu_index,
                                        uint64_t entry,
                                        uint64_t global_pointer,
                                        void *opaque)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(opaque);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    uint64_t low_ram_end = hp->descriptor_low_ram_end;
    uint64_t assist_base = low_ram_end - IA64_FW_BOOT_STACK_SIZE;
    IA64BootInfo info = {
        .firmware_base = IA64_PLATFORM_FIRMWARE_BASE,
        .firmware_entry = entry,
        .global_pointer = global_pointer,
        .iva = HP_ZX6000_IVT_BASE,
        .bsp = assist_base + IA64_FW_EARLY_RSE_OFFSET +
            cpu_index * IA64_FW_EARLY_RSE_SIZE,
        .stack_pointer = cpu_index == 0 ? low_ram_end - 16 :
            assist_base + IA64_FW_BOOTSTRAP_STACK_TOP_OFFSET - 16 -
            cpu_index * IA64_FW_CPU_STACK_SIZE,
        .rsc = IA64_RSC_MODE,
        .low_ram_size = low_ram_end,
        .io_port_base = HP_ZX6000_LEGACY_IO_BASE,
        .interrupt_block_base = HP_ZX6000_PIB_BASE,
        .powered_off = cpu_index != 0,
        .platform_addresses_valid = true,
    };

    g_assert(hp_ia64_machine_apply_platform_firmware_args(hp, &info));
    return info;
}

static IA64BootInfo hp_zx6000_initial_boot_info(unsigned int cpu_index,
                                                void *opaque)
{
    return hp_zx6000_boot_info(cpu_index, IA64_PLATFORM_FIRMWARE_BASE,
                               IA64_PLATFORM_FIRMWARE_BASE, opaque);
}

static IA64BootInfo hp_zx6000_firmware_boot_info(
    unsigned int cpu_index, uint64_t entry, uint64_t global_pointer,
    void *opaque)
{
    return hp_zx6000_boot_info(cpu_index, entry, global_pointer, opaque);
}

static bool hp_zx6000_build(MachineState *machine, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(machine);
    HPIA64MachineState *hp = HP_IA64_MACHINE(s);
    IA64MachineCpuConfig cpu_config = {
        .alat_full = hp->alat_full,
        .boot_info = hp_zx6000_initial_boot_info,
        .boot_info_opaque = s,
    };

    if (!hp_ia64_machine_validate(hp, errp)) {
        return false;
    }
    if (g_strcmp0(machine->cpu_type,
                  IA64_CPU_TYPE_NAME("madison-zx6000")) != 0) {
        error_setg(errp, "%s requires the Madison zx6000 CPU profile",
                   TYPE_HP_ZX6000_MACHINE);
        return false;
    }
    if (!machine->ram || memory_region_size(machine->ram) !=
        machine->ram_size) {
        error_setg(errp, "%s requires machine RAM", TYPE_HP_ZX6000_MACHINE);
        return false;
    }

    hp_zx6000_map_ram(s);
    ia64_machine_map_pib(OBJECT(s), &hp->pib, "hp-zx6000.pib",
                         HP_ZX6000_PIB_BASE, HP_ZX6000_PIB_SIZE);
    memory_region_add_subregion(get_system_memory(), HP_ZX6000_LEGACY_IO_BASE,
                                &s->sparse_io);

    if (!hp_zx6000_create_zx1(s, errp) ||
        !hp_zx6000_create_pdh(s, errp) ||
        !hp_zx6000_create_pci_devices(s, errp)) {
        return false;
    }
    hp_zx6000_configure_pci(s);
    if (!hp_zx6000_init_int10(s, errp) ||
        !hp_zx6000_install_descriptor(s, errp) ||
        !ia64_machine_create_cpus(machine, &cpu_config, errp) ||
        !ia64_machine_load_firmware(
            machine, IA64_PLATFORM_FIRMWARE_BASE,
            IA64_PLATFORM_FIRMWARE_SIZE, &hp->firmware_size, errp)) {
        return false;
    }
    ia64_machine_init_firmware_notifier(
        &hp->firmware_notifier, machine, IA64_PLATFORM_FIRMWARE_BASE,
        hp->firmware_size, hp_zx6000_firmware_boot_info, NULL, s);
    return true;
}

static void hp_zx6000_machine_init(MachineState *machine)
{
    Error *err = NULL;

    if (!hp_zx6000_build(machine, &err)) {
        error_propagate(&error_fatal, err);
    }
}

static void hp_zx6000_machine_reset(MachineState *machine, ResetType type)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(machine);

    qemu_devices_reset(type);
    hp_zx6000_configure_pci(s);
    hp_ia64_int10_reset(&s->int10);
    ia64_machine_reset_cpus();
}

static char *hp_zx6000_get_nvram(Object *obj, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    (void)errp;
    return g_strdup(s->nvram_path ?: "auto");
}

static void hp_zx6000_set_nvram(Object *obj, const char *value, Error **errp)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    (void)errp;
    g_free(s->nvram_path);
    s->nvram_path = g_strcmp0(value, "auto") == 0 ?
                    NULL : g_strdup(value);
}

static void hp_zx6000_instance_init(Object *obj)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);

    memory_region_init_io(&s->sparse_io, obj, &hp_zx6000_sparse_io_ops, s,
                          "hp-zx6000.sparse-io",
                          HP_ZX6000_LEGACY_IO_SIZE);
    s->sparse_io.disable_reentrancy_guard = true;
}

static void hp_zx6000_instance_finalize(Object *obj)
{
    HPZX6000MachineState *s = HP_ZX6000_MACHINE(obj);
    unsigned int root;

    hp_ia64_int10_destroy(&s->int10);
    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        if (s->root_io_initialized[root]) {
            address_space_destroy(&s->root_io[root]);
        }
    }
    g_free(s->nvram_path);
}

static GlobalProperty hp_zx6000_compat_defaults[] = {
    /* Default HID devices omit optional extended-property descriptors. */
    { "usb-kbd", "msos-desc", "off" },
    { "usb-mouse", "msos-desc", "off" },
    { "usb-tablet", "msos-desc", "off" },
};

static void hp_zx6000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HPIA64MachineClass *hmc = HP_IA64_MACHINE_CLASS(oc);

    (void)data;
    mc->desc = "HP zx6000 workstation";
    mc->init = hp_zx6000_machine_init;
    mc->reset = hp_zx6000_machine_reset;
    mc->default_cpu_type = IA64_CPU_TYPE_NAME("madison-zx6000");
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "hp-zx6000.ram";
    mc->default_display = "ati";
    mc->default_nic = "i82550";
    mc->default_machine_opts = "firmware=ia64-firmware.bin";
    mc->block_default_type = IF_SCSI;
    mc->block_default_cdrom_type = IF_IDE;
    mc->no_serial = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;

    compat_props_add(mc->compat_props, hp_zx6000_compat_defaults,
                     G_N_ELEMENTS(hp_zx6000_compat_defaults));

    hmc->platform_id = IA64_PLATFORM_ID_HP_ZX6000;
    hmc->minimum_ram_size = HP_ZX6000_MIN_RAM_SIZE;
    hmc->maximum_ram_size = HP_ZX6000_MAX_RAM_SIZE;
    hmc->descriptor_gpa = HP_ZX6000_DESCRIPTOR_GPA;

    object_class_property_add_str(oc, "nvram", hp_zx6000_get_nvram,
                                  hp_zx6000_set_nvram);
    object_class_property_set_description(
        oc, "nvram", "Set the zx6000 NVRAM mode: auto, none, or a file path");
}

static const TypeInfo hp_zx6000_machine_type = {
    .name = TYPE_HP_ZX6000_MACHINE,
    .parent = TYPE_HP_IA64_MACHINE,
    .instance_size = sizeof(HPZX6000MachineState),
    .instance_init = hp_zx6000_instance_init,
    .instance_finalize = hp_zx6000_instance_finalize,
    .class_init = hp_zx6000_machine_class_init,
};

static void hp_zx6000_register_types(void)
{
    type_register_static(&hp_zx6000_machine_type);
}

type_init(hp_zx6000_register_types)
