/*
 * LPC47B27x ISA integration
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ISA_LPC47B27_H
#define HW_ISA_LPC47B27_H

#include "qom/object.h"

/* Composes serial and i8042 devices around the configuration-register core. */
#define TYPE_LPC47B27_ISA "lpc47b27-isa"
OBJECT_DECLARE_SIMPLE_TYPE(LPC47B27ISAState, LPC47B27_ISA)

/* Two-port configuration pair; UART input clock is divided by 16. */
#define LPC47B27_ISA_PROP_CONFIG_IOBASE         "config-iobase"
#define LPC47B27_ISA_PROP_UART_INPUT_CLOCK_HZ   "uart-input-clock-hz"

#endif /* HW_ISA_LPC47B27_H */
