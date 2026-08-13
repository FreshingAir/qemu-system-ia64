/*
 * IA-64 Advanced Load Address Table architecture operations.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "system/physmem.h"
#include "arch/arch.h"

#define IA64_ROTATING_FR_BASE 32

static void ia64_alat_invalidate_entry(CPUIA64State *env,
                                       IA64AlatEntry *entry)
{
    if (!entry->valid) {
        return;
    }

    entry->valid = false;
    if (env->alat_state.alat_active_count > 0) {
        env->alat_state.alat_active_count--;
    }
}

static void ia64_alat_clear(CPUIA64State *env)
{
    int i;

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        env->alat_state.alat[i].valid = false;
    }
    env->alat_state.alat_active_count = 0;
}

/*
 * CPU-originated stores use precise physical-overlap invalidation.  DMA and
 * other external agents write RAM through the system memory APIs, which bump
 * a shared generation.  IA-64 permits an implementation to discard any ALAT
 * entry at any time, so conservatively failing all local entries on such a
 * write is architecturally valid and keeps the table coherent with DMA.
 */
static bool ia64_alat_sync_memory_writes(CPUIA64State *env)
{
    uint64_t generation = physical_memory_write_generation();

    if (generation != env->alat_state.memory_write_generation) {
        ia64_alat_clear(env);
        env->alat_state.memory_write_generation = generation;
        return true;
    }
    return false;
}

void ia64_invalidate_alat_reg_range(CPUIA64State *env,
                                    uint32_t first, uint32_t last,
                                    bool fp)
{
    uint32_t i;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            env->alat_state.alat[i].fp == fp &&
            env->alat_state.alat[i].reg >= first &&
            env->alat_state.alat[i].reg < last) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
    }
}

static bool ia64_ranges_overlap(uint64_t start, uint64_t size,
                                uint64_t other_start, uint64_t other_size)
{
    uint64_t end;
    uint64_t other_end;

    if (size == 0 || other_size == 0) {
        return false;
    }

    end = start + size - 1;
    if (end < start) {
        end = UINT64_MAX;
    }
    other_end = other_start + other_size - 1;
    if (other_end < other_start) {
        other_end = UINT64_MAX;
    }

    return start <= other_end && other_start <= end;
}

void ia64_invalidate_alat_phys_range(CPUIA64State *env,
                                     uint64_t pa, uint64_t size)
{
    uint32_t i;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }
    if (ia64_alat_sync_memory_writes(env)) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            ia64_ranges_overlap(pa, size, env->alat_state.alat[i].phys_addr,
                                env->alat_state.alat[i].size)) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
    }
}

void ia64_invalidate_stacked_alat(CPUIA64State *env)
{
    ia64_invalidate_alat_reg_range(env, IA64_STACKED_GR_BASE, IA64_GR_COUNT,
                                   false);
}

void ia64_invalidate_rotating_fp_alat(CPUIA64State *env)
{
    ia64_invalidate_alat_reg_range(env, IA64_ROTATING_FR_BASE, IA64_FR_COUNT,
                                   true);
}

/* ---- Advanced Load Address Table check ---- */

uint64_t ia64_alat_chk_a(CPUIA64State *env, uint64_t va, uint32_t reg)
{
    int i;

    if (env->alat_state.alat_active_count != 0 &&
        !ia64_alat_sync_memory_writes(env)) {
        for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
            if (env->alat_state.alat[i].valid &&
                env->alat_state.alat[i].reg == reg) {
                return 0;
            }
        }
    }
    env->cr_ifa = va;
    CPUState *cs = env_cpu(env);
    cs->exception_index = IA64_EXCP_GENERAL;
    cpu_loop_exit(cs);
    return 1;
}

void ia64_alat_invala(CPUIA64State *env)
{
    ia64_alat_clear(env);
    env->alat_state.memory_write_generation =
        physical_memory_write_generation();
}

uint64_t ia64_alat_load_begin(CPUIA64State *env)
{
    ia64_alat_sync_memory_writes(env);
    return env->alat_state.memory_write_generation;
}

static void ia64_set_alat(CPUIA64State *env, uint32_t reg, uint64_t addr,
                          uint32_t size, bool fp, uint64_t generation)
{
    uint64_t pa;
    IA64MemorySpeculation spec;
    int free_index = -1;
    int match_index = -1;
    int i;

    /*
     * The caller sampled generation immediately before reading memory.  If
     * an external agent wrote RAM before this allocation point, the loaded
     * value cannot safely receive an ALAT entry.
     */
    if (physical_memory_write_generation_changed(generation)) {
        ia64_alat_sync_memory_writes(env);
        return;
    }

    if (!ia64_data_address_to_phys_attr(env, addr, &pa, &spec) ||
        !ia64_memory_allows_advanced_load(spec)) {
        return;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (!env->alat_state.alat[i].valid) {
            if (free_index < 0) {
                free_index = i;
            }
        } else if (env->alat_state.alat[i].reg == reg &&
                   env->alat_state.alat[i].fp == fp) {
            if (match_index < 0) {
                match_index = i;
            } else {
                /* A register can name at most one ALAT entry. */
                ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
            }
        }
    }

    i = match_index >= 0 ? match_index : free_index;
    if (i < 0) {
        return;
    }
    if (!env->alat_state.alat[i].valid) {
        env->alat_state.alat_active_count++;
    }
    env->alat_state.alat[i].phys_addr = pa;
    env->alat_state.alat[i].size = size;
    env->alat_state.alat[i].reg = reg;
    env->alat_state.alat[i].fp = fp;
    env->alat_state.alat[i].valid = true;

    /* Close the window around address translation and entry publication. */
    if (physical_memory_write_generation_changed(generation)) {
        ia64_alat_sync_memory_writes(env);
    }
}

void ia64_alat_set(CPUIA64State *env, uint32_t reg, uint64_t addr,
                   uint32_t size, uint64_t generation)
{
    ia64_set_alat(env, reg, addr, size, false, generation);
}

void ia64_alat_set_fp(CPUIA64State *env, uint32_t reg, uint64_t addr,
                      uint32_t size, uint64_t generation)
{
    if (reg > 1) {
        ia64_set_alat(env, reg, addr, size, true, generation);
    }
}

static int ia64_find_alat_reg(CPUIA64State *env, uint32_t reg, bool fp)
{
    int i;

    if (env->alat_state.alat_active_count == 0) {
        return -1;
    }
    if (ia64_alat_sync_memory_writes(env)) {
        return -1;
    }

    for (i = 0; i < IA64_ALAT_ENTRIES; i++) {
        if (env->alat_state.alat[i].valid &&
            env->alat_state.alat[i].reg == reg &&
            env->alat_state.alat[i].fp == fp) {
            return i;
        }
    }

    return -1;
}

static bool ia64_alat_matches_addr(CPUIA64State *env,
                                   const IA64AlatEntry *entry,
                                   uint64_t addr, uint32_t size)
{
    uint64_t pa;

    if (entry->size != size) {
        return false;
    }
    if (!ia64_data_address_to_phys(env, addr, &pa)) {
        return false;
    }
    return entry->phys_addr == pa;
}

static uint64_t ia64_check_load_alat(CPUIA64State *env, uint32_t reg,
                                     bool fp, bool verify_addr,
                                     uint64_t addr, uint32_t size,
                                     uint32_t clear)
{
    int i;

    if (fp && reg <= 1) {
        return 0;
    }

    i = ia64_find_alat_reg(env, reg, fp);
    if (i < 0) {
        return 0;
    }
    if (verify_addr && !ia64_alat_matches_addr(env, &env->alat_state.alat[i],
                                               addr, size)) {
        if (clear) {
            ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
        }
        return 0;
    }
    if (clear) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
    return 1;
}

void ia64_alat_invalidate_reg(CPUIA64State *env, uint32_t reg)
{
    int i = ia64_find_alat_reg(env, reg, false);

    if (i >= 0) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
}

void ia64_alat_invalidate_fp_reg(CPUIA64State *env, uint32_t reg)
{
    int i = ia64_find_alat_reg(env, reg, true);

    if (i >= 0) {
        ia64_alat_invalidate_entry(env, &env->alat_state.alat[i]);
    }
}

uint64_t ia64_alat_check_load(CPUIA64State *env, uint32_t reg,
                                uint32_t clear)
{
    return ia64_check_load_alat(env, reg, false, false, 0, 0, clear);
}

uint64_t ia64_alat_check_load_addr(CPUIA64State *env, uint32_t reg,
                                     uint64_t addr, uint32_t size,
                                     uint32_t clear)
{
    return ia64_check_load_alat(env, reg, false, true, addr, size, clear);
}

uint64_t ia64_alat_check_load_fp(CPUIA64State *env, uint32_t reg,
                                   uint32_t clear)
{
    return ia64_check_load_alat(env, reg, true, false, 0, 0, clear);
}

uint64_t ia64_alat_check_load_fp_addr(CPUIA64State *env, uint32_t reg,
                                        uint64_t addr, uint32_t size,
                                        uint32_t clear)
{
    return ia64_check_load_alat(env, reg, true, true, addr, size, clear);
}
void ia64_invalidate_alat_store(CPUIA64State *env, uint64_t addr,
                                uint32_t size)
{
    uint64_t pa;

    if (env->alat_state.alat_active_count == 0) {
        return;
    }
    if (!ia64_data_address_to_phys(env, addr, &pa)) {
        return;
    }

    ia64_invalidate_alat_phys_range(env, pa, size);
}
