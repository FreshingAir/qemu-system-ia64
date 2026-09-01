/*
 * Broadcom BCM5701/BCM5704 PCI Ethernet controllers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_BCM5704_H
#define HW_NET_BCM5704_H

#include "qom/object.h"

#define TYPE_BCM57XX "bcm57xx"
#define TYPE_BCM5701 "bcm5701"
#define TYPE_BCM5704 "bcm5704"
OBJECT_DECLARE_SIMPLE_TYPE(BCM57xxState, BCM57XX)

#define BCM57XX_PCI_VENDOR_ID             0x14e4
#define BCM57XX_MMIO_SIZE                 0x00010000U
#define BCM57XX_ROM_SIZE                  0x00020000U

#define BCM5701_PCI_DEVICE_ID             0x1645
#define BCM5701_PCI_REVISION              0x15
#define BCM5701_PCI_SUBSYSTEM_VENDOR_ID   0x103c
#define BCM5701_PCI_SUBSYSTEM_ID          0x12a4

#define BCM5704_PCI_VENDOR_ID             BCM57XX_PCI_VENDOR_ID
#define BCM5704_PCI_DEVICE_ID             0x1648
#define BCM5704_PCI_REVISION              0x10
#define BCM5704_PCI_SUBSYSTEM_VENDOR_ID   0x14e4
#define BCM5704_PCI_SUBSYSTEM_ID          0x1644
#define BCM5704_MMIO_SIZE                 BCM57XX_MMIO_SIZE

#endif /* HW_NET_BCM5704_H */
