/*
 * QTest testcase for USB OHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/usb.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QOHCI_PCI QOHCI_PCI;

struct QOHCI_PCI {
    QOSGraphObject obj;
    QPCIDevice dev;
};

#define OHCI_CONTROL          0x04
#define OHCI_INTR_STATUS      0x0c
#define OHCI_RH_PORT_STATUS_1 0x54
#define OHCI_USB_RESUME       0x40
#define OHCI_USB_SUSPEND      0xc0
#define OHCI_INTR_RHSC        (1U << 6)
#define OHCI_PORT_CCS         (1U << 0)
#define OHCI_PORT_PES         (1U << 1)
#define OHCI_PORT_PSS         (1U << 2)
#define OHCI_PORT_POCI        (1U << 3)
#define OHCI_PORT_PRS         (1U << 4)
#define OHCI_PORT_CSC         (1U << 16)
#define OHCI_PORT_PSSC        (1U << 18)
#define OHCI_PORT_PRSC        (1U << 20)
#define OHCI_RESUME_SIGNAL_NS (20 * NANOSECONDS_PER_SECOND / 1000)
#define OHCI_RESUME_EOP_NS    (3 * NANOSECONDS_PER_SECOND / 1500000)
#define OHCI_RESUME_RECOVERY_NS (3 * NANOSECONDS_PER_SECOND / 1000)
#define OHCI_RESUME_SAVE_NS   (7 * NANOSECONDS_PER_SECOND / 1000)

static QOHCI_PCI *resume_ohci;
static QPCIBar resume_bar;

typedef struct OHCITestCase {
    void (*check)(void);
} OHCITestCase;

typedef struct OHCISnapshotData {
    char *tmpdir;
    char *disk_path;
} OHCISnapshotData;

static void ohci_snapshot_data_free(void *opaque)
{
    OHCISnapshotData *snapshot = opaque;

    g_assert_cmpint(g_unlink(snapshot->disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(snapshot->tmpdir), ==, 0);
    g_free(snapshot->disk_path);
    g_free(snapshot->tmpdir);
    g_free(snapshot);
}

static void *ohci_snapshot_setup(GString *cmd_line, void *arg)
{
    g_autofree char *quoted_disk_path = NULL;
    g_autoptr(GError) error = NULL;
    OHCISnapshotData *snapshot;

    if (!have_qemu_img()) {
        return NULL;
    }

    snapshot = g_new0(OHCISnapshotData, 1);
    snapshot->tmpdir = g_dir_make_tmp("ohci-resume-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(snapshot->tmpdir);
    snapshot->disk_path = g_build_filename(snapshot->tmpdir,
                                            "snapshot.qcow2", NULL);
    g_assert_true(mkimg(snapshot->disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(snapshot->disk_path);
    g_string_append_printf(cmd_line,
                           " -drive file=%s,format=qcow2,if=none,id=snapshot",
                           quoted_disk_path);
    g_test_queue_destroy(ohci_snapshot_data_free, snapshot);
    return snapshot;
}

static void test_ohci_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    usb_test_hotplug(global_qtest, "ohci", "1", NULL);
}

static uint32_t ohci_port_status(QPCIDevice *dev)
{
    return qpci_io_readl(dev, resume_bar, OHCI_RH_PORT_STATUS_1);
}

static void ohci_clear_rhsc(QPCIDevice *dev)
{
    qpci_io_writel(dev, resume_bar, OHCI_INTR_STATUS, OHCI_INTR_RHSC);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, 0);
}

static void ohci_prepare_connected_port(QPCIDevice *dev)
{
    uint32_t status = ohci_port_status(dev);

    g_assert_cmphex(status & OHCI_PORT_CCS, ==, OHCI_PORT_CCS);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1,
                   OHCI_PORT_CSC | OHCI_PORT_PES);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_CSC, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    ohci_clear_rhsc(dev);
}

static void check_ohci_port_resume(void)
{
    QPCIDevice *dev = &resume_ohci->dev;
    uint32_t status;

    ohci_prepare_connected_port(dev);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, OHCI_INTR_RHSC);
    ohci_clear_rhsc(dev);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, 0);

    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);

    qtest_clock_step(global_qtest,
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS - 1);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);

    qtest_clock_step(global_qtest, 1);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, OHCI_PORT_PSSC);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, OHCI_INTR_RHSC);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSSC);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
}

static void check_ohci_controller_resume(void)
{
    QPCIDevice *dev = &resume_ohci->dev;
    uint32_t status;

    ohci_prepare_connected_port(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSS, ==,
                    OHCI_PORT_PSS);
    ohci_clear_rhsc(dev);

    qpci_io_writel(dev, resume_bar, OHCI_CONTROL, OHCI_USB_SUSPEND);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    qpci_io_writel(dev, resume_bar, OHCI_CONTROL, OHCI_USB_RESUME);

    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_CONTROL), ==,
                    OHCI_USB_RESUME);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, 0);

    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS +
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSSC, ==, 0);
}

static void check_ohci_reset_suspended_port(void)
{
    QPCIDevice *dev = &resume_ohci->dev;
    uint32_t status;

    ohci_prepare_connected_port(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSS, ==,
                    OHCI_PORT_PSS);
    ohci_clear_rhsc(dev);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PRS);

    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PRSC, ==, OHCI_PORT_PRSC);
    g_assert_cmphex(qpci_io_readl(dev, resume_bar, OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, OHCI_INTR_RHSC);

    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS +
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSSC, ==, 0);
}

static void check_ohci_unplug_during_resume(void)
{
    QPCIDevice *dev = &resume_ohci->dev;

    ohci_prepare_connected_port(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    ohci_clear_rhsc(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSS, ==,
                    OHCI_PORT_PSS);
}

static void test_ohci_resume_case(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QOHCI_PCI *ohci = obj;
    OHCITestCase *test = data;

    qpci_device_enable(&ohci->dev);
    resume_ohci = ohci;
    resume_bar = qpci_iomap(&ohci->dev, 0, NULL);
    usb_test_hotplug(global_qtest, "ohci", "1", test->check);
    g_assert_cmphex(ohci_port_status(&ohci->dev) &
                    (OHCI_PORT_CCS | OHCI_PORT_PES | OHCI_PORT_PSS), ==, 0);
    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS +
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(ohci_port_status(&ohci->dev) & OHCI_PORT_PSSC, ==, 0);
    qpci_iounmap(&ohci->dev, resume_bar);
    resume_ohci = NULL;
}

static void test_ohci_resume_savevm(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QOHCI_PCI *ohci = obj;
    QPCIDevice *dev = &ohci->dev;
    g_autofree char *response = NULL;
    uint32_t status;

    if (!data) {
        g_test_skip("qemu-img is required for resume savevm testing");
        return;
    }

    qpci_device_enable(dev);
    resume_ohci = ohci;
    resume_bar = qpci_iomap(dev, 0, NULL);
    qtest_qmp_device_add(global_qtest, "usb-tablet", "usbdev1",
                         "{'port': '1', 'bus': 'ohci.0'}");
    ohci_prepare_connected_port(dev);

    response = qtest_hmp(global_qtest, "savevm resume-idle");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    ohci_clear_rhsc(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    qtest_clock_step(global_qtest, OHCI_RESUME_SAVE_NS);

    response = qtest_hmp(global_qtest, "loadvm resume-idle");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS +
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSSC, ==, 0);

    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_PSS);
    ohci_clear_rhsc(dev);
    qpci_io_writel(dev, resume_bar, OHCI_RH_PORT_STATUS_1, OHCI_PORT_POCI);
    qtest_clock_step(global_qtest, OHCI_RESUME_SAVE_NS);

    response = qtest_hmp(global_qtest, "savevm resume-pending");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qtest_clock_step(global_qtest, OHCI_RESUME_SIGNAL_NS +
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(ohci_port_status(dev) & OHCI_PORT_PSSC, ==,
                    OHCI_PORT_PSSC);

    response = qtest_hmp(global_qtest, "loadvm resume-pending");
    g_assert_cmpstr(response, ==, "");
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);

    qtest_clock_step(global_qtest,
                     OHCI_RESUME_SIGNAL_NS + OHCI_RESUME_EOP_NS +
                     OHCI_RESUME_RECOVERY_NS - OHCI_RESUME_SAVE_NS - 1);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    qtest_clock_step(global_qtest, 1);
    status = ohci_port_status(dev);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, OHCI_PORT_PSSC);

    qtest_qmp_device_del(global_qtest, "usbdev1");
    qpci_iounmap(dev, resume_bar);
    resume_ohci = NULL;
}

static void *ohci_pci_get_driver(void *obj, const char *interface)
{
    QOHCI_PCI *ohci_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ohci_pci->dev;
    }

    fprintf(stderr, "%s not present in pci-ohci\n", interface);
    g_assert_not_reached();
}

static void *ohci_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QOHCI_PCI *ohci_pci = g_new0(QOHCI_PCI, 1);

    qpci_device_init(&ohci_pci->dev, pci_bus, addr);
    ohci_pci->obj.get_driver = ohci_pci_get_driver;

    return &ohci_pci->obj;
}

static void ohci_pci_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=ohci",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("pci-ohci", ohci_pci_create);
    qos_node_consumes("pci-ohci", "pci-bus", &opts);
    qos_node_produces("pci-ohci", "pci-device");
}

libqos_init(ohci_pci_register_nodes);

static void register_ohci_pci_test(void)
{
    static OHCITestCase port_resume = {
        .check = check_ohci_port_resume,
    };
    static OHCITestCase controller_resume = {
        .check = check_ohci_controller_resume,
    };
    static OHCITestCase reset_suspended_port = {
        .check = check_ohci_reset_suspended_port,
    };
    static OHCITestCase unplug_during_resume = {
        .check = check_ohci_unplug_during_resume,
    };
    static QOSGraphTestOptions port_resume_opts = {
        .arg = &port_resume,
    };
    static QOSGraphTestOptions controller_resume_opts = {
        .arg = &controller_resume,
    };
    static QOSGraphTestOptions reset_suspended_port_opts = {
        .arg = &reset_suspended_port,
    };
    static QOSGraphTestOptions unplug_during_resume_opts = {
        .arg = &unplug_during_resume,
    };
    static QOSGraphTestOptions resume_savevm_opts = {
        .before = ohci_snapshot_setup,
    };

    qos_add_test("ohci_pci-test-hotplug", "pci-ohci", test_ohci_hotplug, NULL);
    qos_add_test("ohci_pci-test-port-resume", "pci-ohci",
                 test_ohci_resume_case, &port_resume_opts);
    qos_add_test("ohci_pci-test-controller-resume", "pci-ohci",
                 test_ohci_resume_case, &controller_resume_opts);
    qos_add_test("ohci_pci-test-reset-suspended-port", "pci-ohci",
                 test_ohci_resume_case, &reset_suspended_port_opts);
    qos_add_test("ohci_pci-test-unplug-during-resume", "pci-ohci",
                 test_ohci_resume_case, &unplug_during_resume_opts);
    qos_add_test("ohci_pci-test-resume-savevm", "pci-ohci",
                 test_ohci_resume_savevm, &resume_savevm_opts);
}

libqos_init(register_ohci_pci_test);
