/*
 * HP Local Bus Adapter PCI configuration selector helpers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_HP_LBA_CONFIG_H
#define HW_PCI_HOST_HP_LBA_CONFIG_H

/*
 * The 64-bit selector is guest-visible; effective PCI selection is its low
 * 32 bits.  Access lanes use little-endian byte offsets within the selector.
 */
bool hp_lba_config_selector_access_valid(uint64_t offset, unsigned int size);
bool hp_lba_config_selector_read(uint64_t selector, uint64_t offset,
                                 unsigned int size, uint64_t *value);
bool hp_lba_config_selector_write(uint64_t *selector, uint32_t *effective,
                                  uint64_t offset, unsigned int size,
                                  uint64_t value);
void hp_lba_config_selector_sync(uint64_t selector, uint32_t *effective);
uint32_t hp_lba_config_data_address(uint32_t effective, uint64_t offset);

#endif
