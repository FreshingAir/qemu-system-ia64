/*
 * HP zx6000 machine qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/acpi/acpi.h"
#include "hw/display/bochs-vbe.h"
#include "hw/ia64/hp_zx6000.h"
#include "hw/ia64/hp_zx6000_pdh.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define TEST_FIRMWARE_ENV "QTEST_IA64_FIRMWARE"

#define ZX6000_DESCRIPTOR_GPA UINT64_C(0x00300000)
#define ZX6000_LOW_RAM_SIZE   UINT64_C(0x40000000)
#define ZX6000_HIGH_RAM_BASE  UINT64_C(0x100000000)
#define ZX6000_NVRAM_BASE     UINT64_C(0xfeb00000)
#define ZX6000_NVRAM_PREFIX_SIZE 0x00010000U
#define ZX6000_ACPI_PM_BASE   UINT64_C(0xff5c0000)
#define ZX6000_ACPI_SCI_GSI   23U
#define ZX6000_SPARSE_IO_BASE UINT64_C(0x00000ffffc000000)
#define ZX6000_INT10_ROM_BASE UINT64_C(0x000c0000)
#define ZX6000_INT10_ROM_SIZE 0x0800U
#define ZX6000_INT10_PCIR_OFFSET 0x0020U
#define ZX6000_INT10_HANDLER_OFFSET 0x0100U
#define ZX6000_INT10_MODE_LIST_OFFSET 0x01d0U
#define ZX6000_INT10_VECTOR_ADDR UINT64_C(0x00000040)
#define ZX6000_INT10_IO_BASE  0x01e0U
#define ZX6000_INT10_IO_EXEC  (ZX6000_INT10_IO_BASE + 0x0cU)
#define ZX6000_INT10_IO_DATA  (ZX6000_INT10_IO_BASE + 0x0eU)
#define ZX6000_INT10_TRIGGER  0x4941U
#define ZX6000_VBE2_SIGNATURE UINT32_C(0x32454256)
#define ZX6000_RV100_FB_BASE  UINT64_C(0xa0000000)
#define ZX6000_VGA_PLANAR_SIZE UINT64_C(0x00040000)
#define ZX6000_VGA_LEGACY_BASE UINT64_C(0x000a0000)
#define ZX6000_BDA_VIDEO_MODE UINT64_C(0x00000449)
#define ZX6000_BDA_VIDEO_COLUMNS UINT64_C(0x0000044a)
#define ZX6000_BDA_VIDEO_PAGE_SIZE UINT64_C(0x0000044c)
#define ZX6000_BDA_VIDEO_PAGE_START UINT64_C(0x0000044e)
#define ZX6000_BDA_VIDEO_ROWS UINT64_C(0x00000484)
#define ZX6000_BDA_CHARACTER_HEIGHT UINT64_C(0x00000485)
#define ZX6000_BDA_VIDEO_CONTROL UINT64_C(0x00000487)
#define ZX6000_VBE_INDEX_PORT 0x01ceU
#define ZX6000_VBE_DATA_PORT  0x01d0U
#define ZX6000_VBE_ENABLE_INDEX 0x0004U
#define ZX6000_VGA_MISC_READ_PORT  0x03ccU
#define ZX6000_VGA_SEQ_INDEX_PORT  0x03c4U
#define ZX6000_VGA_SEQ_DATA_PORT   0x03c5U
#define ZX6000_VGA_CRTC_INDEX_PORT 0x03d4U
#define ZX6000_VGA_CRTC_DATA_PORT  0x03d5U
#define ZX6000_VGA_GFX_INDEX_PORT  0x03ceU
#define ZX6000_VGA_GFX_DATA_PORT   0x03cfU

#define ZX6000_CMD649_CNTRL    0x51
#define ZX6000_CMD649_BMIDECSR 0x79

#define ZX6000_LSI0_MMIO_BASE              UINT64_C(0x88000000)
#define ZX6000_MPT_DOORBELL_OFFSET          0x00U
#define ZX6000_MPT_INTERRUPT_STATUS_OFFSET  0x30U
#define ZX6000_MPT_DOORBELL_ACTIVE          0x08000000U
#define ZX6000_MPT_DOORBELL_INTERRUPT       0x00000001U
#define ZX6000_MPT_HANDSHAKE_IOC_FACTS      0x42030000U
#define ZX6000_MPT_IOC_FACTS_REQUEST_WORD0  0x03000000U

typedef struct ZX6000Int10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
    uint32_t input_signature;
} ZX6000Int10Registers;

static void zx6000_assert_ppm_pixel(const char *filename, unsigned width,
                                    unsigned height, unsigned x, unsigned y,
                                    uint8_t red, uint8_t green, uint8_t blue)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;
    const uint8_t *pixel;
    char *end;
    unsigned actual_width;
    unsigned actual_height;
    unsigned maximum;
    gsize length;

    g_assert_true(g_file_get_contents(filename, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_true(g_str_has_prefix(contents, "P6\n"));
    actual_width = g_ascii_strtoull(contents + 3, &end, 10);
    actual_height = g_ascii_strtoull(end, &end, 10);
    maximum = g_ascii_strtoull(end, &end, 10);
    g_assert_cmpuint(actual_width, ==, width);
    g_assert_cmpuint(actual_height, ==, height);
    g_assert_cmpuint(maximum, ==, 255);
    g_assert_cmpuint(x, <, width);
    g_assert_cmpuint(y, <, height);
    g_assert_cmpuint(length - (end - contents), >,
                     (gsize)width * height * 3);
    g_assert_true(g_ascii_isspace(*end));
    if (*end++ == '\r' && *end == '\n') {
        end++;
    }
    pixel = (const uint8_t *)end + ((gsize)y * width + x) * 3;
    g_assert_cmphex(pixel[0], ==, red);
    g_assert_cmphex(pixel[1], ==, green);
    g_assert_cmphex(pixel[2], ==, blue);
}

static const uint8_t zx6000_first_bus[] = {
    0x00, 0x20, 0x40, 0x60, 0x80, 0xc0,
};

static const uint8_t zx6000_last_bus[] = {
    0x1f, 0x3f, 0x5f, 0x7f, 0x9f, 0xdf,
};

static const uint64_t zx6000_cpu_mmio_base[] = {
    UINT64_C(0x80000000), UINT64_C(0x88000000),
    UINT64_C(0x90000000), UINT64_C(0x98000000),
    UINT64_C(0xa0000000), UINT64_C(0xb0000000),
};

static const uint64_t zx6000_mmio_size[] = {
    UINT64_C(0x08000000), UINT64_C(0x08000000),
    UINT64_C(0x08000000), UINT64_C(0x08000000),
    UINT64_C(0x10000000), UINT64_C(0x08000000),
};

static QTestState *zx6000_start(void)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-zx6000,nvram=none -smp 2 -S "
                       "-display none -serial none -net none -bios %s",
                       firmware);
}

static QTestState *zx6000_start_with_storage(const char *storage)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-zx6000,nvram=none -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       firmware, storage);
}

static QTestState *zx6000_start_with_options(const char *machine_options,
                                             const char *options)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *nvram_options = strstr(machine_options, "nvram=") ?
        "" : ",nvram=none";

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-zx6000%s%s -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       nvram_options, machine_options, firmware, options);
}

static void zx6000_assert_block_devices(QTestState *qts,
                                        const char *const *expected,
                                        size_t expected_count)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-block'}");
    QList *blocks = qdict_get_qlist(response, "return");
    size_t i;

    g_assert_cmpuint(qlist_size(blocks), ==, expected_count);
    for (i = 0; i < expected_count; i++) {
        QListEntry *entry;
        bool found = false;

        QLIST_FOREACH_ENTRY(blocks, entry) {
            QDict *block = qobject_to(QDict, qlist_entry_obj(entry));

            if (g_str_equal(qdict_get_str(block, "device"), expected[i])) {
                found = true;
                break;
            }
        }
        g_assert_true(found);
    }
}

static unsigned int zx6000_count_unattached_children(QTestState *qts,
                                                      const char *qom_type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':"
             " {'path':'/machine/unattached'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;
    unsigned int count = 0;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            count++;
        }
    }
    return count;
}

static char *zx6000_find_unattached_child(QTestState *qts,
                                          const char *qom_type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':"
             " {'path':'/machine/unattached'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    return NULL;
}

static bool zx6000_qom_has_child(QTestState *qts, const char *name,
                                 const char *type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':{'path':'/machine'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "name"), name) &&
            g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return true;
        }
    }
    return false;
}

static void zx6000_assert_start_fails(const char *option,
                                      const char *value,
                                      const char *message)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", "hp-zx6000,nvram=none",
        "-bios", firmware,
        option, value,
        "-display", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_nonnull(firmware);
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void zx6000_assert_nvram_start_fails(const char *path,
                                             const char *message)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    g_autofree char *machine =
        g_strdup_printf("hp-zx6000,nvram=%s", path);
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", machine,
        "-m", "512M",
        "-smp", "1",
        "-bios", firmware,
        "-S",
        "-display", "none",
        "-serial", "none",
        "-net", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_nonnull(firmware);
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void test_hp_zx6000_machine_identity(void)
{
    QTestState *qts = zx6000_start();
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-machines'}");
    QList *machines = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool found = false;

    QLIST_FOREACH_ENTRY(machines, entry) {
        QDict *machine = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(machine, "name"), "hp-zx6000")) {
            g_assert_cmpstr(qdict_get_str(machine, "default-cpu-type"), ==,
                            "madison-zx6000-ia64-cpu");
            g_assert_cmpint(qdict_get_int(machine, "cpu-max"), ==, 2);
            g_assert_cmpstr(qdict_get_str(machine, "default-ram-id"), ==,
                            "hp-zx6000.ram");
            found = true;
            break;
        }
    }
    g_assert_true(found);
    g_assert_true(zx6000_qom_has_child(qts, "mio", "hp-zx1-mio"));
    g_assert_true(zx6000_qom_has_child(qts, "pdh", "hp-zx6000-pdh"));
    g_assert_false(zx6000_qom_has_child(qts, "vpc", "ia64-vpc"));
    qtest_quit(qts);
}

static void test_hp_zx6000_constraints(void)
{
    zx6000_assert_start_fails("-smp", "3", "max CPUs supported");
    zx6000_assert_start_fails(
        "-smp", "2,sockets=1,cores=2",
        "requires one single-core, single-thread processor per socket");
    zx6000_assert_start_fails("-m", "256M", "at least 512 MiB");
}

static void test_hp_zx6000_storage_defaults(void)
{
    static const char *const automatic[] = {
        "scsi0-hd0", "ide0-cd0",
    };
    static const char *const explicit_topology[] = {
        "ide0-hd0", "ide0-cd1", "ide1-cd0", "ide1-hd1",
        "scsi0-cd6", "scsi1-hd6",
    };
    static const char *const cdrom_shortcut[] = {
        "ide1-cd0",
    };
    QTestState *qts = zx6000_start_with_storage("");

    zx6000_assert_block_devices(qts, NULL, 0);
    qtest_quit(qts);

    qts = zx6000_start_with_storage(
        "-drive media=disk,file=null-co://,format=raw "
        "-drive media=cdrom,file=null-co://,format=raw");
    zx6000_assert_block_devices(qts, automatic, G_N_ELEMENTS(automatic));
    qtest_quit(qts);

    qts = zx6000_start_with_storage(
        "-drive if=ide,bus=0,unit=0,media=disk,file=null-co://,format=raw "
        "-drive if=ide,bus=0,unit=1,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=0,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=1,media=disk,file=null-co://,format=raw "
        "-drive if=scsi,bus=0,unit=6,media=cdrom,file=null-co://,format=raw "
        "-drive if=scsi,bus=1,unit=6,media=disk,file=null-co://,format=raw");
    zx6000_assert_block_devices(qts, explicit_topology,
                                G_N_ELEMENTS(explicit_topology));
    qtest_quit(qts);

    qts = zx6000_start_with_storage("-cdrom null-co://");
    zx6000_assert_block_devices(qts, cdrom_shortcut,
                                G_N_ELEMENTS(cdrom_shortcut));
    qtest_quit(qts);
}

static void test_hp_zx6000_mpt_doorbell(void)
{
    QTestState *qts = zx6000_start();
    uint32_t doorbell;
    unsigned int reply_words = 0;

    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE + ZX6000_MPT_DOORBELL_OFFSET,
                 ZX6000_MPT_HANDSHAKE_IOC_FACTS);
    g_assert_cmphex(
        qtest_readl(qts, ZX6000_LSI0_MMIO_BASE +
                         ZX6000_MPT_INTERRUPT_STATUS_OFFSET) &
            ZX6000_MPT_DOORBELL_INTERRUPT,
        ==, ZX6000_MPT_DOORBELL_INTERRUPT);
    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE +
                 ZX6000_MPT_INTERRUPT_STATUS_OFFSET, 0);

    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE + ZX6000_MPT_DOORBELL_OFFSET,
                 ZX6000_MPT_IOC_FACTS_REQUEST_WORD0);
    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE + ZX6000_MPT_DOORBELL_OFFSET, 0);
    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE + ZX6000_MPT_DOORBELL_OFFSET, 0);

    do {
        g_assert_cmphex(
            qtest_readl(qts, ZX6000_LSI0_MMIO_BASE +
                             ZX6000_MPT_INTERRUPT_STATUS_OFFSET) &
                ZX6000_MPT_DOORBELL_INTERRUPT,
            ==, ZX6000_MPT_DOORBELL_INTERRUPT);
        doorbell = qtest_readl(
            qts, ZX6000_LSI0_MMIO_BASE + ZX6000_MPT_DOORBELL_OFFSET);
        if (!(doorbell & ZX6000_MPT_DOORBELL_ACTIVE)) {
            break;
        }
        reply_words++;
        qtest_writel(qts,
                     ZX6000_LSI0_MMIO_BASE +
                     ZX6000_MPT_INTERRUPT_STATUS_OFFSET, 0);
    } while (reply_words < 256);

    g_assert_cmpuint(reply_words, >, 0);
    g_assert_cmpuint(reply_words, <, 256);
    g_assert_cmphex(
        qtest_readl(qts, ZX6000_LSI0_MMIO_BASE +
                         ZX6000_MPT_INTERRUPT_STATUS_OFFSET) &
            ZX6000_MPT_DOORBELL_INTERRUPT,
        ==, ZX6000_MPT_DOORBELL_INTERRUPT);
    qtest_writel(qts,
                 ZX6000_LSI0_MMIO_BASE +
                 ZX6000_MPT_INTERRUPT_STATUS_OFFSET, 0);
    g_assert_cmphex(
        qtest_readl(qts, ZX6000_LSI0_MMIO_BASE +
                         ZX6000_MPT_INTERRUPT_STATUS_OFFSET) &
            ZX6000_MPT_DOORBELL_INTERRUPT,
        ==, 0);
    qtest_quit(qts);
}

static uint8_t zx6000_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

static void zx6000_config_select(QTestState *qts, unsigned int root,
                                 unsigned int devfn, unsigned int reg)
{
    uint64_t selector;

    g_assert_cmpuint(root, <, HP_ZX6000_PCI_ROOT_COUNT);
    selector = (uint64_t)devfn << 8 | (reg & 0xfc);
    qtest_writeq(qts,
                 HP_ZX6000_IOA_ADDRESS(root) +
                 HP_ZX1_IOA_CONFIG_ADDRESS,
                 selector);
}

static uint8_t zx6000_config_readb(QTestState *qts, unsigned int root,
                                   unsigned int devfn, unsigned int reg)
{
    zx6000_config_select(qts, root, devfn, reg);
    return qtest_readb(qts,
                       HP_ZX6000_IOA_ADDRESS(root) +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint16_t zx6000_config_readw(QTestState *qts, unsigned int root,
                                    unsigned int devfn, unsigned int reg)
{
    zx6000_config_select(qts, root, devfn, reg);
    return qtest_readw(qts,
                       HP_ZX6000_IOA_ADDRESS(root) +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint32_t zx6000_config_readl(QTestState *qts, unsigned int root,
                                    unsigned int devfn, unsigned int reg)
{
    zx6000_config_select(qts, root, devfn, reg);
    return qtest_readl(qts,
                       HP_ZX6000_IOA_ADDRESS(root) +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint64_t zx6000_sparse_io_address(uint16_t port)
{
    return ZX6000_SPARSE_IO_BASE +
        ((uint64_t)(port >> 2) << 12) + (port & 3U);
}

static uint8_t zx6000_inb(QTestState *qts, uint16_t port)
{
    return qtest_readb(qts, zx6000_sparse_io_address(port));
}

static void zx6000_outb(QTestState *qts, uint16_t port, uint8_t value)
{
    qtest_writeb(qts, zx6000_sparse_io_address(port), value);
}

static uint16_t zx6000_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, zx6000_sparse_io_address(port));
}

static void zx6000_outw(QTestState *qts, uint16_t port, uint16_t value)
{
    qtest_writew(qts, zx6000_sparse_io_address(port), value);
}

static uint8_t zx6000_vga_indexed_read(QTestState *qts,
                                       uint16_t index_port,
                                       uint16_t data_port, uint8_t index)
{
    zx6000_outb(qts, index_port, index);
    return zx6000_inb(qts, data_port);
}

static void zx6000_assert_int10_rom(QTestState *qts)
{
    uint8_t rom[ZX6000_INT10_ROM_SIZE];
    uint8_t vector[4];
    uint16_t pcir;

    qtest_memread(qts, ZX6000_INT10_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom), ==, 0xaa55);
    g_assert_cmpuint(rom[2] * 512U, ==, sizeof(rom));
    g_assert_cmphex(rom[3], !=, 0xcb);
    pcir = lduw_le_p(rom + 0x18);
    g_assert_cmphex(pcir, ==, ZX6000_INT10_PCIR_OFFSET);
    g_assert_cmpmem(rom + pcir, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + pcir + 4), ==, 0x1002);
    g_assert_cmphex(lduw_le_p(rom + pcir + 6), ==, 0x5159);
    g_assert_cmpuint(lduw_le_p(rom + pcir + 0x10) * 512U, ==,
                     sizeof(rom));
    g_assert_cmphex(zx6000_checksum(rom, sizeof(rom)), ==, 0);
    g_assert_cmphex(rom[ZX6000_INT10_HANDLER_OFFSET], !=, 0xcb);

    qtest_memread(qts, ZX6000_INT10_VECTOR_ADDR, vector, sizeof(vector));
    g_assert_cmphex(lduw_le_p(vector), ==, ZX6000_INT10_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(vector + 2), ==,
                    ZX6000_INT10_ROM_BASE >> 4);
}

static void zx6000_int10_write_request(QTestState *qts,
                                       const ZX6000Int10Registers *regs)
{
    const uint16_t values[] = {
        regs->ax, regs->bx, regs->cx, regs->dx, regs->di, regs->es,
    };
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(values); i++) {
        zx6000_outw(qts, ZX6000_INT10_IO_BASE + i * 2, values[i]);
    }
    if (regs->input_signature != 0) {
        zx6000_outw(qts, ZX6000_INT10_IO_DATA,
                    (uint16_t)regs->input_signature);
        zx6000_outw(qts, ZX6000_INT10_IO_DATA,
                    (uint16_t)(regs->input_signature >> 16));
    }
}

static void zx6000_int10_read_result(QTestState *qts,
                                     ZX6000Int10Registers *regs)
{
    regs->ax = zx6000_inw(qts, ZX6000_INT10_IO_BASE);
    regs->bx = zx6000_inw(qts, ZX6000_INT10_IO_BASE + 2);
    regs->cx = zx6000_inw(qts, ZX6000_INT10_IO_BASE + 4);
    regs->dx = zx6000_inw(qts, ZX6000_INT10_IO_BASE + 6);
    regs->di = zx6000_inw(qts, ZX6000_INT10_IO_BASE + 8);
    regs->es = zx6000_inw(qts, ZX6000_INT10_IO_BASE + 10);
}

static size_t zx6000_int10_call(QTestState *qts,
                                ZX6000Int10Registers *regs,
                                uint8_t *response, size_t response_size)
{
    size_t word_count;
    size_t i;

    zx6000_int10_write_request(qts, regs);
    zx6000_outw(qts, ZX6000_INT10_IO_EXEC, ZX6000_INT10_TRIGGER);
    word_count = zx6000_inw(qts, ZX6000_INT10_IO_EXEC);
    g_assert_cmpuint(word_count * 2, <=, response_size);
    for (i = 0; i < word_count; i++) {
        stw_le_p(response + i * 2,
                 zx6000_inw(qts, ZX6000_INT10_IO_DATA));
    }
    zx6000_int10_read_result(qts, regs);
    return word_count * 2;
}

static void zx6000_int10_set_mode(QTestState *qts, uint16_t ax)
{
    ZX6000Int10Registers regs = { .ax = ax };

    g_assert_cmpuint(zx6000_int10_call(qts, &regs, NULL, 0), ==, 0);
    g_assert_cmphex(regs.ax, ==, ax);
}

static void zx6000_assert_mode12(QTestState *qts, bool no_clear)
{
    zx6000_outw(qts, ZX6000_VBE_INDEX_PORT, ZX6000_VBE_ENABLE_INDEX);
    g_assert_cmphex(zx6000_inw(qts, ZX6000_VBE_DATA_PORT), ==, 0);
    g_assert_cmphex(zx6000_inb(qts, ZX6000_VGA_MISC_READ_PORT), ==, 0xe3);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_SEQ_INDEX_PORT,
                        ZX6000_VGA_SEQ_DATA_PORT, 2), ==, 0x0f);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_SEQ_INDEX_PORT,
                        ZX6000_VGA_SEQ_DATA_PORT, 4), ==, 0x06);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_CRTC_INDEX_PORT,
                        ZX6000_VGA_CRTC_DATA_PORT, 1), ==, 0x4f);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_CRTC_INDEX_PORT,
                        ZX6000_VGA_CRTC_DATA_PORT, 0x12), ==, 0xdf);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_CRTC_INDEX_PORT,
                        ZX6000_VGA_CRTC_DATA_PORT, 0x13), ==, 0x28);
    g_assert_cmphex(zx6000_vga_indexed_read(
                        qts, ZX6000_VGA_GFX_INDEX_PORT,
                        ZX6000_VGA_GFX_DATA_PORT, 6), ==, 0x05);

    g_assert_cmphex(qtest_readb(qts, ZX6000_BDA_VIDEO_MODE), ==, 0x12);
    g_assert_cmphex(qtest_readw(qts, ZX6000_BDA_VIDEO_COLUMNS), ==, 80);
    g_assert_cmphex(qtest_readw(qts, ZX6000_BDA_VIDEO_PAGE_SIZE), ==,
                    0xa000);
    g_assert_cmphex(qtest_readw(qts, ZX6000_BDA_VIDEO_PAGE_START), ==, 0);
    g_assert_cmphex(qtest_readb(qts, ZX6000_BDA_VIDEO_ROWS), ==, 29);
    g_assert_cmphex(qtest_readw(qts, ZX6000_BDA_CHARACTER_HEIGHT), ==, 16);
    g_assert_cmphex(qtest_readb(qts, ZX6000_BDA_VIDEO_CONTROL), ==,
                    no_clear ? 0xe0 : 0x60);
}

static uint32_t zx6000_int10_far_to_linear(uint32_t pointer)
{
    return (pointer >> 16) * 16 + (pointer & 0xffff);
}

static bool zx6000_int10_mode_list_contains(QTestState *qts,
                                             uint32_t address,
                                             uint16_t expected)
{
    size_t i;

    for (i = 0; i < ZX6000_INT10_ROM_SIZE / 2; i++, address += 2) {
        uint16_t mode = qtest_readw(qts, address);

        if (mode == expected) {
            return true;
        }
        if (mode == 0xffff) {
            return false;
        }
    }
    g_assert_not_reached();
}

static void zx6000_assert_int10_vbe(QTestState *qts)
{
    uint8_t response[512];
    ZX6000Int10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = ZX6000_VBE2_SIGNATURE,
    };
    uint32_t memory_size;
    uint32_t max_width;
    uint32_t modes;
    size_t length;

    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmpmem(response, 4, "VESA", 4);
    memory_size = (uint32_t)lduw_le_p(response + 18) * (64 * KiB);
    modes = zx6000_int10_far_to_linear(ldl_le_p(response + 14));
    g_assert_cmphex(modes, ==, ZX6000_INT10_ROM_BASE +
                    ZX6000_INT10_MODE_LIST_OFFSET);
    g_assert_true(zx6000_int10_mode_list_contains(qts, modes, 0x111));

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f01,
        .cx = 0x0111,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[28], ==, 0);
    g_assert_cmphex((uint32_t)ldl_le_p(response + 40), ==,
                    ZX6000_RV100_FB_BASE);

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f02,
        .bx = 0xc143,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);

    regs = (ZX6000Int10Registers) { .ax = 0x4f03 };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 0xc143);

    regs = (ZX6000Int10Registers) { .ax = 0x4f05 };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x034f);

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f06,
        .cx = 801,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);
    g_assert_cmphex(regs.dx, ==, memory_size / 3232);

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f06,
        .bx = 2,
        .cx = 3201,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f06,
        .cx = VBE_DISPI_MAX_XRES + 1,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x024f);

    regs = (ZX6000Int10Registers) {
        .ax = 0x4f06,
        .bx = 1,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / 600 / 4) & ~7U;
    regs = (ZX6000Int10Registers) {
        .ax = 0x4f06,
        .bx = 3,
    };
    length = zx6000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, max_width * 4);
    g_assert_cmphex(regs.cx, ==, max_width);
    g_assert_cmphex(regs.dx, ==, memory_size / (max_width * 4));
}

static void zx6000_config_writew(QTestState *qts, unsigned int root,
                                 unsigned int devfn, unsigned int reg,
                                 uint16_t value)
{
    zx6000_config_select(qts, root, devfn, reg);
    qtest_writew(qts,
                 HP_ZX6000_IOA_ADDRESS(root) +
                 HP_ZX1_IOA_CONFIG_DATA + (reg & 3),
                 value);
}

static void zx6000_config_writel(QTestState *qts, unsigned int root,
                                 unsigned int devfn, unsigned int reg,
                                 uint32_t value)
{
    zx6000_config_select(qts, root, devfn, reg);
    qtest_writel(qts,
                 HP_ZX6000_IOA_ADDRESS(root) +
                 HP_ZX1_IOA_CONFIG_DATA + (reg & 3),
                 value);
}

static void zx6000_assert_device(QTestState *qts, unsigned int root,
                                 unsigned int devfn, uint16_t vendor,
                                 uint16_t device, uint16_t command)
{
    g_assert_cmphex(zx6000_config_readl(qts, root, devfn, 0), ==,
                    (uint32_t)device << 16 | vendor);
    g_assert_cmphex(zx6000_config_readw(qts, root, devfn, PCI_COMMAND), ==,
                    command);
}

static void zx6000_assert_rv100(QTestState *qts)
{
    unsigned int devfn = PCI_DEVFN(0, 0);

    zx6000_assert_device(qts, 4, devfn, 0x1002, 0x5159,
                         PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                         PCI_COMMAND_MASTER);
    g_assert_cmphex(zx6000_config_readw(
                        qts, 4, devfn, PCI_CLASS_DEVICE), ==, 0x0300);
    g_assert_cmphex(zx6000_config_readl(
                        qts, 4, devfn, PCI_BASE_ADDRESS_0), ==, 0xa0000008);
    g_assert_cmphex(zx6000_config_readl(
                        qts, 4, devfn, PCI_BASE_ADDRESS_1), ==, 0x8001);
    g_assert_cmphex(zx6000_config_readl(
                        qts, 4, devfn, PCI_BASE_ADDRESS_2), ==, 0xa8000000);
    g_assert_cmphex(zx6000_config_readl(
                        qts, 4, devfn, PCI_ROM_ADDRESS), ==, 0);
    g_assert_cmphex(zx6000_config_readb(
                        qts, 4, devfn, PCI_INTERRUPT_LINE), ==, 64);
    g_assert_cmphex(zx6000_config_readb(
                        qts, 4, devfn, PCI_INTERRUPT_PIN), ==, 1);
}

static void test_hp_zx6000_descriptor(void)
{
    uint8_t storage[2048] = { 0 };
    IA64PlatformDescriptor *descriptor =
        (IA64PlatformDescriptor *)storage;
    const IA64PlatformRamRange *ram;
    const IA64PlatformPciRoot *roots;
    QTestState *qts = zx6000_start();
    uint32_t total_size;
    unsigned int root;

    qtest_memread(qts, ZX6000_DESCRIPTOR_GPA, storage,
                  sizeof(*descriptor));
    total_size = le32_to_cpu(descriptor->TotalSize);
    g_assert_cmpuint(total_size, <=, sizeof(storage));
    qtest_memread(qts, ZX6000_DESCRIPTOR_GPA, storage, total_size);

    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_ZX6000);
    g_assert_cmpuint(descriptor->ProcessorCount, ==, cpu_to_le32(2));
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     HP_ZX6000_PCI_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==,
                     HP_ZX6000_PCI_ROOT_COUNT);
    g_assert_cmphex(le64_to_cpu(descriptor->AcpiPmBase), ==,
                    ZX6000_ACPI_PM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->AcpiPmSize), ==,
                    IA64_PLATFORM_ACPI_PM_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->AcpiSciGsi), ==,
                     ZX6000_ACPI_SCI_GSI);
    g_assert_cmpuint(zx6000_checksum(storage, total_size), ==, 0);

    ram = (const IA64PlatformRamRange *)(
        storage + le32_to_cpu(descriptor->RamRangeOffset));
    g_assert_cmphex(le64_to_cpu(ram[0].Base), ==, 0);
    g_assert_cmphex(le64_to_cpu(ram[0].Size), ==, ZX6000_LOW_RAM_SIZE);
    g_assert_cmphex(le64_to_cpu(ram[1].Base), ==, ZX6000_HIGH_RAM_BASE);
    g_assert_cmphex(le64_to_cpu(ram[1].Size), ==, ZX6000_LOW_RAM_SIZE);

    roots = (const IA64PlatformPciRoot *)(
        storage + le32_to_cpu(descriptor->PciRootOffset));
    for (root = 0; root < HP_ZX6000_PCI_ROOT_COUNT; root++) {
        g_assert_cmpuint(roots[root].Bus, ==, zx6000_first_bus[root]);
        g_assert_cmpuint(roots[root].BusEnd, ==, zx6000_last_bus[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].ConfigBase), ==,
                        HP_ZX6000_IOA_ADDRESS(root));
        g_assert_cmphex(le64_to_cpu(roots[root].IoBase), ==,
                        (uint64_t)root * 0x2000U);
        g_assert_cmphex(le64_to_cpu(roots[root].IoSize), ==, 0x2000U);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio32Base), ==,
                        zx6000_cpu_mmio_base[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio32Size), ==,
                        zx6000_mmio_size[root]);
        g_assert_cmphex(
            le64_to_cpu(roots[root].Mmio32TranslationOffset), ==,
            0);
        g_assert_cmphex(le32_to_cpu(roots[root].Flags), ==,
                        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
                        IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO);
        g_assert_cmphex(
            le64_to_cpu(roots[root].IoTranslationOffset), ==,
            ZX6000_SPARSE_IO_BASE);
        g_assert_cmphex(le64_to_cpu(roots[root].DmaBase), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[root].DmaSize), ==,
                        ZX6000_LOW_RAM_SIZE);
        g_assert_cmphex(qtest_readq(
                            qts, HP_ZX6000_IOA_ADDRESS(root) +
                            HP_ZX1_IOA_FUNCTION_ID), ==,
                        UINT64_C(0x02b00000122e103c));
    }

    qtest_quit(qts);
}

static void test_hp_zx6000_acpi_pm(void)
{
    const uint64_t event_enable = ZX6000_ACPI_PM_BASE +
        IA64_PLATFORM_ACPI_PM1_EVT_OFFSET + 2U;
    const uint64_t control = ZX6000_ACPI_PM_BASE +
        IA64_PLATFORM_ACPI_PM1_CNT_OFFSET;
    const uint64_t timer = ZX6000_ACPI_PM_BASE +
        IA64_PLATFORM_ACPI_PM_TMR_OFFSET;
    const uint64_t gpe_status = ZX6000_ACPI_PM_BASE +
        IA64_PLATFORM_ACPI_GPE0_STS_OFFSET;
    const uint64_t gpe_enable = ZX6000_ACPI_PM_BASE +
        IA64_PLATFORM_ACPI_GPE0_EN_OFFSET;
    const uint64_t io_event_enable = zx6000_sparse_io_address(
        IA64_PLATFORM_ACPI_PM1_EVT_OFFSET + 2U);
    const uint64_t io_control = zx6000_sparse_io_address(
        IA64_PLATFORM_ACPI_PM1_CNT_OFFSET);
    const uint64_t io_timer = zx6000_sparse_io_address(
        IA64_PLATFORM_ACPI_PM_TMR_OFFSET);
    const uint64_t io_gpe_status = zx6000_sparse_io_address(
        IA64_PLATFORM_ACPI_GPE0_STS_OFFSET);
    const uint64_t io_gpe_enable = zx6000_sparse_io_address(
        IA64_PLATFORM_ACPI_GPE0_EN_OFFSET);
    QTestState *qts = zx6000_start();
    uint32_t first_timer;

    g_assert_cmphex(qtest_readw(qts, control) & ACPI_BITMASK_SCI_ENABLE,
                    ==, ACPI_BITMASK_SCI_ENABLE);
    g_assert_cmphex(qtest_readw(qts, io_control), ==,
                    qtest_readw(qts, control));
    qtest_writew(qts, io_event_enable, ACPI_BITMASK_POWER_BUTTON_ENABLE);
    g_assert_cmphex(qtest_readw(qts, event_enable), ==,
                    ACPI_BITMASK_POWER_BUTTON_ENABLE);
    g_assert_cmphex(qtest_readw(qts, io_event_enable), ==,
                    ACPI_BITMASK_POWER_BUTTON_ENABLE);

    first_timer = qtest_readl(qts, io_timer);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND / 1000U);
    g_assert_cmphex(qtest_readl(qts, io_timer), !=, first_timer);
    g_assert_cmphex(qtest_readl(qts, timer), ==,
                    qtest_readl(qts, io_timer));

    qtest_writew(qts, io_control, 0);
    g_assert_cmphex(qtest_readw(qts, control), ==, 0);
    qtest_writel(qts, io_gpe_enable, UINT32_C(0xa55aa55a));
    g_assert_cmphex(qtest_readl(qts, gpe_enable), ==,
                    UINT32_C(0xa55aa55a));
    g_assert_cmphex(qtest_readl(qts, gpe_status), ==, 0);
    g_assert_cmphex(qtest_readl(qts, io_gpe_status), ==, 0);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readw(qts, control) & ACPI_BITMASK_SCI_ENABLE,
                    ==, ACPI_BITMASK_SCI_ENABLE);
    g_assert_cmphex(qtest_readw(qts, event_enable), ==, 0);
    g_assert_cmphex(qtest_readl(qts, gpe_enable), ==, 0);
    g_assert_cmphex(qtest_readw(qts, io_event_enable), ==, 0);
    g_assert_cmphex(qtest_readl(qts, io_gpe_enable), ==, 0);
    qtest_quit(qts);
}

static void test_hp_zx6000_pci_layout(void)
{
    QTestState *qts = zx6000_start();

    zx6000_assert_rv100(qts);

    zx6000_assert_device(qts, 0, PCI_DEVFN(2, 0), 0x1095, 0x0649,
                         PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_BASE_ADDRESS_0), ==, 0xd59);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_BASE_ADDRESS_1), ==, 0xd65);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_BASE_ADDRESS_2), ==, 0xd51);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_BASE_ADDRESS_3), ==, 0xd61);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_BASE_ADDRESS_4), ==, 0xd41);
    g_assert_cmphex(zx6000_config_readb(qts, 0, PCI_DEVFN(2, 0),
                                        ZX6000_CMD649_CNTRL), ==, 0xec);
    g_assert_cmphex(zx6000_config_readb(qts, 0, PCI_DEVFN(2, 0),
                                        ZX6000_CMD649_BMIDECSR), ==, 0x01);

    zx6000_assert_device(qts, 0, PCI_DEVFN(3, 0), 0x8086, 0x1209,
                         PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                         PCI_COMMAND_MASTER);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(3, 0),
                                        PCI_BASE_ADDRESS_0), ==, 0x80020008);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(3, 0),
                                        PCI_BASE_ADDRESS_1), ==, 0xd01);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(3, 0),
                                        PCI_BASE_ADDRESS_2), ==, 0x80040000);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(3, 0),
                                        PCI_ROM_ADDRESS), ==, 0);

    zx6000_assert_device(qts, 0, PCI_DEVFN(1, 0), 0x1033, 0x0035,
                         PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    zx6000_assert_device(qts, 0, PCI_DEVFN(1, 1), 0x1033, 0x0035,
                         PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    zx6000_assert_device(qts, 0, PCI_DEVFN(1, 2), 0x1033, 0x00e0,
                         PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(1, 0),
                                        PCI_BASE_ADDRESS_0), ==, 0x80023000);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(1, 1),
                                        PCI_BASE_ADDRESS_0), ==, 0x80022000);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(1, 2),
                                        PCI_BASE_ADDRESS_0), ==, 0x80021000);

    zx6000_assert_device(qts, 1, PCI_DEVFN(1, 0), 0x1000, 0x0030,
                         PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                         PCI_COMMAND_MASTER);
    zx6000_assert_device(qts, 1, PCI_DEVFN(1, 1), 0x1000, 0x0030,
                         PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                         PCI_COMMAND_MASTER);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 0),
                                        PCI_BASE_ADDRESS_0), ==, 0x2001);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 0),
                                        PCI_BASE_ADDRESS_1), ==, 0x88000000);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 0),
                                        PCI_BASE_ADDRESS_2), ==, 0x88010000);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 1),
                                        PCI_BASE_ADDRESS_0), ==, 0x2101);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 1),
                                        PCI_BASE_ADDRESS_1), ==, 0x88004000);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 1),
                                        PCI_BASE_ADDRESS_2), ==, 0x88020000);

    zx6000_config_writel(qts, 4, PCI_DEVFN(0, 0), PCI_BASE_ADDRESS_0, 0);
    zx6000_config_writel(qts, 4, PCI_DEVFN(0, 0), PCI_BASE_ADDRESS_1, 0);
    zx6000_config_writel(qts, 4, PCI_DEVFN(0, 0), PCI_BASE_ADDRESS_2, 0);
    zx6000_config_writew(qts, 4, PCI_DEVFN(0, 0), PCI_COMMAND, 0);
    qtest_system_reset(qts);
    zx6000_assert_rv100(qts);
    g_assert_cmphex(zx6000_config_readl(qts, 1, PCI_DEVFN(1, 1),
                                        PCI_BASE_ADDRESS_1), ==, 0x88004000);
    qtest_quit(qts);
}

static void test_hp_zx6000_vga_legacy_io(void)
{
    QTestState *qts = zx6000_start();
    uint64_t vbe_index = zx6000_sparse_io_address(0x1ce);
    uint64_t vbe_data = zx6000_sparse_io_address(0x1d0);
    uint64_t sequencer_index = zx6000_sparse_io_address(0x3c4);
    uint64_t sequencer_data = zx6000_sparse_io_address(0x3c5);

    qtest_writew(qts, vbe_index, 1);
    qtest_writew(qts, vbe_data, 800);
    g_assert_cmphex(qtest_readw(qts, vbe_data), ==, 800);

    qtest_writeb(qts, sequencer_index, 2);
    qtest_writeb(qts, sequencer_data, 0x0f);
    g_assert_cmphex(qtest_readb(qts, sequencer_index), ==, 2);
    g_assert_cmphex(qtest_readb(qts, sequencer_data), ==, 0x0f);
    qtest_quit(qts);
}

static void test_hp_zx6000_int10(void)
{
    uint8_t first_marker[16];
    uint8_t last_marker[16];
    uint8_t actual[16];
    uint8_t zero[16] = { 0 };
    uint64_t last = ZX6000_RV100_FB_BASE +
        ZX6000_VGA_PLANAR_SIZE - sizeof(last_marker);
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts = zx6000_start_with_options(
        "", "-nodefaults -vga ati");

    zx6000_assert_int10_rom(qts);
    zx6000_assert_int10_vbe(qts);

    memset(first_marker, 0xa5, sizeof(first_marker));
    memset(last_marker, 0x5a, sizeof(last_marker));
    qtest_memwrite(qts, ZX6000_RV100_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    zx6000_int10_set_mode(qts, 0x0012);
    zx6000_assert_mode12(qts, false);
    qtest_memread(qts, ZX6000_RV100_FB_BASE, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));

    qtest_writeb(qts, ZX6000_VGA_LEGACY_BASE, 0xff);
    tmpdir = g_dir_make_tmp("hp-zx6000-mode12-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "mode12.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    zx6000_assert_ppm_pixel(ppm, 640, 480, 0, 0,
                            0xff, 0xff, 0xff);
    zx6000_assert_ppm_pixel(ppm, 640, 480, 8, 0, 0, 0, 0);

    qtest_memwrite(qts, ZX6000_RV100_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    zx6000_int10_set_mode(qts, 0x0092);
    zx6000_assert_mode12(qts, true);
    qtest_memread(qts, ZX6000_RV100_FB_BASE, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    first_marker, sizeof(first_marker));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    last_marker, sizeof(last_marker));

    qtest_writeb(qts, ZX6000_INT10_ROM_BASE, 0);
    qtest_writel(qts, ZX6000_INT10_VECTOR_ADDR, 0);
    zx6000_outw(qts, ZX6000_INT10_IO_BASE, 0xffff);
    qtest_system_reset(qts);
    zx6000_assert_int10_rom(qts);
    g_assert_cmphex(zx6000_inw(qts, ZX6000_INT10_IO_BASE), ==, 0);

    qtest_memwrite(qts, ZX6000_RV100_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    zx6000_int10_set_mode(qts, 0x0012);
    zx6000_assert_mode12(qts, false);
    qtest_memread(qts, ZX6000_RV100_FB_BASE, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_hp_zx6000_default_usb_input(void)
{
    QTestState *qts = zx6000_start_with_options("", "");

    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-kbd"), ==, 1);
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-tablet"), ==,
                     1);
    {
        g_autofree char *keyboard =
            zx6000_find_unattached_child(qts, "usb-kbd");
        g_autofree char *tablet =
            zx6000_find_unattached_child(qts, "usb-tablet");

        g_assert_nonnull(keyboard);
        g_assert_nonnull(tablet);
        g_assert_false(qtest_qom_get_bool(qts, keyboard, "msos-desc"));
        g_assert_false(qtest_qom_get_bool(qts, tablet, "msos-desc"));
    }
    qtest_quit(qts);

    qts = zx6000_start_with_options("", "-nodefaults");
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-kbd"), ==, 0);
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-tablet"), ==,
                     0);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(1, 0), 0), ==,
                    0xffffffffU);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(2, 0), 0), ==,
                    0x06491095U);
    qtest_quit(qts);

    qts = zx6000_start_with_options(",usb=off", "");
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-kbd"), ==, 0);
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-tablet"), ==,
                     0);
    g_assert_cmphex(zx6000_config_readl(qts, 0, PCI_DEVFN(1, 0), 0), ==,
                    0xffffffffU);
    g_assert_cmphex(zx6000_config_readl(qts, 4, PCI_DEVFN(0, 0), 0), ==,
                    0x51591002U);
    qtest_quit(qts);
}

static void test_hp_zx6000_graphics_options(void)
{
    QTestState *qts = zx6000_start_with_options("", "-vga none");

    g_assert_cmphex(zx6000_config_readl(qts, 4, PCI_DEVFN(0, 0), 0), ==,
                    0xffffffffU);
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-kbd"), ==, 1);
    qtest_quit(qts);

    qts = zx6000_start_with_options("", "-nodefaults -vga ati");
    g_assert_cmphex(zx6000_config_readl(qts, 4, PCI_DEVFN(0, 0), 0), ==,
                    0x51591002U);
    g_assert_cmpuint(zx6000_count_unattached_children(qts, "usb-kbd"), ==, 0);
    qtest_quit(qts);
}

static void test_hp_zx6000_ram_and_nvram(void)
{
    QTestState *qts = zx6000_start();

    qtest_writel(qts, UINT64_C(0x01000000), 0x12345678);
    qtest_writel(qts, ZX6000_HIGH_RAM_BASE, 0x89abcdef);
    g_assert_cmphex(qtest_readl(qts, UINT64_C(0x01000000)), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, ZX6000_HIGH_RAM_BASE), ==, 0x89abcdef);

    qtest_writeb(qts, ZX6000_NVRAM_BASE + 0x80, 0x5a);
    g_assert_cmphex(qtest_readb(qts, ZX6000_NVRAM_BASE + 0x80), ==, 0x5a);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readb(qts, ZX6000_NVRAM_BASE + 0x80), ==, 0x5a);

    qtest_quit(qts);
}

static void test_hp_zx6000_nvram_backing_file(void)
{
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *machine_options = NULL;
    g_autofree char *initial = g_malloc(ZX6000_NVRAM_PREFIX_SIZE);
    g_autofree char *contents = NULL;
    g_autofree char *zero_tail = NULL;
    g_autofree char *oversized = NULL;
    g_autoptr(GError) error = NULL;
    gsize length = 0;
    QTestState *qts;

    tmpdir = g_dir_make_tmp("hp-zx6000-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    machine_options = g_strdup_printf(",nvram=%s", quoted_path);

    qts = zx6000_start_with_options(machine_options, "-nodefaults");
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    qtest_writeb(qts, ZX6000_NVRAM_BASE, 0x3c);
    qtest_writeq(qts,
                 ZX6000_NVRAM_BASE +
                     HP_ZX6000_PDH_NVRAM_COMMIT_OFFSET,
                 HP_ZX6000_PDH_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, HP_ZX6000_PDH_NVRAM_SIZE);
    g_assert_cmphex((uint8_t)contents[0], ==, 0x3c);
    zero_tail = g_malloc0(HP_ZX6000_PDH_NVRAM_SIZE - 1U);
    g_assert_cmpmem(contents + 1, HP_ZX6000_PDH_NVRAM_SIZE - 1U,
                    zero_tail, HP_ZX6000_PDH_NVRAM_SIZE - 1U);
    g_clear_pointer(&contents, g_free);

    /* An empty file remains empty until the guest commits NVRAM. */
    g_assert_true(g_file_set_contents(path, "", 0, &error));
    g_assert_no_error(error);
    qts = zx6000_start_with_options(machine_options, "-nodefaults");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 0);
    g_clear_pointer(&contents, g_free);
    qtest_writeb(qts, ZX6000_NVRAM_BASE, 0x96);
    qtest_writeq(qts,
                 ZX6000_NVRAM_BASE +
                     HP_ZX6000_PDH_NVRAM_COMMIT_OFFSET,
                 HP_ZX6000_PDH_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, HP_ZX6000_PDH_NVRAM_SIZE);
    g_assert_cmphex((uint8_t)contents[0], ==, 0x96);
    g_clear_pointer(&contents, g_free);

    /* A legacy 64 KiB file must remain 64 KiB across load and commit. */
    memset(initial, 0x5a, ZX6000_NVRAM_PREFIX_SIZE);
    g_assert_true(g_file_set_contents(path, initial,
                                      ZX6000_NVRAM_PREFIX_SIZE, &error));
    g_assert_no_error(error);
    qts = zx6000_start_with_options(machine_options, "-nodefaults");
    g_assert_cmphex(qtest_readb(qts, ZX6000_NVRAM_BASE), ==, 0x5a);
    g_assert_cmphex(qtest_readb(qts,
                               ZX6000_NVRAM_BASE +
                               ZX6000_NVRAM_PREFIX_SIZE), ==, 0);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, ZX6000_NVRAM_PREFIX_SIZE);
    g_assert_cmpmem(contents, ZX6000_NVRAM_PREFIX_SIZE,
                    initial, ZX6000_NVRAM_PREFIX_SIZE);
    g_clear_pointer(&contents, g_free);

    qtest_writeb(qts, ZX6000_NVRAM_BASE + 0x80, 0xa5);
    qtest_writeb(qts,
                 ZX6000_NVRAM_BASE + ZX6000_NVRAM_PREFIX_SIZE + 0x80,
                 0xc3);
    qtest_writeq(qts,
                 ZX6000_NVRAM_BASE +
                     HP_ZX6000_PDH_NVRAM_COMMIT_OFFSET,
                 HP_ZX6000_PDH_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    initial[0x80] = 0xa5;
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, ZX6000_NVRAM_PREFIX_SIZE);
    g_assert_cmpmem(contents, ZX6000_NVRAM_PREFIX_SIZE,
                    initial, ZX6000_NVRAM_PREFIX_SIZE);
    g_clear_pointer(&contents, g_free);

    qts = zx6000_start_with_options(machine_options, "-nodefaults");
    g_assert_cmphex(qtest_readb(qts, ZX6000_NVRAM_BASE + 0x80), ==, 0xa5);
    g_assert_cmphex(qtest_readb(
                        qts, ZX6000_NVRAM_BASE +
                             ZX6000_NVRAM_PREFIX_SIZE + 0x80), ==, 0);
    qtest_quit(qts);

    oversized = g_malloc0(ZX6000_NVRAM_PREFIX_SIZE + 1U);
    memset(oversized, 0x7d, ZX6000_NVRAM_PREFIX_SIZE + 1U);
    g_assert_true(g_file_set_contents(path, oversized,
                                      ZX6000_NVRAM_PREFIX_SIZE + 1U,
                                      &error));
    g_assert_no_error(error);
    zx6000_assert_nvram_start_fails(
        path, "must be 65536 or 524288 bytes");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, ZX6000_NVRAM_PREFIX_SIZE + 1U);
    g_assert_cmpmem(contents, length, oversized,
                    ZX6000_NVRAM_PREFIX_SIZE + 1U);
    g_clear_pointer(&contents, g_free);
    g_clear_pointer(&oversized, g_free);

    oversized = g_malloc0(HP_ZX6000_PDH_NVRAM_SIZE + 1);
    g_assert_true(g_file_set_contents(path, oversized,
                                      HP_ZX6000_PDH_NVRAM_SIZE + 1,
                                      &error));
    g_assert_no_error(error);
    zx6000_assert_nvram_start_fails(path, "maximum is 524288 bytes");

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hp-zx6000/machine-identity",
                   test_hp_zx6000_machine_identity);
    qtest_add_func("/hp-zx6000/constraints", test_hp_zx6000_constraints);
    qtest_add_func("/hp-zx6000/storage-defaults",
                   test_hp_zx6000_storage_defaults);
    qtest_add_func("/hp-zx6000/mpt-doorbell",
                   test_hp_zx6000_mpt_doorbell);
    qtest_add_func("/hp-zx6000/descriptor", test_hp_zx6000_descriptor);
    qtest_add_func("/hp-zx6000/acpi-pm", test_hp_zx6000_acpi_pm);
    qtest_add_func("/hp-zx6000/pci-layout", test_hp_zx6000_pci_layout);
    qtest_add_func("/hp-zx6000/vga-legacy-io",
                   test_hp_zx6000_vga_legacy_io);
    qtest_add_func("/hp-zx6000/int10", test_hp_zx6000_int10);
    qtest_add_func("/hp-zx6000/default-usb-input",
                   test_hp_zx6000_default_usb_input);
    qtest_add_func("/hp-zx6000/graphics-options",
                   test_hp_zx6000_graphics_options);
    qtest_add_func("/hp-zx6000/ram-and-nvram",
                   test_hp_zx6000_ram_and_nvram);
    qtest_add_func("/hp-zx6000/nvram/backing-file",
                   test_hp_zx6000_nvram_backing_file);
    return g_test_run();
}
