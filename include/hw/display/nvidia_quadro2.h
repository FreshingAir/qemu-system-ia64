/*
 * NVIDIA Quadro2 Pro (NV15) display adapter
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DISPLAY_NVIDIA_QUADRO2_H
#define HW_DISPLAY_NVIDIA_QUADRO2_H

#include "qemu/units.h"
#include "qom/object.h"

#define TYPE_NVIDIA_QUADRO2 "nvidia-quadro2"
OBJECT_DECLARE_SIMPLE_TYPE(NVIDIAQuadro2State, NVIDIA_QUADRO2)

#define NVIDIA_QUADRO2_VENDOR_ID          0x10de
#define NVIDIA_QUADRO2_DEVICE_ID          0x0153
#define NVIDIA_QUADRO2_REVISION           0xa4
#define NVIDIA_QUADRO2_SUBSYSTEM_ID       0x006d

#define NVIDIA_QUADRO2_MMIO_SIZE          (16 * MiB)
#define NVIDIA_QUADRO2_FB_APERTURE_SIZE   (128 * MiB)

#endif /* HW_DISPLAY_NVIDIA_QUADRO2_H */
