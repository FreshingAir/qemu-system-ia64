/*
 * QEMU physical memory interfaces (target independent).
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_SYSTEM_PHYSMEM_H
#define QEMU_SYSTEM_PHYSMEM_H

#include "exec/hwaddr.h"
#include "system/ramlist.h"

#define DIRTY_CLIENTS_ALL     ((1 << DIRTY_MEMORY_NUM) - 1)
#define DIRTY_CLIENTS_NOCODE  (DIRTY_CLIENTS_ALL & ~(1 << DIRTY_MEMORY_CODE))

bool physical_memory_get_dirty_flag(ram_addr_t addr, unsigned client);

bool physical_memory_is_clean(ram_addr_t addr);

/*
 * Monotonic notification for RAM writes performed through the system memory
 * APIs.  Readers use the generation to bracket a RAM read: the first sample
 * has acquire semantics, and changed() orders the preceding RAM read before
 * its closing sample.
 */
uint64_t physical_memory_write_generation(void);
bool physical_memory_write_generation_changed(uint64_t generation);

uint8_t physical_memory_range_includes_clean(ram_addr_t start,
                                             ram_addr_t length,
                                             uint8_t mask);

void physical_memory_set_dirty_flag(ram_addr_t addr, unsigned client);

void physical_memory_set_dirty_range(ram_addr_t start, ram_addr_t length,
                                     uint8_t mask);

/*
 * Contrary to physical_memory_sync_dirty_bitmap() this function returns
 * the number of dirty pages in @bitmap passed as argument. On the other hand,
 * physical_memory_sync_dirty_bitmap() returns newly dirtied pages that
 * weren't set in the global migration bitmap.
 */
uint64_t physical_memory_set_dirty_lebitmap(unsigned long *bitmap,
                                            ram_addr_t start,
                                            ram_addr_t pages);

void physical_memory_dirty_bits_cleared(ram_addr_t start, ram_addr_t length);

uint64_t physical_memory_test_and_clear_dirty(ram_addr_t start,
                                              ram_addr_t length,
                                              unsigned client,
                                              unsigned long *bmap);

DirtyBitmapSnapshot *
physical_memory_snapshot_and_clear_dirty(MemoryRegion *mr, hwaddr offset,
                                         hwaddr length, unsigned client);

bool physical_memory_snapshot_get_dirty(DirtyBitmapSnapshot *snap,
                                        ram_addr_t start,
                                        ram_addr_t length);
int ram_block_rebind(Error **errp);

#endif
