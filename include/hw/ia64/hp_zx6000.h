/*
 * HP zx6000 workstation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_HP_ZX6000_H
#define HW_IA64_HP_ZX6000_H

#include "hw/core/boards.h"

#define TYPE_HP_ZX6000_MACHINE MACHINE_TYPE_NAME("hp-zx6000")
OBJECT_DECLARE_SIMPLE_TYPE(HPZX6000MachineState, HP_ZX6000_MACHINE)

#define HP_ZX6000_PCI_ROOT_COUNT 6U

#define HP_ZX6000_MIO_BASE        UINT64_C(0x00000000fed00000)
#define HP_ZX6000_IOA_BASE        UINT64_C(0x00000000fed20000)
#define HP_ZX6000_IOA_STRIDE      UINT64_C(0x0000000000002000)

/* The physical IOA numbering reserves the fed2a000 CCSR slot. */
#define HP_ZX6000_IOA_ADDRESS(index)                              \
    (HP_ZX6000_IOA_BASE +                                        \
     ((index) == 5U ? 6U : (index)) * HP_ZX6000_IOA_STRIDE)

#endif /* HW_IA64_HP_ZX6000_H */
