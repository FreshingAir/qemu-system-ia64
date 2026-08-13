/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Validation of IA-64 RSE state received through migration.
 */

#ifndef TARGET_IA64_RSE_MIGRATION_H
#define TARGET_IA64_RSE_MIGRATION_H

#include "cpu.h"

bool ia64_rse_migration_state_valid(const CPUIA64State *env,
                                    bool has_clean_partition);
bool ia64_firmware_debug_rse_migration_state_valid(
    const IA64FirmwareDebugRseState *state, bool has_clean_partition);

#endif
