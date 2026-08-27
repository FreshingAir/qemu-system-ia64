/*
 * HP i2000 machine qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/memattrs.h"
#include "hw/display/bochs-vbe.h"
#include "hw/ia64/hp_i2000.h"
#include "hw/ia64/ia64_i2000_profile_abi.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/pci/pci.h"
#include "hw/scsi/isp12160_abi.h"
#include "hw/southbridge/intel_82468gx.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define TEST_FIRMWARE_ENV "QTEST_IA64_FIRMWARE"
#define HP_I2000_LOW_DESCRIPTOR_SIZE  840U
#define HP_I2000_HIGH_DESCRIPTOR_SIZE 856U
#define HP_I2000_RAGE128_ROM_BASE     UINT64_C(0x000c0000)
#define HP_I2000_RAGE128_ROM_SIZE     0x0800U
#define HP_I2000_RAGE128_PCIR_OFFSET  0x0020U
#define HP_I2000_INT10_HANDLER_OFFSET 0x0100U
#define HP_I2000_INT10_MODE_LIST_OFFSET 0x01d0U
#define HP_I2000_INT10_VECTOR_ADDR    UINT64_C(0x00000040)
#define HP_I2000_INT10_IO_BASE        0x01e0U
#define HP_I2000_INT10_IO_EXEC        (HP_I2000_INT10_IO_BASE + 0x0cU)
#define HP_I2000_INT10_IO_DATA        (HP_I2000_INT10_IO_BASE + 0x0eU)
#define HP_I2000_INT10_TRIGGER        0x4941U
#define HP_I2000_VBE2_SIGNATURE       UINT32_C(0x32454256)
#define HP_I2000_RAGE128_FB_BASE      UINT64_C(0x90000000)
#define HP_I2000_VGA_PLANAR_SIZE      (256 * KiB)
#define HP_I2000_VGA_LEGACY_BASE      UINT64_C(0x000a0000)
#define HP_I2000_BDA_VIDEO_MODE       UINT64_C(0x00000449)
#define HP_I2000_BDA_VIDEO_COLUMNS    UINT64_C(0x0000044a)
#define HP_I2000_BDA_VIDEO_PAGE_SIZE  UINT64_C(0x0000044c)
#define HP_I2000_BDA_VIDEO_PAGE_START UINT64_C(0x0000044e)
#define HP_I2000_BDA_VIDEO_ROWS       UINT64_C(0x00000484)
#define HP_I2000_BDA_CHARACTER_HEIGHT UINT64_C(0x00000485)
#define HP_I2000_BDA_VIDEO_CONTROL    UINT64_C(0x00000487)
#define HP_I2000_VBE_INDEX_PORT       0x01ceU
#define HP_I2000_VBE_DATA_PORT        0x01d0U
#define HP_I2000_VBE_ENABLE_INDEX     0x0004U
#define HP_I2000_VGA_MISC_READ_PORT   0x03ccU
#define HP_I2000_VGA_SEQ_INDEX_PORT   0x03c4U
#define HP_I2000_VGA_SEQ_DATA_PORT    0x03c5U
#define HP_I2000_VGA_CRTC_INDEX_PORT  0x03d4U
#define HP_I2000_VGA_CRTC_DATA_PORT   0x03d5U
#define HP_I2000_VGA_GFX_INDEX_PORT   0x03ceU
#define HP_I2000_VGA_GFX_DATA_PORT    0x03cfU
#define HP_I2000_PIC_MASTER_COMMAND   0x20U
#define HP_I2000_PIC_MASTER_DATA      0x21U
#define HP_I2000_PIC_SLAVE_COMMAND    0xa0U
#define HP_I2000_PIC_SLAVE_DATA       0xa1U
#define HP_I2000_I8042_SELF_TEST      0xaaU
#define HP_I2000_I8042_SELF_TEST_OK   0x55U
#define HP_I2000_I8042_WRITE_AUX_OBUF 0xd3U
#define HP_I2000_I8042_STATUS_OBF      BIT(0)
#define HP_I2000_I8042_STATUS_AUX_OBF  BIT(5)
#define HP_I2000_PID_BASE              UINT64_C(0xfec00000)
#define HP_I2000_PID_IOREGSEL          0x00U
#define HP_I2000_PID_IOWIN             0x10U
#define HP_I2000_PID_RTE_BASE          0x10U
#define HP_I2000_DMA_TEST_SLOT         4U
#define HP_I2000_DMA_TEST_LEN          4U
#define HP_I2000_DMA_TEST_SENTINEL     UINT32_C(0xa5a5a5a5)

typedef struct HPI2000Int10Registers {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t di;
    uint16_t es;
    uint32_t input_signature;
} HPI2000Int10Registers;

static void hp_i2000_assert_ppm_pixel(const char *filename, unsigned width,
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

static QTestState *hp_i2000_start(const char *memory)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none "
                       "-m %s -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s",
                       memory, firmware);
}

static QTestState *hp_i2000_start_with_dma_testdev(void)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none "
                       "-m 4G -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s "
                       "-device %s,bus=pci,addr=%u",
                       firmware, TYPE_IOMMU_TESTDEV,
                       HP_I2000_DMA_TEST_SLOT);
}

static QTestState *hp_i2000_start_with_machine_options(
    const char *machine_options, const char *options)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *nvram_options = strstr(machine_options, "nvram=") ?
        "" : ",nvram=none";

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000%s%s "
                       "-m 2G -smp 1 -S -nodefaults "
                       "-display none -serial none -net none -bios %s %s",
                       nvram_options, machine_options, firmware, options);
}

static QTestState *hp_i2000_start_with_options(const char *options)
{
    return hp_i2000_start_with_machine_options("", options);
}

static QTestState *hp_i2000_start_with_storage(const char *storage)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       firmware, storage);
}

static QTestState *hp_i2000_start_defaults(const char *options)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);

    g_assert_nonnull(firmware);
    return qtest_initf("-machine hp-i2000,nvram=none -m 2G -smp 1 -S "
                       "-display none -serial none -net none -bios %s %s",
                       firmware, options);
}

static void hp_i2000_assert_block_devices(QTestState *qts,
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

static void hp_i2000_assert_start_fails_with_machine(
    const char *machine, const char *option, const char *value,
    const char *message)
{
    const char *firmware = g_getenv(TEST_FIRMWARE_ENV);
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", machine,
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

static void hp_i2000_assert_start_fails(const char *option,
                                         const char *value,
                                         const char *message)
{
    hp_i2000_assert_start_fails_with_machine(
        "hp-i2000,nvram=none", option, value, message);
}

static uint8_t hp_i2000_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

static void hp_i2000_config_select(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg)
{
    uint32_t address = UINT32_C(0x80000000) | (uint32_t)bus << 16 |
        devfn << 8 | (reg & 0xfc);

    qtest_writel(qts, HP_I2000_CF8_PA, address);
}

static uint8_t hp_i2000_config_readb(QTestState *qts, uint8_t bus,
                                      unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readb(qts, HP_I2000_CFC_PA + (reg & 3));
}

static uint16_t hp_i2000_config_readw(QTestState *qts, uint8_t bus,
                                       unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readw(qts, HP_I2000_CFC_PA + (reg & 3));
}

static uint32_t hp_i2000_config_readl(QTestState *qts, uint8_t bus,
                                       unsigned int devfn, unsigned int reg)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    return qtest_readl(qts, HP_I2000_CFC_PA + (reg & 3));
}

static void hp_i2000_config_writeb(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint8_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writeb(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static void hp_i2000_config_writew(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint16_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writew(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static void hp_i2000_config_writel(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, unsigned int reg,
                                    uint32_t value)
{
    hp_i2000_config_select(qts, bus, devfn, reg);
    qtest_writel(qts, HP_I2000_CFC_PA + (reg & 3), value);
}

static uint32_t hp_i2000_dma_testdev_trigger(QTestState *qts,
                                             uint64_t mmio_base,
                                             uint64_t dma_address,
                                             uint64_t physical_address)
{
    uint32_t attrs = ITD_ATTRS_SET_SPACE(
        0, ITD_ATTRS_SPACE_NONSECURE);

    qtest_writel(qts, mmio_base + ITD_REG_DMA_GVA_LO,
                 (uint32_t)dma_address);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GVA_HI,
                 (uint32_t)(dma_address >> 32));
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GPA_LO,
                 (uint32_t)physical_address);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_GPA_HI,
                 (uint32_t)(physical_address >> 32));
    qtest_writel(qts, mmio_base + ITD_REG_DMA_LEN,
                 HP_I2000_DMA_TEST_LEN);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_ATTRS, attrs);
    qtest_writel(qts, mmio_base + ITD_REG_DMA_DBELL,
                 ITD_DMA_DBELL_ARM);
    g_assert_cmphex(qtest_readl(qts, mmio_base + ITD_REG_DMA_RESULT), ==,
                    ITD_DMA_RESULT_BUSY);
    (void)qtest_readl(qts, mmio_base + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, mmio_base + ITD_REG_DMA_RESULT);
}

static uint64_t hp_i2000_sparse_io_address(uint16_t port)
{
    return HP_I2000_LEGACY_IO_BASE +
        ((uint64_t)(port >> 2) << 12) + (port & 0xfffU);
}

static uint8_t hp_i2000_inb(QTestState *qts, uint16_t port)
{
    return qtest_readb(qts, hp_i2000_sparse_io_address(port));
}

static void hp_i2000_outb(QTestState *qts, uint16_t port, uint8_t value)
{
    qtest_writeb(qts, hp_i2000_sparse_io_address(port), value);
}

static uint16_t hp_i2000_inw(QTestState *qts, uint16_t port)
{
    return qtest_readw(qts, hp_i2000_sparse_io_address(port));
}

static void hp_i2000_outw(QTestState *qts, uint16_t port, uint16_t value)
{
    qtest_writew(qts, hp_i2000_sparse_io_address(port), value);
}

static uint32_t hp_i2000_pid_rte_low(unsigned int pin)
{
    return HP_I2000_PID_RTE_BASE + pin * 2U;
}

static void hp_i2000_pid_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, HP_I2000_PID_BASE + HP_I2000_PID_IOREGSEL, reg);
    qtest_writel(qts, HP_I2000_PID_BASE + HP_I2000_PID_IOWIN, value);
}

static bool hp_i2000_sapic_irr_has_vector(QTestState *qts, uint8_t vector)
{
    g_autofree char *registers = qtest_hmp(qts, "info registers");
    const char *line = strstr(registers, "SAPIC IRR:");
    uint64_t irr[4];

    g_assert_nonnull(line);
    g_assert_cmpint(sscanf(line, "SAPIC IRR: %" SCNx64 " %" SCNx64
                          " %" SCNx64 " %" SCNx64,
                          &irr[0], &irr[1], &irr[2], &irr[3]), ==, 4);
    return (irr[vector / 64] & BIT_ULL(vector % 64)) != 0;
}

static bool hp_i2000_sapic_irr_wait_for_vector(QTestState *qts,
                                                uint8_t vector)
{
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        if (hp_i2000_sapic_irr_has_vector(qts, vector)) {
            return true;
        }
        g_usleep(1000);
    }
    return false;
}

static uint8_t hp_i2000_vga_indexed_read(QTestState *qts,
                                         uint16_t index_port,
                                         uint16_t data_port, uint8_t index)
{
    hp_i2000_outb(qts, index_port, index);
    return hp_i2000_inb(qts, data_port);
}

static void hp_i2000_assert_int10_rom(QTestState *qts)
{
    uint8_t rom[HP_I2000_RAGE128_ROM_SIZE];
    uint8_t vector[4];
    uint16_t pcir;

    qtest_memread(qts, HP_I2000_RAGE128_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom), ==, 0xaa55);
    g_assert_cmpuint(rom[2] * 512U, ==, sizeof(rom));
    g_assert_cmphex(rom[3], !=, 0xcb);
    pcir = lduw_le_p(rom + 0x18);
    g_assert_cmphex(pcir, ==, HP_I2000_RAGE128_PCIR_OFFSET);
    g_assert_cmpmem(rom + pcir, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + pcir + 4), ==, 0x1002);
    g_assert_cmphex(lduw_le_p(rom + pcir + 6), ==, 0x5046);
    g_assert_cmpuint(lduw_le_p(rom + pcir + 0x10) * 512U, ==,
                     sizeof(rom));
    g_assert_cmphex(hp_i2000_checksum(rom, sizeof(rom)), ==, 0);
    g_assert_cmphex(rom[HP_I2000_INT10_HANDLER_OFFSET], !=, 0xcb);

    qtest_memread(qts, HP_I2000_INT10_VECTOR_ADDR, vector, sizeof(vector));
    g_assert_cmphex(lduw_le_p(vector), ==,
                    HP_I2000_INT10_HANDLER_OFFSET);
    g_assert_cmphex(lduw_le_p(vector + 2), ==,
                    HP_I2000_RAGE128_ROM_BASE >> 4);
}

static void hp_i2000_int10_write_request(
    QTestState *qts, const HPI2000Int10Registers *regs)
{
    const uint16_t values[] = {
        regs->ax, regs->bx, regs->cx, regs->dx, regs->di, regs->es,
    };
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(values); i++) {
        hp_i2000_outw(qts, HP_I2000_INT10_IO_BASE + i * 2, values[i]);
    }
    if (regs->input_signature != 0) {
        hp_i2000_outw(qts, HP_I2000_INT10_IO_DATA,
                      (uint16_t)regs->input_signature);
        hp_i2000_outw(qts, HP_I2000_INT10_IO_DATA,
                      (uint16_t)(regs->input_signature >> 16));
    }
}

static void hp_i2000_int10_read_result(QTestState *qts,
                                       HPI2000Int10Registers *regs)
{
    regs->ax = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE);
    regs->bx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 2);
    regs->cx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 4);
    regs->dx = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 6);
    regs->di = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 8);
    regs->es = hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE + 10);
}

static size_t hp_i2000_int10_call(QTestState *qts,
                                  HPI2000Int10Registers *regs,
                                  uint8_t *response, size_t response_size)
{
    size_t word_count;
    size_t i;

    hp_i2000_int10_write_request(qts, regs);
    hp_i2000_outw(qts, HP_I2000_INT10_IO_EXEC,
                  HP_I2000_INT10_TRIGGER);
    word_count = hp_i2000_inw(qts, HP_I2000_INT10_IO_EXEC);
    g_assert_cmpuint(word_count * 2, <=, response_size);
    for (i = 0; i < word_count; i++) {
        stw_le_p(response + i * 2,
                 hp_i2000_inw(qts, HP_I2000_INT10_IO_DATA));
    }
    hp_i2000_int10_read_result(qts, regs);
    return word_count * 2;
}

static void hp_i2000_int10_set_mode(QTestState *qts, uint16_t ax)
{
    HPI2000Int10Registers regs = { .ax = ax };

    g_assert_cmpuint(hp_i2000_int10_call(qts, &regs, NULL, 0), ==, 0);
    g_assert_cmphex(regs.ax, ==, ax);
}

static void hp_i2000_assert_mode12(QTestState *qts, bool no_clear)
{
    hp_i2000_outw(qts, HP_I2000_VBE_INDEX_PORT,
                  HP_I2000_VBE_ENABLE_INDEX);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_VBE_DATA_PORT), ==, 0);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_VGA_MISC_READ_PORT), ==,
                    0xe3);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_SEQ_INDEX_PORT,
                        HP_I2000_VGA_SEQ_DATA_PORT, 2), ==, 0x0f);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_SEQ_INDEX_PORT,
                        HP_I2000_VGA_SEQ_DATA_PORT, 4), ==, 0x06);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 1), ==, 0x4f);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 0x12), ==, 0xdf);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_CRTC_INDEX_PORT,
                        HP_I2000_VGA_CRTC_DATA_PORT, 0x13), ==, 0x28);
    g_assert_cmphex(hp_i2000_vga_indexed_read(
                        qts, HP_I2000_VGA_GFX_INDEX_PORT,
                        HP_I2000_VGA_GFX_DATA_PORT, 6), ==, 0x05);

    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_MODE), ==, 0x12);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_COLUMNS), ==, 80);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_PAGE_SIZE), ==,
                    0xa000);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_VIDEO_PAGE_START), ==, 0);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_ROWS), ==, 29);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_BDA_CHARACTER_HEIGHT), ==, 16);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_BDA_VIDEO_CONTROL), ==,
                    no_clear ? 0xe0 : 0x60);
}

static uint32_t hp_i2000_int10_far_to_linear(uint32_t pointer)
{
    return (pointer >> 16) * 16 + (pointer & 0xffff);
}

static bool hp_i2000_int10_mode_list_contains(QTestState *qts,
                                               uint32_t address,
                                               uint16_t expected)
{
    size_t i;

    for (i = 0; i < HP_I2000_RAGE128_ROM_SIZE / 2; i++, address += 2) {
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

static void hp_i2000_assert_int10_vbe(QTestState *qts)
{
    uint8_t response[512];
    HPI2000Int10Registers regs = {
        .ax = 0x4f00,
        .di = 0x0100,
        .es = 0x2000,
        .input_signature = HP_I2000_VBE2_SIGNATURE,
    };
    uint32_t memory_size;
    uint32_t max_width;
    uint32_t modes;
    size_t length;

    length = hp_i2000_int10_call(qts, &regs,
                                 response, sizeof(response));
    g_assert_cmpuint(length, ==, 512);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmpmem(response, 4, "VESA", 4);
    memory_size = (uint32_t)lduw_le_p(response + 18) * (64 * KiB);
    modes = hp_i2000_int10_far_to_linear(ldl_le_p(response + 14));
    g_assert_cmphex(modes, ==, HP_I2000_RAGE128_ROM_BASE +
                    HP_I2000_INT10_MODE_LIST_OFFSET);
    g_assert_true(hp_i2000_int10_mode_list_contains(qts, modes, 0x111));

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f01,
        .cx = 0x0111,
    };
    length = hp_i2000_int10_call(qts, &regs,
                                 response, sizeof(response));
    g_assert_cmpuint(length, ==, 256);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(response[28], ==, 0);
    g_assert_cmphex((uint32_t)ldl_le_p(response + 40), ==,
                    HP_I2000_RAGE128_FB_BASE);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f02,
        .bx = 0xc143,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);

    regs = (HPI2000Int10Registers) { .ax = 0x4f03 };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 0xc143);

    regs = (HPI2000Int10Registers) { .ax = 0x4f05 };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x034f);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .cx = 801,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);
    g_assert_cmphex(regs.dx, ==, memory_size / 3232);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 2,
        .cx = 3201,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .cx = VBE_DISPI_MAX_XRES + 1,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x024f);

    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 1,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, 3232);
    g_assert_cmphex(regs.cx, ==, 808);

    max_width = MIN((uint32_t)VBE_DISPI_MAX_XRES,
                    memory_size / 600 / 4) & ~7U;
    regs = (HPI2000Int10Registers) {
        .ax = 0x4f06,
        .bx = 3,
    };
    length = hp_i2000_int10_call(qts, &regs, response, sizeof(response));
    g_assert_cmpuint(length, ==, 0);
    g_assert_cmphex(regs.ax, ==, 0x004f);
    g_assert_cmphex(regs.bx, ==, max_width * 4);
    g_assert_cmphex(regs.cx, ==, max_width);
    g_assert_cmphex(regs.dx, ==, memory_size / (max_width * 4));
}

static void hp_i2000_activate_i8042(QTestState *qts)
{
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_ENTER_KEY);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_LDN_SELECT_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_LDN);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_I8042_KBD_IRQ_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_KBD_IRQ);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_I8042_MOUSE_IRQ_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_I8042_MOUSE_IRQ);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_ACTIVATE_REGISTER);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_DATA_PORT,
                  IA64_I2000_PROFILE_SIO_ACTIVE_VALUE);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_SIO_INDEX_PORT,
                  IA64_I2000_PROFILE_SIO_EXIT_KEY);
}

static void hp_i2000_assert_device(QTestState *qts, uint8_t bus,
                                    unsigned int devfn, uint16_t vendor,
                                    uint16_t device)
{
    g_assert_cmphex(hp_i2000_config_readl(qts, bus, devfn, 0), ==,
                    (uint32_t)device << 16 | vendor);
}

static void hp_i2000_assert_descriptor(QTestState *qts, uint64_t ram_size,
                                        uint32_t expected_size,
                                        bool nvram_persistent)
{
    uint8_t storage[HP_I2000_HIGH_DESCRIPTOR_SIZE] = { 0 };
    IA64PlatformDescriptor *descriptor =
        (IA64PlatformDescriptor *)storage;
    const IA64PlatformRamRange *ranges;
    const IA64PlatformPciRoute *routes;
    const IA64PlatformI2000Profile *profile;
    uint64_t high_size = ram_size - HP_I2000_LOW_RAM_LIMIT;

    qtest_memread(qts, HP_I2000_DESCRIPTOR_GPA, storage, sizeof(*descriptor));
    g_assert_cmpuint(le32_to_cpu(descriptor->TotalSize), ==, expected_size);
    qtest_memread(qts, HP_I2000_DESCRIPTOR_GPA, storage, expected_size);

    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_I2000);
    g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==, ram_size);
    g_assert_cmphex(le64_to_cpu(descriptor->LowRamEnd), ==,
                    HP_I2000_LOW_RAM_LIMIT);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==,
                     high_size ? 2 : 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     HP_I2000_PCI_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==, 5);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramBase), ==,
                    IA64_I2000_PROFILE_NVRAM_BASE);
    g_assert_cmphex(le64_to_cpu(descriptor->NvramSize), ==,
                    IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(
        le32_to_cpu(descriptor->Flags) &
            IA64_PLATFORM_FLAG_NVRAM_PERSISTENT,
        ==, nvram_persistent ? IA64_PLATFORM_FLAG_NVRAM_PERSISTENT : 0);
    g_assert_cmphex(
        le32_to_cpu(descriptor->Flags) &
            ~IA64_PLATFORM_FLAG_NVRAM_PERSISTENT,
        ==, IA64_PLATFORM_HP_I2000_REQUIRED_FLAGS);
    g_assert_cmphex(ia64_platform_firmware_compat_flags(
                        le32_to_cpu(descriptor->PlatformId),
                        le32_to_cpu(descriptor->Flags)), ==,
                    IA64_FW_COMPAT_ALL_MASK);
    g_assert_cmpuint(hp_i2000_checksum(storage, expected_size), ==, 0);

    ranges = (const IA64PlatformRamRange *)(
        storage + le32_to_cpu(descriptor->RamRangeOffset));
    g_assert_cmphex(le64_to_cpu(ranges[0].Base), ==, 0);
    g_assert_cmphex(le64_to_cpu(ranges[0].Size), ==,
                    HP_I2000_LOW_RAM_LIMIT);
    if (high_size) {
        g_assert_cmphex(le64_to_cpu(ranges[1].Base), ==,
                        HP_I2000_HIGH_RAM_BASE);
        g_assert_cmphex(le64_to_cpu(ranges[1].Size), ==, high_size);
    }

    routes = (const IA64PlatformPciRoute *)(
        storage + le32_to_cpu(descriptor->PciRouteOffset));
    g_assert_cmpuint(le16_to_cpu(routes[0].Segment), ==, 0);
    g_assert_cmpuint(routes[0].Bus, ==, 0);
    g_assert_cmpuint(routes[0].Device, ==, 5);
    g_assert_cmpuint(routes[0].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[0].Gsi), ==, 16);
    g_assert_cmpuint(le16_to_cpu(routes[2].Segment), ==, 0);
    g_assert_cmpuint(routes[2].Bus, ==, 0);
    g_assert_cmpuint(routes[2].Device, ==, 2);
    g_assert_cmpuint(routes[2].Pin, ==, 3);
    g_assert_cmpuint(le32_to_cpu(routes[2].Gsi), ==, 19);
    g_assert_cmpuint(le16_to_cpu(routes[3].Segment), ==, 0);
    g_assert_cmpuint(routes[3].Bus, ==, 0x20);
    g_assert_cmpuint(routes[3].Device, ==, 3);
    g_assert_cmpuint(routes[3].Pin, ==, 0);
    g_assert_cmpuint(le32_to_cpu(routes[3].Gsi), ==, 20);

    profile = (const IA64PlatformI2000Profile *)(
        storage + le32_to_cpu(descriptor->ProfileOffset));
    g_assert_cmpuint(le32_to_cpu(profile->ProfileType), ==,
                     IA64_PLATFORM_PROFILE_TYPE_HP_I2000);
    g_assert_cmpuint(profile->IdeProgIf, ==,
                     IA64_I2000_PROFILE_IDE_PROG_IF);
}

static void test_hp_i2000_machine_identity(void)
{
    QTestState *qts = hp_i2000_start("2G");
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-machines'}");
    QList *machines = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool found = false;

    QLIST_FOREACH_ENTRY(machines, entry) {
        QDict *machine = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(machine, "name"), "hp-i2000")) {
            g_assert_cmpstr(qdict_get_str(machine, "default-cpu-type"), ==,
                            "merced-ia64-cpu");
            g_assert_cmpint(qdict_get_int(machine, "cpu-max"), ==, 2);
            g_assert_cmpstr(qdict_get_str(machine, "default-ram-id"), ==,
                            "hp-i2000.ram");
            found = true;
            break;
        }
    }
    g_assert_true(found);
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, false);
    qtest_quit(qts);
}

static void test_hp_i2000_constraints(void)
{
    g_assert_cmphex(HP_I2000_MAX_RAM_SIZE, ==, 16 * GiB);
    hp_i2000_assert_start_fails("-m", "1G", "at least 2 GiB");
}

static void test_hp_i2000_storage_defaults(void)
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
    QTestState *qts = hp_i2000_start_with_storage("");

    hp_i2000_assert_block_devices(qts, NULL, 0);
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage(
        "-drive media=disk,file=null-co://,format=raw "
        "-drive media=cdrom,file=null-co://,format=raw");
    hp_i2000_assert_block_devices(qts, automatic,
                                  G_N_ELEMENTS(automatic));
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage(
        "-drive if=ide,bus=0,unit=0,media=disk,file=null-co://,format=raw "
        "-drive if=ide,bus=0,unit=1,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=0,media=cdrom,file=null-co://,format=raw "
        "-drive if=ide,bus=1,unit=1,media=disk,file=null-co://,format=raw "
        "-drive if=scsi,bus=0,unit=6,media=cdrom,file=null-co://,format=raw "
        "-drive if=scsi,bus=1,unit=6,media=disk,file=null-co://,format=raw");
    hp_i2000_assert_block_devices(qts, explicit_topology,
                                  G_N_ELEMENTS(explicit_topology));
    qtest_quit(qts);

    qts = hp_i2000_start_with_storage("-cdrom null-co://");
    hp_i2000_assert_block_devices(qts, cdrom_shortcut,
                                  G_N_ELEMENTS(cdrom_shortcut));
    qtest_quit(qts);
}

static void test_hp_i2000_ram_descriptor(void)
{
    QTestState *qts = hp_i2000_start("3G");

    hp_i2000_assert_descriptor(qts, 3 * GiB,
                               HP_I2000_HIGH_DESCRIPTOR_SIZE, false);
    qtest_writel(qts, HP_I2000_HIGH_RAM_BASE, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, HP_I2000_HIGH_RAM_BASE), ==,
                    0x12345678);
    qtest_quit(qts);

    qts = hp_i2000_start("8G");
    hp_i2000_assert_descriptor(qts, 8 * GiB,
                               HP_I2000_HIGH_DESCRIPTOR_SIZE, false);
    qtest_writel(qts, HP_I2000_HIGH_RAM_BASE + 6 * GiB - 4,
                 0x89abcdef);
    g_assert_cmphex(qtest_readl(
                        qts, HP_I2000_HIGH_RAM_BASE + 6 * GiB - 4), ==,
                    0x89abcdef);
    qtest_quit(qts);
}

static void test_hp_i2000_pci_dma_ram_map(void)
{
    static const struct {
        uint8_t bus;
        uint64_t mmio_base;
    } roots[] = {
        { 0x00, UINT64_C(0x98000000) },
        { 0x20, UINT64_C(0xa8000000) },
        { 0x40, UINT64_C(0xb8000000) },
    };
    const unsigned int devfn = PCI_DEVFN(HP_I2000_DMA_TEST_SLOT, 0);
    const uint64_t high_ram = HP_I2000_HIGH_RAM_BASE;
    uint64_t mmio_base = 0;
    uint8_t bus = 0;
    QTestState *qts;
    uint32_t result;
    unsigned int root;

    if (!qtest_has_device(TYPE_IOMMU_TESTDEV)) {
        g_test_skip("iommu-testdev is unavailable");
        return;
    }

    qts = hp_i2000_start_with_dma_testdev();
    for (root = 0; root < G_N_ELEMENTS(roots); root++) {
        if (hp_i2000_config_readl(qts, roots[root].bus, devfn, 0) ==
            ((uint32_t)IOMMU_TESTDEV_DEVICE_ID << 16 |
             IOMMU_TESTDEV_VENDOR_ID)) {
            bus = roots[root].bus;
            mmio_base = roots[root].mmio_base;
            break;
        }
    }
    g_assert_cmpuint(root, <, G_N_ELEMENTS(roots));
    hp_i2000_config_writel(qts, bus, devfn, PCI_BASE_ADDRESS_0,
                           mmio_base);
    hp_i2000_config_writew(qts, bus, devfn, PCI_COMMAND,
                           PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    qtest_writel(qts, high_ram, HP_I2000_DMA_TEST_SENTINEL);
    result = hp_i2000_dma_testdev_trigger(
        qts, mmio_base, high_ram, high_ram);
    g_assert_cmphex(result, ==, 0);
    g_assert_cmphex(qtest_readl(
                        qts, mmio_base + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_OK);
    g_assert_cmphex(qtest_readl(qts, high_ram), ==, ITD_DMA_WRITE_VAL);

    /* The RAM hole starts at 2 GiB; that boundary must stay unmapped. */
    qtest_writel(qts, high_ram, HP_I2000_DMA_TEST_SENTINEL);
    result = hp_i2000_dma_testdev_trigger(
        qts, mmio_base, HP_I2000_LOW_RAM_LIMIT, high_ram);
    g_assert_cmphex(result, ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(
                        qts, mmio_base + ITD_REG_DMA_MEMTX_RESULT), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmphex(qtest_readl(qts, high_ram), ==,
                    HP_I2000_DMA_TEST_SENTINEL);
    qtest_quit(qts);
}

static void test_hp_i2000_pci_layout_and_reset(void)
{
    QTestState *qts = hp_i2000_start("2G");
    unsigned int function;
    unsigned int expander;

    for (function = 0; function < INTEL_82468GX_IFB_FUNCTIONS;
         function++) {
        hp_i2000_assert_device(
            qts, 0, PCI_DEVFN(2, function),
            INTEL_82468GX_IFB_VENDOR_ID,
            INTEL_82468GX_IFB_LPC_DEVICE_ID + function);
    }
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(2, 1), PCI_CLASS_PROG), ==,
                    IA64_I2000_PROFILE_IDE_PROG_IF);
    hp_i2000_assert_device(qts, 0, PCI_DEVFN(3, 0), 0x8086, 0x1229);
    hp_i2000_assert_device(
        qts, 0x20,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        0x1077, 0x1216);
    hp_i2000_assert_device(qts, 0x20, PCI_DEVFN(3, 0), 0x1000, 0x0012);

    hp_i2000_assert_device(qts, 0xff, PCI_DEVFN(0, 0), 0x8086, 0x84e0);
    hp_i2000_assert_device(qts, 0xff, PCI_DEVFN(4, 0), 0x8086, 0x84e1);
    hp_i2000_assert_device(qts, 0xff, PCI_DEVFN(5, 0), 0x8086, 0x84e3);
    hp_i2000_assert_device(qts, 0xff, PCI_DEVFN(5, 1), 0x8086, 0x84e4);
    for (expander = 0; expander < HP_I2000_PCI_ROOT_COUNT; expander++) {
        hp_i2000_assert_device(qts, 0xff,
                               PCI_DEVFN(0x10 + expander, 0),
                               0x8086, 0x84cb);
    }

    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(2, 0), PCI_COMMAND), ==, 0x0007);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(2, 2), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, PCI_DEVFN(2, 2),
                        PCI_BASE_ADDRESS_4), ==, 0x00001101);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, PCI_DEVFN(2, 2),
                        PCI_INTERRUPT_LINE), ==, 19);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(2, 0), PCI_COMMAND, 0x0108);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(2, 0), PCI_COMMAND), ==, 0x010f);
    hp_i2000_config_writel(
        qts, 0x20,
        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                  ISP12160_QEMU_I2000_FUNCTION),
        PCI_BASE_ADDRESS_1, 0);
    hp_i2000_config_writel(qts, 0x20, PCI_DEVFN(3, 0),
                           PCI_BASE_ADDRESS_1, 0);
    qtest_writeb(qts, HP_I2000_CF8_PA + 1, 0x5a);
    qtest_writew(qts, HP_I2000_CF8_PA + 2, 0xa55a);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0x5a);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0xa55a);
    qtest_system_reset(qts);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, PCI_DEVFN(2, 0), PCI_COMMAND), ==, 0x0007);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0x20,
                        PCI_DEVFN(ISP12160_QEMU_I2000_DEVICE,
                                  ISP12160_QEMU_I2000_FUNCTION),
                        PCI_BASE_ADDRESS_1), ==,
                    ISP12160_QEMU_I2000_BAR_ADDRESS);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0x20, PCI_DEVFN(3, 0), PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                    PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0x20, PCI_DEVFN(3, 0),
                        PCI_BASE_ADDRESS_0), ==, 0x00006001);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0x20, PCI_DEVFN(3, 0),
                        PCI_BASE_ADDRESS_1), ==, 0xa0020000);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0x20, PCI_DEVFN(3, 0),
                        PCI_BASE_ADDRESS_2), ==, 0xa0022000);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0x20, PCI_DEVFN(3, 0),
                        PCI_INTERRUPT_LINE), ==, 20);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_acpi_pm(void)
{
    QTestState *qts = hp_i2000_start("2G");
    const unsigned int ifb = PCI_DEVFN(2, 0);
    const uint16_t base = IA64_I2000_PROFILE_ACPI_PM_IO_BASE;
    uint64_t cnt = hp_i2000_sparse_io_address(base + 4U);
    uint64_t timer = hp_i2000_sparse_io_address(base + 8U);
    uint64_t gpe = hp_i2000_sparse_io_address(base + 0x0cU);
    uint32_t first;
    uint32_t second;

    g_assert_cmphex(hp_i2000_config_readl(qts, 0, ifb, 0x40), ==,
                    base | 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x45), ==, 0U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 0U);
    qtest_writew(qts, cnt, 1U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 1U);
    g_assert_cmphex(qtest_readw(qts, gpe), ==, 0x0800U);
    qtest_writew(qts, gpe, 0x0800U);
    g_assert_cmphex(qtest_readw(qts, gpe), ==, 0U);

    first = qtest_readl(qts, timer) & 0x00ffffffU;
    qtest_clock_step(qts, 1000000);
    second = qtest_readl(qts, timer) & 0x00ffffffU;
    g_assert_cmpuint(second, !=, first);

    hp_i2000_config_writeb(qts, 0, ifb, 0x44, 0U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 0U);
    qtest_system_reset(qts);
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, ifb, 0x40), ==,
                    base | 1U);
    g_assert_cmphex(hp_i2000_config_readb(qts, 0, ifb, 0x44), ==, 1U);
    g_assert_cmphex(qtest_readw(qts, cnt), ==, 0U);
    qtest_quit(qts);
}

static void test_hp_i2000_pib_inta(void)
{
    QTestState *qts = hp_i2000_start("2G");

    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x11);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_COMMAND, 0x11);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x20);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x28);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x04);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x02);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0x01);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0x01);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_DATA, 0xfd);
    hp_i2000_outb(qts, HP_I2000_PIC_SLAVE_DATA, 0xff);

    hp_i2000_activate_i8042(qts);

    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_SELF_TEST);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x0a);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==,
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ));

    g_assert_cmphex(qtest_readb(qts, HP_I2000_PIB_INTA_PA), ==,
                    0x20U + IA64_I2000_PROFILE_I8042_KBD_IRQ);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x0b);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==,
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ));

    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    HP_I2000_I8042_SELF_TEST_OK);
    hp_i2000_outb(qts, HP_I2000_PIC_MASTER_COMMAND, 0x20);
    g_assert_cmphex(hp_i2000_inb(qts, HP_I2000_PIC_MASTER_COMMAND) &
                    BIT(IA64_I2000_PROFILE_I8042_KBD_IRQ), ==, 0);
    qtest_quit(qts);
}

static void test_hp_i2000_isa_pid_fanout(void)
{
    const uint8_t keyboard_vector = 0x51U;
    const uint8_t mouse_vector = 0x5cU;
    QTestState *qts = hp_i2000_start("2G");

    /* Exercise the i8042's keyboard and auxiliary IRQ outputs independently. */
    hp_i2000_activate_i8042(qts);

    hp_i2000_pid_write(
        qts, hp_i2000_pid_rte_low(IA64_I2000_PROFILE_I8042_KBD_IRQ),
        keyboard_vector);
    g_assert_false(hp_i2000_sapic_irr_has_vector(qts, keyboard_vector));
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_SELF_TEST);
    g_assert_true(hp_i2000_sapic_irr_wait_for_vector(qts, keyboard_vector));
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    HP_I2000_I8042_SELF_TEST_OK);

    hp_i2000_pid_write(
        qts, hp_i2000_pid_rte_low(IA64_I2000_PROFILE_I8042_MOUSE_IRQ),
        mouse_vector);
    g_assert_false(hp_i2000_sapic_irr_has_vector(qts, mouse_vector));
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  HP_I2000_I8042_WRITE_AUX_OBUF);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_DATA_PORT, 0xa5U);
    g_assert_true(hp_i2000_sapic_irr_wait_for_vector(qts, mouse_vector));
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT) &
                    (HP_I2000_I8042_STATUS_OBF |
                     HP_I2000_I8042_STATUS_AUX_OBF), ==,
                    HP_I2000_I8042_STATUS_OBF |
                    HP_I2000_I8042_STATUS_AUX_OBF);
    g_assert_cmphex(hp_i2000_inb(
                        qts, IA64_I2000_PROFILE_I8042_DATA_PORT), ==,
                    0xa5U);
    qtest_quit(qts);
}

static void test_hp_i2000_i8042_reset(void)
{
    QTestState *qts = hp_i2000_start("2G");
    g_autoptr(QDict) event = NULL;
    QDict *data;

    hp_i2000_activate_i8042(qts);
    hp_i2000_outb(qts, IA64_I2000_PROFILE_I8042_COMMAND_PORT,
                  IA64_I2000_PROFILE_I8042_RESET_COMMAND);
    event = qtest_qmp_eventwait_ref(qts, "RESET");
    data = qdict_get_qdict(event, "data");
    g_assert_true(qdict_get_bool(data, "guest"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-reset");
    qtest_quit(qts);
}

static void hp_i2000_assert_rage128(QTestState *qts)
{
    uint8_t rom[HP_I2000_RAGE128_ROM_SIZE];
    unsigned int devfn = PCI_DEVFN(5, 0);

    hp_i2000_assert_device(qts, 0, devfn, 0x1002, 0x5046);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, devfn, PCI_CLASS_DEVICE), ==, 0x0300);
    g_assert_cmphex(hp_i2000_config_readw(
                        qts, 0, devfn, PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_0), ==, 0x90000008);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_1), ==, 0x00001001);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_BASE_ADDRESS_2), ==, 0x94000000);
    g_assert_cmphex(hp_i2000_config_readl(
                        qts, 0, devfn, PCI_ROM_ADDRESS), ==, 0);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_INTERRUPT_LINE), ==, 16);
    g_assert_cmphex(hp_i2000_config_readb(
                        qts, 0, devfn, PCI_INTERRUPT_PIN), ==, 1);

    qtest_memread(qts, HP_I2000_RAGE128_ROM_BASE, rom, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom), ==, 0xaa55);
    g_assert_cmpuint(rom[2] * 512U, ==, sizeof(rom));
    g_assert_cmphex(lduw_le_p(rom + 0x18), ==,
                    HP_I2000_RAGE128_PCIR_OFFSET);
    g_assert_cmpmem(rom + HP_I2000_RAGE128_PCIR_OFFSET, 4, "PCIR", 4);
    g_assert_cmphex(lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 4),
                    ==, 0x1002);
    g_assert_cmphex(lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 6),
                    ==, 0x5046);
    g_assert_cmphex(
        lduw_le_p(rom + HP_I2000_RAGE128_PCIR_OFFSET + 0x10) * 512U,
        ==, sizeof(rom));
    g_assert_cmphex(rom[HP_I2000_RAGE128_PCIR_OFFSET + 0x15], ==, 0x80);
    g_assert_cmphex(hp_i2000_checksum(rom, sizeof(rom)), ==, 0);
}

static void test_hp_i2000_graphics_defaults(void)
{
    QTestState *qts = hp_i2000_start_defaults("");

    hp_i2000_assert_rage128(qts);
    g_assert_cmphex(qtest_readl(qts, UINT64_C(0x940000f8)), ==, 0x04000000);
    qtest_writel(qts, UINT64_C(0x90001000), 0x12345678);
    g_assert_cmphex(qtest_readl(qts, UINT64_C(0x90001000)), ==, 0x12345678);

    hp_i2000_config_writel(qts, 0, PCI_DEVFN(5, 0), PCI_BASE_ADDRESS_0, 0);
    hp_i2000_config_writew(qts, 0, PCI_DEVFN(5, 0), PCI_COMMAND, 0);
    qtest_system_reset(qts);
    hp_i2000_assert_rage128(qts);
    qtest_quit(qts);
}

static void test_hp_i2000_graphics_options(void)
{
    QTestState *qts = hp_i2000_start("2G");

    g_assert_cmphex(hp_i2000_config_readl(qts, 0, PCI_DEVFN(5, 0), 0), ==,
                    0xffffffffU);
    qtest_quit(qts);

    qts = hp_i2000_start_defaults("-vga none");
    g_assert_cmphex(hp_i2000_config_readl(qts, 0, PCI_DEVFN(5, 0), 0), ==,
                    0xffffffffU);
    qtest_quit(qts);

    qts = hp_i2000_start_with_options("-vga ati");
    hp_i2000_assert_rage128(qts);
    qtest_quit(qts);
}

static void test_hp_i2000_int10(void)
{
    uint8_t first_marker[16];
    uint8_t last_marker[16];
    uint8_t actual[16];
    uint8_t zero[16] = { 0 };
    uint64_t last = HP_I2000_RAGE128_FB_BASE +
        HP_I2000_VGA_PLANAR_SIZE - sizeof(last_marker);
    g_autofree char *tmpdir = NULL;
    g_autofree char *ppm = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts = hp_i2000_start_with_options("-vga ati");

    hp_i2000_assert_int10_rom(qts);
    hp_i2000_assert_int10_vbe(qts);

    memset(first_marker, 0xa5, sizeof(first_marker));
    memset(last_marker, 0x5a, sizeof(last_marker));
    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0012);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));

    qtest_writeb(qts, HP_I2000_VGA_LEGACY_BASE, 0xff);
    tmpdir = g_dir_make_tmp("hp-i2000-mode12-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    ppm = g_build_filename(tmpdir, "mode12.ppm", NULL);
    qtest_qmp_assert_success(qts,
                             "{'execute':'screendump','arguments':"
                             " {'filename':%s}}", ppm);
    hp_i2000_assert_ppm_pixel(ppm, 640, 480, 0, 0,
                              0xff, 0xff, 0xff);
    hp_i2000_assert_ppm_pixel(ppm, 640, 480, 8, 0, 0, 0, 0);

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0092);
    hp_i2000_assert_mode12(qts, true);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    first_marker, sizeof(first_marker));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual),
                    last_marker, sizeof(last_marker));

    qtest_writeb(qts, HP_I2000_RAGE128_ROM_BASE, 0);
    qtest_writel(qts, HP_I2000_INT10_VECTOR_ADDR, 0);
    hp_i2000_outw(qts, HP_I2000_INT10_IO_BASE, 0xffff);
    qtest_system_reset(qts);
    hp_i2000_assert_int10_rom(qts);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_INT10_IO_BASE), ==, 0);

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    hp_i2000_int10_set_mode(qts, 0x0012);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_quit(qts);
    g_assert_cmpint(g_unlink(ppm), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_hp_i2000_nvram(void)
{
    const uint64_t initial_value = UINT64_C(0x1122334455667788);
    const uint64_t committed_value = UINT64_C(0x8877665544332211);
    const uint64_t volatile_value = UINT64_C(0xa5a5a5a55a5a5a5a);
    g_autofree char *tmpdir = NULL;
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    g_autofree char *machine_options = NULL;
    g_autofree char *machine_name = NULL;
    g_autofree uint8_t *prefix =
        g_malloc0(IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_autofree char *contents = NULL;
    g_autofree char *oversized = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(QDict) response = NULL;
    gsize length = 0;
    QTestState *qts;

    qts = hp_i2000_start("2G");
    response = qtest_qmp(
        qts, "{'execute':'qom-get','arguments':"
             "{'path':'/machine','property':'nvram'}}");
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, "none");
    qobject_unref(response);
    response = NULL;
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, false);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==, 0);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==,
                    initial_value);
    qtest_quit(qts);

    qts = hp_i2000_start("2G");
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==, 0);
    qtest_quit(qts);

    tmpdir = g_dir_make_tmp("hp-i2000-nvram-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    path = g_build_filename(tmpdir, "nvram.bin", NULL);
    quoted_path = g_shell_quote(path);
    machine_options = g_strdup_printf(",nvram=%s", quoted_path);
    machine_name = g_strdup_printf("hp-i2000,nvram=%s", path);

    /* A missing backing file is created only by an explicit commit. */
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, initial_value);
    g_assert_cmphex((uint8_t)contents[length - 1], ==, 0);
    g_clear_pointer(&contents, g_free);

    /* An existing empty file also remains empty until a commit. */
    g_assert_true(g_file_set_contents(path, "", 0, &error));
    g_assert_no_error(error);
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 0);
    g_clear_pointer(&contents, g_free);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE, initial_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_I2000_PROFILE_NVRAM_SIZE);
    g_assert_cmphex(ldq_le_p(contents), ==, initial_value);
    g_clear_pointer(&contents, g_free);

    /* A legacy 64 KiB file must remain 64 KiB across load and commit. */
    stq_le_p(prefix, initial_value);
    stq_le_p(prefix + sizeof(initial_value), ~initial_value);
    g_assert_true(g_file_set_contents(
        path, (const char *)prefix, IA64_PLATFORM_MIN_NVRAM_SIZE, &error));
    g_assert_no_error(error);
    qts = hp_i2000_start_with_machine_options(machine_options, "");
    hp_i2000_assert_descriptor(qts, 2 * GiB,
                               HP_I2000_LOW_DESCRIPTOR_SIZE, true);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE), ==,
                    initial_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             sizeof(initial_value)), ==,
                    ~initial_value);
    g_assert_cmphex(qtest_readb(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_I2000_PROFILE_NVRAM_SIZE - 1U), ==, 0);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_assert_cmpmem(contents, IA64_PLATFORM_MIN_NVRAM_SIZE,
                    prefix, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80,
                 committed_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80,
                 volatile_value);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    committed_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80), ==,
                    volatile_value);
    qtest_writeq(qts,
                 IA64_I2000_PROFILE_NVRAM_BASE +
                     IA64_I2000_PROFILE_NVRAM_COMMIT_OFFSET,
                 IA64_I2000_PROFILE_NVRAM_COMMIT_MAGIC);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE);
    stq_le_p(prefix + 0x80, committed_value);
    g_assert_cmpmem(contents, IA64_PLATFORM_MIN_NVRAM_SIZE,
                    prefix, IA64_PLATFORM_MIN_NVRAM_SIZE);
    g_clear_pointer(&contents, g_free);

    qts = hp_i2000_start_with_machine_options(machine_options, "");
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    committed_value);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE +
                             IA64_PLATFORM_MIN_NVRAM_SIZE + 0x80), ==, 0);
    qtest_quit(qts);

    oversized = g_malloc0(IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    memset(oversized, 0x7d, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_assert_true(g_file_set_contents(
        path, oversized, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U, &error));
    g_assert_no_error(error);
    hp_i2000_assert_start_fails_with_machine(
        machine_name, "-m", "2G", "must be 65536 or 524288 bytes");
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_assert_cmpmem(contents, length, oversized,
                    IA64_PLATFORM_MIN_NVRAM_SIZE + 1U);
    g_clear_pointer(&contents, g_free);
    g_clear_pointer(&oversized, g_free);

    oversized = g_malloc0(IA64_I2000_PROFILE_NVRAM_SIZE + 1U);
    g_assert_true(g_file_set_contents(
        path, oversized, IA64_I2000_PROFILE_NVRAM_SIZE + 1U, &error));
    g_assert_no_error(error);
    hp_i2000_assert_start_fails_with_machine(
        machine_name, "-m", "2G", "exceeds 524288 bytes");

    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void hp_i2000_wait_for_migration(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 60 * G_TIME_SPAN_SECOND;

    for (;;) {
        QDict *result = qtest_qmp_assert_success_ref(
            qts, "{'execute':'query-migrate'}");
        const char *status = qdict_get_str(result, "status");

        if (!strcmp(status, "completed")) {
            qobject_unref(result);
            return;
        }
        if (!strcmp(status, "failed") || !strcmp(status, "cancelled")) {
            g_error("migration entered terminal status '%s'", status);
        }
        qobject_unref(result);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_hp_i2000_migration(void)
{
    g_autofree char *path = g_strdup_printf(
        "%s/hp-i2000-migration.XXXXXX", g_get_tmp_dir());
    g_autofree char *uri = NULL;
    HPI2000Int10Registers int10_request = {
        .ax = 0x0012,
        .bx = 0x1357,
        .cx = 0x2468,
        .dx = 0x369a,
        .di = 0x47bc,
        .es = 0x58de,
    };
    HPI2000Int10Registers int10_result = { 0 };
    uint8_t first_marker[16];
    uint8_t last_marker[16];
    uint8_t actual[16];
    uint8_t zero[16] = { 0 };
    uint64_t last = HP_I2000_RAGE128_FB_BASE +
        HP_I2000_VGA_PLANAR_SIZE - sizeof(last_marker);
    QTestState *qts;
    int fd;

    fd = g_mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    uri = g_strdup_printf("file:%s", path);
    memset(first_marker, 0xa5, sizeof(first_marker));
    memset(last_marker, 0x5a, sizeof(last_marker));

    qts = hp_i2000_start_with_options("-vga ati");
    qtest_writeb(qts, HP_I2000_CF8_PA + 1, 0x5a);
    qtest_writew(qts, HP_I2000_CF8_PA + 2, 0xa55a);
    qtest_writeq(qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80,
                 UINT64_C(0x123456789abcdef0));
    hp_i2000_int10_write_request(qts, &int10_request);
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate','arguments':{'uri':%s}}", uri);
    hp_i2000_wait_for_migration(qts);
    qtest_quit(qts);

    qts = hp_i2000_start_with_options("-vga ati -incoming defer");
    qtest_qmp_assert_success(
        qts, "{'execute':'migrate-incoming','arguments':"
             "{'uri':%s,'exit-on-error':false}}", uri);
    hp_i2000_wait_for_migration(qts);
    g_assert_cmphex(qtest_readb(qts, HP_I2000_CF8_PA + 1), ==, 0x5a);
    g_assert_cmphex(qtest_readw(qts, HP_I2000_CF8_PA + 2), ==, 0xa55a);
    g_assert_cmphex(qtest_readq(
                        qts, IA64_I2000_PROFILE_NVRAM_BASE + 0x80), ==,
                    UINT64_C(0x123456789abcdef0));

    qtest_memwrite(qts, HP_I2000_RAGE128_FB_BASE,
                   first_marker, sizeof(first_marker));
    qtest_memwrite(qts, last, last_marker, sizeof(last_marker));
    /* Execution copies the migrated request into the readable result bank. */
    hp_i2000_outw(qts, HP_I2000_INT10_IO_EXEC,
                  HP_I2000_INT10_TRIGGER);
    g_assert_cmphex(hp_i2000_inw(qts, HP_I2000_INT10_IO_EXEC), ==, 0);
    hp_i2000_int10_read_result(qts, &int10_result);
    g_assert_cmphex(int10_result.ax, ==, int10_request.ax);
    g_assert_cmphex(int10_result.bx, ==, int10_request.bx);
    g_assert_cmphex(int10_result.cx, ==, int10_request.cx);
    g_assert_cmphex(int10_result.dx, ==, int10_request.dx);
    g_assert_cmphex(int10_result.di, ==, int10_request.di);
    g_assert_cmphex(int10_result.es, ==, int10_request.es);
    hp_i2000_assert_mode12(qts, false);
    qtest_memread(qts, HP_I2000_RAGE128_FB_BASE,
                  actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_memread(qts, last, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hp-i2000/machine-identity",
                   test_hp_i2000_machine_identity);
    qtest_add_func("/hp-i2000/constraints", test_hp_i2000_constraints);
    qtest_add_func("/hp-i2000/storage-defaults",
                   test_hp_i2000_storage_defaults);
    qtest_add_func("/hp-i2000/ram-descriptor",
                   test_hp_i2000_ram_descriptor);
    qtest_add_func("/hp-i2000/pci-dma-ram-map",
                   test_hp_i2000_pci_dma_ram_map);
    qtest_add_func("/hp-i2000/pci-layout-reset",
                   test_hp_i2000_pci_layout_and_reset);
    qtest_add_func("/hp-i2000/acpi-pm", test_hp_i2000_acpi_pm);
    qtest_add_func("/hp-i2000/pib-inta", test_hp_i2000_pib_inta);
    qtest_add_func("/hp-i2000/isa-pid-fanout",
                   test_hp_i2000_isa_pid_fanout);
    qtest_add_func("/hp-i2000/i8042-reset", test_hp_i2000_i8042_reset);
    qtest_add_func("/hp-i2000/graphics-defaults",
                   test_hp_i2000_graphics_defaults);
    qtest_add_func("/hp-i2000/graphics-options",
                   test_hp_i2000_graphics_options);
    qtest_add_func("/hp-i2000/int10", test_hp_i2000_int10);
    qtest_add_func("/hp-i2000/nvram", test_hp_i2000_nvram);
    qtest_add_func("/hp-i2000/migration", test_hp_i2000_migration);
    return g_test_run();
}
