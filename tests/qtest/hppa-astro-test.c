/*
 * HPPA Astro and Elroy PCI host bridge tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/pci/pci_regs.h"
#include "libqtest.h"

/* machine_HP_C3700_init() maps ASTRO_HPA through translate_pa20(). */
#define ASTRO_BASE              UINT64_C(0xfffffffffed00000)
#define ASTRO_IOC_ID            0x00000
#define ASTRO_IOC_CTRL          0x00008
#define ASTRO_LMMIO_DIST_BASE   0x00360
#define ASTRO_LMMIO_DIST_MASK   0x00368
#define ASTRO_LMMIO_DIST_ROUTE  0x00370
#define ASTRO_GMMIO_DIST_BASE   0x00378
#define ASTRO_GMMIO_DIST_ROUTE  0x00388
#define ASTRO_IOC_RANGE_END     0x003d8
#define ASTRO_IOC_ROPE_CONFIG   0x20040
#define ASTRO_TLB_IBASE         0x20300
#define ASTRO_TLB_IMASK         0x20308
#define ASTRO_TLB_PCOM          0x20310
#define ASTRO_TLB_PDIR_BASE     0x20320
#define ASTRO_IOC_FLUSH_CONTROL 0x20400
#define CPU0_EIRR               UINT64_C(0xfffffffffffb0000)

#define DEFAULT_LMMIO_BASE      UINT64_C(0xfffffffff4000001)
#define CUSTOM_LMMIO_BASE       UINT64_C(0xfffffffff5000001)
#define DEFAULT_LMMIO_ROPE2     UINT64_C(0xfffffffff4200000)
#define CUSTOM_LMMIO_ROPE2      UINT64_C(0xfffffffff5200000)
#define CUSTOM_GMMIO_BASE       UINT64_C(0x0000000100000001)
#define CUSTOM_GMMIO_ROPE2      UINT64_C(0x0000000140000000)

#define ASTRO_DIRECT0_BASE      0x00300
#define ASTRO_DIRECT0_SIZE      0x00308
#define ASTRO_DIRECT0_ROUTE     0x00310
#define DIRECT_BASE_REG         UINT64_C(0xffffffff10000001)
#define DIRECT_CPU_BASE         UINT64_C(0xffffffff10000000)
#define DIRECT_PCI_BASE         UINT32_C(0x10000000)
#define DIRECT_SIZE             UINT64_C(0x00100000)
#define LSI_DSTAT               0x0c
#define LSI_ISTAT0              0x14
#define LSI_DSP                 0x2c
#define LSI_DIEN                0x39
#define LSI_SCRATCHB0           0x5c
#define LSI_ISTAT0_DIP          0x01
#define LSI_DSTAT_SIR           0x04
#define LSI_SCRIPT_MEMMOVE      UINT32_C(0xc0000000)
#define LSI_SCRIPT_INTERRUPT    UINT32_C(0x98080000)
#define DIRECT_SIGNATURE        0xa5

#define IOMMU_SCRIPT_ADDR       UINT32_C(0x00100000)
#define IOMMU_SOURCE_ADDR       UINT32_C(0x00101000)
#define IOMMU_PDIR_BASE         UINT32_C(0x00200000)
#define IOMMU_TARGET_ADDR       UINT32_C(0x00300000)
#define IOMMU_IOVA              UINT32_C(0x01002000)
#define IOMMU_IBASE             UINT64_C(0x01000001)
#define IOMMU_IMASK             UINT64_C(0xff000000)
#define IOMMU_PTE_ADDR          (IOMMU_PDIR_BASE + \
                                 ((IOMMU_IOVA >> 12) * sizeof(uint64_t)))
#define IOMMU_SCRIPT_SIZE       20
#define IOMMU_COPY_SIZE         32

#define ELROY0_BASE             (ASTRO_BASE + 0x30000)
#define EMPTY_ELROY_BASE        (ASTRO_BASE + 0x34000)
#define ELROY_FUNC_CLASS        0x00008
#define ELROY_BUS_NUMBER        0x00058
#define ELROY_LMMIO_BASE        0x00200
#define ELROY_CONFIG_ADDR       0x00040
#define ELROY_CONFIG_DATA       0x00048
#define ELROY_IO_SAPIC_SELECT   0x00800
#define ELROY_IO_SAPIC_WINDOW   0x00810
#define ELROY_IO_SAPIC_VERSION  UINT32_C(0x00200001)
#define ELROY_IO_SAPIC_RTE1_LOW 0x12
#define ELROY_IO_SAPIC_RTE1_HIGH 0x13
#define ELROY_IO_SAPIC_RTE1_RESET UINT32_C(0x0001a003)

#define TEST_PCI_SLOT           1
#define TEST_PCI_SELECTOR       (TEST_PCI_SLOT << 11)
#define TEST_PCI_SELECTOR_TAG   (UINT32_C(0x5a000000) | \
                                 TEST_PCI_SELECTOR)
#define TEST_PCI_SELECTOR_TAG64 UINT64_C(0x5aa55aa500000800)
#define TEST_LSI_ID             UINT32_C(0x00121000)

/*
 * QTest scalar accesses use target byte order.  Astro and Elroy registers
 * are little-endian, while HPPA is big-endian.
 */
static uint32_t astro_readl(QTestState *qts, uint64_t addr)
{
    return bswap32(qtest_readl(qts, addr));
}

static uint64_t astro_readq(QTestState *qts, uint64_t addr)
{
    return bswap64(qtest_readq(qts, addr));
}

static void astro_writel(QTestState *qts, uint64_t addr, uint32_t value)
{
    qtest_writel(qts, addr, bswap32(value));
}

static void astro_writeq(QTestState *qts, uint64_t addr, uint64_t value)
{
    qtest_writeq(qts, addr, bswap64(value));
}

static QTestState *astro_start(const char *extra_args)
{
    return qtest_initf("-machine A400 -m 256M -smp 1 -S -nodefaults "
                       "-display none "
                       "-device lsi53c895a,bus=pci,addr=%u %s",
                       TEST_PCI_SLOT, extra_args ?: "");
}

static void test_reset_defaults(void)
{
    QTestState *qts = astro_start(NULL);

    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_IOC_ID), ==, 0x9);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_IOC_CTRL), ==,
                    0x29cf);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_BASE), ==,
                    DEFAULT_LMMIO_BASE);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_MASK), ==,
                    UINT64_C(0xfc000000));
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_IOC_ROPE_CONFIG), ==,
                    0xc5f);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_IOC_FLUSH_CONTROL), ==,
                    0xb03);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_TLB_PCOM), ==, 0);

    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_FUNC_CLASS), ==,
                    0x6000005);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_BUS_NUMBER), ==, 0);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_LMMIO_BASE), ==,
                    0xf0000001);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==, 0);

    qtest_quit(qts);
}

static void assert_default_distributed_mappings(QTestState *qts)
{
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_BASE), ==,
                    DEFAULT_LMMIO_BASE);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_ROUTE), ==, 0);
    g_assert_cmphex(astro_readq(qts, DEFAULT_LMMIO_ROPE2), ==, UINT64_MAX);
    g_assert_cmphex(astro_readq(qts, CUSTOM_LMMIO_ROPE2), ==, 0);

    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_GMMIO_DIST_BASE), ==, 0);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_GMMIO_DIST_ROUTE), ==, 0);
    g_assert_cmphex(astro_readq(qts, CUSTOM_GMMIO_ROPE2), ==, 0);
}

static void set_custom_distributed_mappings(QTestState *qts)
{
    /* Route zero is clamped to 1 MiB per LMMIO rope. */
    astro_writeq(qts, ASTRO_BASE + ASTRO_LMMIO_DIST_ROUTE, 0);
    astro_writeq(qts, ASTRO_BASE + ASTRO_LMMIO_DIST_BASE,
                 CUSTOM_LMMIO_BASE);

    /* Route zero is clamped to 512 MiB per GMMIO rope. */
    astro_writeq(qts, ASTRO_BASE + ASTRO_GMMIO_DIST_ROUTE, 0);
    astro_writeq(qts, ASTRO_BASE + ASTRO_GMMIO_DIST_BASE,
                 CUSTOM_GMMIO_BASE);
}

static void assert_custom_distributed_mappings(QTestState *qts)
{
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_BASE), ==,
                    CUSTOM_LMMIO_BASE);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_LMMIO_DIST_ROUTE), ==, 0);
    g_assert_cmphex(astro_readq(qts, DEFAULT_LMMIO_ROPE2), ==, 0);
    g_assert_cmphex(astro_readq(qts, CUSTOM_LMMIO_ROPE2), ==, UINT64_MAX);

    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_GMMIO_DIST_BASE), ==,
                    CUSTOM_GMMIO_BASE);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_GMMIO_DIST_ROUTE), ==, 0);
    g_assert_cmphex(astro_readq(qts, CUSTOM_GMMIO_ROPE2), ==, UINT64_MAX);
}

static void test_distributed_mappings_and_reset(void)
{
    QTestState *qts = astro_start(NULL);

    assert_default_distributed_mappings(qts);
    set_custom_distributed_mappings(qts);
    assert_custom_distributed_mappings(qts);

    qtest_system_reset(qts);
    assert_default_distributed_mappings(qts);

    qtest_quit(qts);
}

static uint32_t lsi_config_readl(QTestState *qts, unsigned reg)
{
    astro_writel(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG | (reg & 0xfc));
    return astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA);
}

static void lsi_config_writel(QTestState *qts, unsigned reg, uint32_t value)
{
    astro_writel(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG | (reg & 0xfc));
    astro_writel(qts, ELROY0_BASE + ELROY_CONFIG_DATA, value);
}

static void configure_lsi_direct_target(QTestState *qts)
{
    const uint16_t command = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;

    lsi_config_writel(qts, PCI_BASE_ADDRESS_1, DIRECT_PCI_BASE);
    lsi_config_writel(qts, PCI_COMMAND, command);
    g_assert_cmphex(lsi_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    DIRECT_PCI_BASE);
    g_assert_cmphex(lsi_config_readl(qts, PCI_COMMAND) & command,
                    ==, command);
}

static void enable_direct0_mapping(QTestState *qts, uint64_t route)
{
    /* Program the enable bit last so only a complete window is exposed. */
    astro_writeq(qts, ASTRO_BASE + ASTRO_DIRECT0_SIZE, DIRECT_SIZE);
    astro_writeq(qts, ASTRO_BASE + ASTRO_DIRECT0_ROUTE, route);
    astro_writeq(qts, ASTRO_BASE + ASTRO_DIRECT0_BASE, DIRECT_BASE_REG);
}

static void assert_direct0_mapping(QTestState *qts, uint64_t route)
{
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_DIRECT0_BASE), ==,
                    DIRECT_BASE_REG);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_DIRECT0_SIZE), ==,
                    DIRECT_SIZE);
    g_assert_cmphex(astro_readq(qts,
                               ASTRO_BASE + ASTRO_DIRECT0_ROUTE), ==,
                    route);
}

static void write_le32_to_ram(QTestState *qts, uint64_t addr, uint32_t value)
{
    uint8_t bytes[sizeof(value)];

    stl_le_p(bytes, value);
    qtest_memwrite(qts, addr, bytes, sizeof(bytes));
}

static void write_le64_to_ram(QTestState *qts, uint64_t addr, uint64_t value)
{
    uint8_t bytes[sizeof(value)];

    stq_le_p(bytes, value);
    qtest_memwrite(qts, addr, bytes, sizeof(bytes));
}

static uint32_t read_le32_from_ram(QTestState *qts, uint64_t addr)
{
    uint8_t bytes[sizeof(uint32_t)];

    qtest_memread(qts, addr, bytes, sizeof(bytes));
    return ldl_le_p(bytes);
}

static uint64_t read_le64_from_ram(QTestState *qts, uint64_t addr)
{
    uint8_t bytes[sizeof(uint64_t)];

    qtest_memread(qts, addr, bytes, sizeof(bytes));
    return ldq_le_p(bytes);
}

static void lsi_start_script(QTestState *qts, uint32_t script_addr)
{
    uint64_t mmio = DIRECT_CPU_BASE;

    /* DSP is four byte registers; writing the high byte starts SCRIPTS. */
    qtest_writeb(qts, mmio + LSI_DSP, script_addr & 0xff);
    qtest_writeb(qts, mmio + LSI_DSP + 1, (script_addr >> 8) & 0xff);
    qtest_writeb(qts, mmio + LSI_DSP + 2, (script_addr >> 16) & 0xff);
    qtest_writeb(qts, mmio + LSI_DSP + 3, script_addr >> 24);
}

static void lsi_wait_script_interrupt(QTestState *qts)
{
    uint8_t dstat = 0;
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        if (qtest_readb(qts, DIRECT_CPU_BASE + LSI_ISTAT0) &
            LSI_ISTAT0_DIP) {
            dstat = qtest_readb(qts, DIRECT_CPU_BASE + LSI_DSTAT);
            if (dstat & LSI_DSTAT_SIR) {
                break;
            }
        }
        g_usleep(1000);
    }
    g_assert_cmphex(dstat & LSI_DSTAT_SIR, ==, LSI_DSTAT_SIR);
}

static void setup_lsi_iommu_memory_move(QTestState *qts, bool valid_pte,
                                        const uint8_t *source,
                                        const uint8_t *sentinel)
{
    uint64_t pte = IOMMU_TARGET_ADDR;

    configure_lsi_direct_target(qts);
    enable_direct0_mapping(qts, 0);

    write_le32_to_ram(qts, IOMMU_SCRIPT_ADDR,
                      LSI_SCRIPT_MEMMOVE | IOMMU_COPY_SIZE);
    write_le32_to_ram(qts, IOMMU_SCRIPT_ADDR + 4, IOMMU_SOURCE_ADDR);
    write_le32_to_ram(qts, IOMMU_SCRIPT_ADDR + 8, IOMMU_IOVA);
    write_le32_to_ram(qts, IOMMU_SCRIPT_ADDR + 12,
                      LSI_SCRIPT_INTERRUPT);
    write_le32_to_ram(qts, IOMMU_SCRIPT_ADDR + 16, 0);
    qtest_memwrite(qts, IOMMU_SOURCE_ADDR, source, IOMMU_COPY_SIZE);
    qtest_memwrite(qts, IOMMU_TARGET_ADDR, sentinel, IOMMU_COPY_SIZE);
    qtest_memwrite(qts, IOMMU_IOVA, sentinel, IOMMU_COPY_SIZE);

    if (valid_pte) {
        pte |= UINT64_C(0x8000000000000000);
    }
    write_le64_to_ram(qts, IOMMU_PTE_ADDR, pte);

    /* Astro uses a 4 KiB absolute IOVA index into the little-endian PDIR. */
    astro_writeq(qts, ASTRO_BASE + ASTRO_TLB_PDIR_BASE, IOMMU_PDIR_BASE);
    astro_writeq(qts, ASTRO_BASE + ASTRO_TLB_IMASK, IOMMU_IMASK);
    astro_writeq(qts, ASTRO_BASE + ASTRO_TLB_IBASE, IOMMU_IBASE);
}

static void enable_lsi_sir_delivery(QTestState *qts)
{
    /* Slot 1 maps to Elroy input 1.  Vector 63 sets CPU0 EIRR bit zero. */
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_LOW);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_WINDOW, 63);
    qtest_writeb(qts, DIRECT_CPU_BASE + LSI_DIEN, LSI_DSTAT_SIR);
}

static void test_iommu_valid_pte_dma(void)
{
    const uint8_t source[IOMMU_COPY_SIZE] = {
        0x73, 0x79, 0x6e, 0x74, 0x68, 0x65, 0x74, 0x69,
        0x63, 0x2d, 0x73, 0x62, 0x61, 0x2d, 0x64, 0x6d,
        0x61, 0x2d, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x2d,
        0x70, 0x74, 0x65, 0x2d, 0xa5, 0x5a, 0xc3, 0x3c,
    };
    const uint8_t sentinel[IOMMU_COPY_SIZE] = { [0 ... 31] = 0xcc };
    uint8_t translated[IOMMU_COPY_SIZE];
    uint8_t identity[IOMMU_COPY_SIZE];
    QTestState *qts = astro_start(NULL);

    setup_lsi_iommu_memory_move(qts, true, source, sentinel);
    astro_writeq(qts, ASTRO_BASE + ASTRO_TLB_PCOM,
                 IOMMU_IOVA | 12);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_TLB_PCOM), ==,
                    IOMMU_IOVA | 12);
    g_assert_cmphex(qtest_readl(qts, CPU0_EIRR), ==, 0);
    enable_lsi_sir_delivery(qts);
    lsi_start_script(qts, IOMMU_SCRIPT_ADDR);
    lsi_wait_script_interrupt(qts);

    qtest_memread(qts, IOMMU_TARGET_ADDR, translated, sizeof(translated));
    qtest_memread(qts, IOMMU_IOVA, identity, sizeof(identity));
    g_assert_cmpmem(translated, sizeof(translated), source, sizeof(source));
    g_assert_cmpmem(identity, sizeof(identity), sentinel, sizeof(sentinel));
    g_assert_cmphex(qtest_readl(qts, CPU0_EIRR), ==, 1);

    qtest_quit(qts);
}

static void test_io_sapic_registers_and_reset(void)
{
    QTestState *qts = astro_start(NULL);

    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT, 1);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    ELROY_IO_SAPIC_VERSION);

    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_LOW);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_WINDOW, 63);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_SELECT), ==,
                    ELROY_IO_SAPIC_RTE1_LOW);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    63);

    qtest_system_reset(qts);
    /* Elroy reset preserves select and restores every RTE. */
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_SELECT), ==,
                    ELROY_IO_SAPIC_RTE1_LOW);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    ELROY_IO_SAPIC_RTE1_RESET);

    qtest_quit(qts);
}

static void test_iommu_invalid_pte_blocks_dma(void)
{
    const uint8_t source[IOMMU_COPY_SIZE] = { [0 ... 31] = 0x5a };
    const uint8_t sentinel[IOMMU_COPY_SIZE] = { [0 ... 31] = 0xcc };
    uint8_t translated[IOMMU_COPY_SIZE];
    uint8_t identity[IOMMU_COPY_SIZE];
    QTestState *qts = astro_start(NULL);

    setup_lsi_iommu_memory_move(qts, false, source, sentinel);
    lsi_start_script(qts, IOMMU_SCRIPT_ADDR);
    lsi_wait_script_interrupt(qts);

    qtest_memread(qts, IOMMU_TARGET_ADDR, translated, sizeof(translated));
    qtest_memread(qts, IOMMU_IOVA, identity, sizeof(identity));
    g_assert_cmpmem(translated, sizeof(translated), sentinel,
                    sizeof(sentinel));
    g_assert_cmpmem(identity, sizeof(identity), sentinel, sizeof(sentinel));

    qtest_quit(qts);
}

static void test_direct_mapping_route_and_reset(void)
{
    QTestState *qts = astro_start(NULL);

    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==, 0);
    configure_lsi_direct_target(qts);
    enable_direct0_mapping(qts, 0);
    assert_direct0_mapping(qts, 0);

    qtest_writeb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0, DIRECT_SIGNATURE);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==,
                    DIRECT_SIGNATURE);

    /* Elroy1 has no PCI BAR at the routed address. */
    astro_writeq(qts, ASTRO_BASE + ASTRO_DIRECT0_ROUTE, 1);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==, 0);
    astro_writeq(qts, ASTRO_BASE + ASTRO_DIRECT0_ROUTE, 0);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==,
                    DIRECT_SIGNATURE);

    qtest_system_reset(qts);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_DIRECT0_BASE), ==, 0);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_DIRECT0_SIZE), ==, 0);
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_DIRECT0_ROUTE), ==, 0);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==, 0);

    qtest_quit(qts);
}

static void test_config_selector_data_and_reset(void)
{
    QTestState *qts = astro_start(NULL);

    /* The top byte is echoed by Elroy but ignored by PCI lookup. */
    astro_writel(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==,
                    TEST_PCI_SELECTOR_TAG);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    TEST_LSI_ID);

    /* The full selector is echoed, while PCI lookup uses its low 32 bits. */
    astro_writeq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG64);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==,
                    TEST_PCI_SELECTOR_TAG64);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    TEST_LSI_ID);

    qtest_system_reset(qts);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==, 0);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    UINT32_MAX);

    qtest_quit(qts);
}

static void test_register_boundaries(void)
{
    QTestState *qts = astro_start(NULL);

    /* 0x3d8 is the exclusive end of ioc_ranges, not the next field. */
    g_assert_cmphex(astro_readq(qts, ASTRO_BASE + ASTRO_IOC_RANGE_END), ==,
                    0);

    /* Offsets 0x28 and 0x30 are not PCI configuration ports. */
    g_assert_cmphex(astro_readq(qts, EMPTY_ELROY_BASE + 0x28), ==, 0);
    g_assert_cmphex(astro_readq(qts, EMPTY_ELROY_BASE + 0x30), ==, 0);
    g_assert_cmphex(astro_readq(qts,
                               EMPTY_ELROY_BASE + ELROY_CONFIG_ADDR), ==,
                    UINT64_MAX);
    g_assert_cmphex(astro_readq(qts,
                               EMPTY_ELROY_BASE + ELROY_CONFIG_DATA), ==,
                    UINT64_MAX);

    qtest_quit(qts);
}

static void test_config_selector_migration(void)
{
    const uint8_t source[IOMMU_COPY_SIZE] = {
        0x73, 0x61, 0x76, 0x65, 0x76, 0x6d, 0x2d, 0x74,
        0x72, 0x61, 0x6e, 0x73, 0x6c, 0x61, 0x74, 0x65,
        0x64, 0x2d, 0x64, 0x6d, 0x61, 0x2d, 0x72, 0x65,
        0x73, 0x74, 0x6f, 0x72, 0x65, 0xa5, 0x5a, 0xc3,
    };
    const uint8_t sentinel[IOMMU_COPY_SIZE] = { [0 ... 31] = 0xcc };
    const uint16_t command = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    uint8_t restored[IOMMU_COPY_SIZE];
    uint8_t identity[IOMMU_COPY_SIZE];
    uint32_t cpu0_rte_high;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for Elroy migration testing");
        return;
    }

    tmpdir = g_dir_make_tmp("hppa-astro-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);
    qts = astro_start(args);

    astro_writeq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG64);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==,
                    TEST_PCI_SELECTOR_TAG64);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    TEST_LSI_ID);
    set_custom_distributed_mappings(qts);
    assert_custom_distributed_mappings(qts);
    setup_lsi_iommu_memory_move(qts, true, source, sentinel);
    qtest_writeb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0, DIRECT_SIGNATURE);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==,
                    DIRECT_SIGNATURE);
    g_assert_cmphex(qtest_readl(qts, CPU0_EIRR), ==, 0);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_HIGH);
    cpu0_rte_high = astro_readl(qts,
                                ELROY0_BASE + ELROY_IO_SAPIC_WINDOW);
    enable_lsi_sir_delivery(qts);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_HIGH);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_WINDOW,
                 UINT32_C(0x12340000));
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_LOW);
    /* Keep the selector migration assertion independent of BAR setup. */
    astro_writeq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR,
                 TEST_PCI_SELECTOR_TAG64);
    response = qtest_hmp(qts, "savevm elroy-selector");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qtest_system_reset(qts);
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==, 0);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    UINT32_MAX);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_SELECT), ==,
                    ELROY_IO_SAPIC_RTE1_LOW);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    ELROY_IO_SAPIC_RTE1_RESET);
    assert_default_distributed_mappings(qts);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==, 0);

    /* Overwrite DMA inputs and outputs after reset before loading saved state. */
    write_le64_to_ram(qts, IOMMU_PTE_ADDR, 0);
    qtest_memset(qts, IOMMU_SCRIPT_ADDR, 0x11, IOMMU_SCRIPT_SIZE);
    qtest_memset(qts, IOMMU_SOURCE_ADDR, 0x22, IOMMU_COPY_SIZE);
    qtest_memset(qts, IOMMU_TARGET_ADDR, 0x33, IOMMU_COPY_SIZE);
    qtest_memset(qts, IOMMU_IOVA, 0x44, IOMMU_COPY_SIZE);

    response = qtest_hmp(qts, "loadvm elroy-selector");
    g_assert_cmpstr(response, ==, "");

    /* Both the visible selector and the selector used by data must restore. */
    g_assert_cmphex(astro_readq(qts, ELROY0_BASE + ELROY_CONFIG_ADDR), ==,
                    TEST_PCI_SELECTOR_TAG64);
    g_assert_cmphex(astro_readl(qts, ELROY0_BASE + ELROY_CONFIG_DATA), ==,
                    TEST_LSI_ID);
    /*
     * Verify restored IBASE, IMASK, and PDIR state through translated DMA;
     * these registers lack readback.
     */
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_SELECT), ==,
                    ELROY_IO_SAPIC_RTE1_LOW);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    63);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_HIGH);
    g_assert_cmphex(astro_readl(qts,
                               ELROY0_BASE + ELROY_IO_SAPIC_WINDOW), ==,
                    UINT32_C(0x12340000));
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_WINDOW, cpu0_rte_high);
    astro_writel(qts, ELROY0_BASE + ELROY_IO_SAPIC_SELECT,
                 ELROY_IO_SAPIC_RTE1_LOW);
    assert_custom_distributed_mappings(qts);
    assert_direct0_mapping(qts, 0);
    g_assert_cmphex(qtest_readb(qts, DIRECT_CPU_BASE + LSI_SCRATCHB0), ==,
                    DIRECT_SIGNATURE);
    g_assert_cmphex(lsi_config_readl(qts, PCI_BASE_ADDRESS_1), ==,
                    DIRECT_PCI_BASE);
    g_assert_cmphex(lsi_config_readl(qts, PCI_COMMAND) & command, ==,
                    command);

    g_assert_cmphex(read_le64_from_ram(qts, IOMMU_PTE_ADDR), ==,
                    UINT64_C(0x8000000000000000) | IOMMU_TARGET_ADDR);
    g_assert_cmphex(read_le32_from_ram(qts, IOMMU_SCRIPT_ADDR), ==,
                    LSI_SCRIPT_MEMMOVE | IOMMU_COPY_SIZE);
    g_assert_cmphex(read_le32_from_ram(qts, IOMMU_SCRIPT_ADDR + 4), ==,
                    IOMMU_SOURCE_ADDR);
    g_assert_cmphex(read_le32_from_ram(qts, IOMMU_SCRIPT_ADDR + 8), ==,
                    IOMMU_IOVA);
    g_assert_cmphex(read_le32_from_ram(qts, IOMMU_SCRIPT_ADDR + 12), ==,
                    LSI_SCRIPT_INTERRUPT);
    g_assert_cmphex(read_le32_from_ram(qts, IOMMU_SCRIPT_ADDR + 16), ==, 0);
    qtest_memread(qts, IOMMU_SOURCE_ADDR, restored, sizeof(restored));
    g_assert_cmpmem(restored, sizeof(restored), source, sizeof(source));
    qtest_memread(qts, IOMMU_TARGET_ADDR, restored, sizeof(restored));
    g_assert_cmpmem(restored, sizeof(restored), sentinel, sizeof(sentinel));
    qtest_memread(qts, IOMMU_IOVA, identity, sizeof(identity));
    g_assert_cmpmem(identity, sizeof(identity), sentinel, sizeof(sentinel));

    g_assert_cmphex(qtest_readl(qts, CPU0_EIRR), ==, 0);
    lsi_start_script(qts, IOMMU_SCRIPT_ADDR);
    lsi_wait_script_interrupt(qts);
    qtest_memread(qts, IOMMU_TARGET_ADDR, restored, sizeof(restored));
    qtest_memread(qts, IOMMU_IOVA, identity, sizeof(identity));
    g_assert_cmpmem(restored, sizeof(restored), source, sizeof(source));
    g_assert_cmpmem(identity, sizeof(identity), sentinel, sizeof(sentinel));
    g_assert_cmphex(qtest_readl(qts, CPU0_EIRR), ==, 1);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/hppa/astro/reset-defaults", test_reset_defaults);
    qtest_add_func("/hppa/astro/config-selector-data-reset",
                   test_config_selector_data_and_reset);
    qtest_add_func("/hppa/astro/distributed-mappings-reset",
                   test_distributed_mappings_and_reset);
    qtest_add_func("/hppa/astro/direct-mapping-route-reset",
                   test_direct_mapping_route_and_reset);
    qtest_add_func("/hppa/astro/iommu-valid-pte-dma",
                   test_iommu_valid_pte_dma);
    qtest_add_func("/hppa/astro/iommu-invalid-pte-blocks-dma",
                   test_iommu_invalid_pte_blocks_dma);
    qtest_add_func("/hppa/astro/io-sapic-registers-reset",
                   test_io_sapic_registers_and_reset);
    qtest_add_func("/hppa/astro/register-boundaries",
                   test_register_boundaries);
    qtest_add_func("/hppa/astro/config-selector-migration",
                   test_config_selector_migration);

    return g_test_run();
}
