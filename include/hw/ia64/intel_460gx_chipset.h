/*
 * Intel 460GX chipset configuration targets
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_INTEL_460GX_CHIPSET_H
#define HW_IA64_INTEL_460GX_CHIPSET_H

#include "qemu/bitops.h"
#include "qom/object.h"

#define TYPE_INTEL_460GX_CHIPSET "intel-460gx-chipset"
OBJECT_DECLARE_SIMPLE_TYPE(Intel460GXChipsetState, INTEL_460GX_CHIPSET)

#define INTEL_460GX_CHIPSET_PROP_HOST          "host"
#define INTEL_460GX_CHIPSET_PROP_EXPANDER_MASK "expander-mask"

#define INTEL_460GX_CHIPSET_SAC_DEVICE          0x00
#define INTEL_460GX_CHIPSET_SAC_MEMORY_DEVICE   0x01
#define INTEL_460GX_CHIPSET_SDC_DEVICE          0x04
#define INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE 0x10

#define INTEL_460GX_CHIPSET_FIXED_PRESENT_MASK \
    (BIT(INTEL_460GX_CHIPSET_SAC_DEVICE) |      \
     BIT(INTEL_460GX_CHIPSET_SAC_MEMORY_DEVICE) | \
     BIT(INTEL_460GX_CHIPSET_SDC_DEVICE) |      \
     MAKE_64BIT_MASK(INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE, 8))

uint32_t intel_460gx_chipset_present_mask(uint8_t expander_mask);

#endif
