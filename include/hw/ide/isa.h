/*
 * QEMU IDE Emulation: ISA Bus support.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HW_IDE_ISA_H
#define HW_IDE_ISA_H

#include "hw/ide/ide-bus.h"
#include "qom/object.h"

#define TYPE_ISA_IDE "isa-ide"
OBJECT_DECLARE_SIMPLE_TYPE(ISAIDEState, ISA_IDE)

/* Return the IDE child bus after the ISA IDE device has been realized. */
IDEBus *isa_ide_bus(ISAIDEState *s);

ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int irqnum,
                        DriveInfo *hd0, DriveInfo *hd1);

#endif
