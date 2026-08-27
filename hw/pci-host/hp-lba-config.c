/*
 * HP Local Bus Adapter PCI configuration selector helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-lba-config.h"
#include "qemu/bitops.h"

bool hp_lba_config_selector_access_valid(uint64_t offset, unsigned int size)
{
    return (size == 4 && (offset == 0 || offset == 4)) ||
           (size == 8 && offset == 0);
}

bool hp_lba_config_selector_read(uint64_t selector, uint64_t offset,
                                 unsigned int size, uint64_t *value)
{
    if (!value || !hp_lba_config_selector_access_valid(offset, size)) {
        return false;
    }

    *value = extract64(selector, offset * 8, size * 8);
    return true;
}

bool hp_lba_config_selector_write(uint64_t *selector, uint32_t *effective,
                                  uint64_t offset, unsigned int size,
                                  uint64_t value)
{
    if (!selector || !effective ||
        !hp_lba_config_selector_access_valid(offset, size)) {
        return false;
    }

    *selector = deposit64(*selector, offset * 8, size * 8, value);
    hp_lba_config_selector_sync(*selector, effective);
    return true;
}

void hp_lba_config_selector_sync(uint64_t selector, uint32_t *effective)
{
    *effective = (uint32_t)selector;
}

uint32_t hp_lba_config_data_address(uint32_t effective, uint64_t offset)
{
    return effective | (uint32_t)(offset & 3);
}
