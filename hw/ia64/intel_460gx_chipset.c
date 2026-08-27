/*
 * Intel 460GX chipset configuration targets
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/ia64/intel_460gx_chipset.h"
#include "hw/ia64/intel_460gx_host.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define INTEL_460GX_CONFIG_SIZE 256
#define INTEL_460GX_VENDOR_ID   0x8086
#define INTEL_460GX_SAC_ID      0x84e0
#define INTEL_460GX_SDC_ID      0x84e1
#define INTEL_460GX_MAC_ID      0x84e3
#define INTEL_460GX_MDC_ID      0x84e4
#define INTEL_460GX_PXB_ID      0x84cb

typedef struct Intel460GXRegisterBlock {
    uint8_t config[INTEL_460GX_CONFIG_SIZE];
    uint8_t reset[INTEL_460GX_CONFIG_SIZE];
    uint8_t wmask[INTEL_460GX_CONFIG_SIZE];
    uint8_t w1cmask[INTEL_460GX_CONFIG_SIZE];
    uint8_t sticky[INTEL_460GX_CONFIG_SIZE];
    uint16_t coupled_w1c_a;
    uint16_t coupled_w1c_b;
    uint8_t coupled_w1c_size;
} Intel460GXRegisterBlock;

struct Intel460GXChipsetState {
    DeviceState parent_obj;

    Intel460GXHostState *host;
    uint8_t expander_mask;
    Intel460GXRegisterBlock sac[3];
    Intel460GXRegisterBlock sac_memory[2];
    Intel460GXRegisterBlock sdc;
    Intel460GXRegisterBlock memory_card[2][2];
    Intel460GXRegisterBlock expander[INTEL_460GX_DOWNSTREAM_PORTS];
};

static uint32_t register_block_read(void *opaque, uint16_t offset,
                                    unsigned size)
{
    Intel460GXRegisterBlock *block = opaque;
    uint32_t value = 0;
    unsigned i;

    if ((size != 1 && size != 2 && size != 4) ||
        offset > INTEL_460GX_CONFIG_SIZE - size) {
        return size == 4 ? UINT32_MAX : MAKE_64BIT_MASK(0, size * 8);
    }
    for (i = 0; i < size; i++) {
        value |= (uint32_t)block->config[offset + i] << (i * 8);
    }
    return value;
}

static void register_block_write(void *opaque, uint16_t offset,
                                 uint32_t value, unsigned size)
{
    Intel460GXRegisterBlock *block = opaque;
    unsigned i;

    if ((size != 1 && size != 2 && size != 4) ||
        offset > INTEL_460GX_CONFIG_SIZE - size) {
        return;
    }
    for (i = 0; i < size; i++) {
        unsigned index = offset + i;
        uint8_t byte = value >> (i * 8);
        uint8_t old = block->config[index];
        uint8_t clear = byte & block->w1cmask[index];

        old = (old & ~block->wmask[index]) |
              (byte & block->wmask[index]);
        old &= ~clear;
        block->config[index] = old;
        if (index >= block->coupled_w1c_a &&
            index < block->coupled_w1c_a + block->coupled_w1c_size) {
            block->config[block->coupled_w1c_b +
                          index - block->coupled_w1c_a] &= ~clear;
        } else if (index >= block->coupled_w1c_b &&
                   index < block->coupled_w1c_b +
                           block->coupled_w1c_size) {
            block->config[block->coupled_w1c_a +
                          index - block->coupled_w1c_b] &= ~clear;
        }
    }
}

static const Intel460GXConfigTargetOps register_block_ops = {
    .read = register_block_read,
    .write = register_block_write,
};

static void register_block_set_word(uint8_t *config, unsigned offset,
                                    uint16_t value)
{
    config[offset] = value;
    config[offset + 1] = value >> 8;
}

static void register_block_set_long(uint8_t *config, unsigned offset,
                                    uint32_t value)
{
    config[offset] = value;
    config[offset + 1] = value >> 8;
    config[offset + 2] = value >> 16;
    config[offset + 3] = value >> 24;
}

static void register_block_init(Intel460GXRegisterBlock *block,
                                uint16_t device_id, uint16_t class_id)
{
    memset(block, 0, sizeof(*block));
    register_block_set_word(block->reset, PCI_VENDOR_ID,
                            INTEL_460GX_VENDOR_ID);
    register_block_set_word(block->reset, PCI_DEVICE_ID, device_id);
    register_block_set_word(block->reset, PCI_STATUS,
                            PCI_STATUS_DEVSEL_MEDIUM);
    register_block_set_long(block->reset, PCI_REVISION_ID,
                            (uint32_t)class_id << 16);
    memcpy(block->config, block->reset, sizeof(block->config));
}

static void intel_460gx_chipset_init_registers(
    Intel460GXChipsetState *s)
{
    unsigned i;
    unsigned function;

    register_block_init(&s->sac[0], INTEL_460GX_SAC_ID,
                        PCI_CLASS_BRIDGE_HOST);
    register_block_init(&s->sac[1], INTEL_460GX_SAC_ID,
                        PCI_CLASS_BRIDGE_HOST);
    register_block_init(&s->sac[2], INTEL_460GX_SAC_ID,
                        PCI_CLASS_BRIDGE_HOST);
    s->sac[0].reset[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_MULTI_FUNCTION;
    s->sac[0].wmask[0x80] = BIT(7);
    s->sac[0].wmask[0x81] = BIT(7);
    s->sac[0].wmask[0x82] = BIT(7);
    s->sac[0].w1cmask[0x80] = BIT(6);
    s->sac[0].w1cmask[0x81] = BIT(6);
    s->sac[0].w1cmask[0x82] = BIT(6);
    memset(s->sac[0].sticky + 0x80, 0xff, 3);
    register_block_set_long(s->sac[1].w1cmask, 0x40,
                            UINT32_C(0xffff7fe1));
    register_block_set_long(s->sac[1].w1cmask, 0x44,
                            UINT32_C(0xffff7fe1));
    memset(s->sac[1].sticky + 0x40, 0xff, 8);
    s->sac[1].wmask[0x80] = 0x3f;

    /* SAC memory targets expose PCI identity only. */
    register_block_init(&s->sac_memory[0], INTEL_460GX_SAC_ID,
                        PCI_CLASS_BRIDGE_HOST);
    register_block_init(&s->sac_memory[1], INTEL_460GX_SAC_ID,
                        PCI_CLASS_BRIDGE_HOST);

    register_block_init(&s->sdc, INTEL_460GX_SDC_ID,
                        PCI_CLASS_MEMORY_OTHER);
    memset(s->sdc.w1cmask + 0x80, 0xff, 8);
    s->sdc.coupled_w1c_a = 0x80;
    s->sdc.coupled_w1c_b = 0x84;
    s->sdc.coupled_w1c_size = 4;
    memset(s->sdc.wmask + 0xc8, 0xff, 4);

    /* Memory-card targets expose PCI identity only. */
    for (i = 0; i < 2; i++) {
        register_block_init(&s->memory_card[i][0], INTEL_460GX_MAC_ID,
                            PCI_CLASS_MEMORY_RAM);
        register_block_init(&s->memory_card[i][1], INTEL_460GX_MDC_ID,
                            PCI_CLASS_MEMORY_RAM);
        s->memory_card[i][0].reset[PCI_HEADER_TYPE] =
            PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXRegisterBlock *block = &s->expander[i];

        register_block_init(block, INTEL_460GX_PXB_ID,
                            PCI_CLASS_BRIDGE_PCI);
        block->w1cmask[0x44] = 0x7b;
        block->sticky[0x44] = 0x7b;
        block->wmask[0x46] = 0x7d;
    }

    for (function = 0; function < G_N_ELEMENTS(s->sac); function++) {
        memcpy(s->sac[function].config, s->sac[function].reset,
               sizeof(s->sac[function].config));
    }
    for (function = 0; function < G_N_ELEMENTS(s->sac_memory); function++) {
        memcpy(s->sac_memory[function].config,
               s->sac_memory[function].reset,
               sizeof(s->sac_memory[function].config));
    }
    for (i = 0; i < 2; i++) {
        memcpy(s->memory_card[i][0].config,
               s->memory_card[i][0].reset,
               sizeof(s->memory_card[i][0].config));
    }
}

uint32_t intel_460gx_chipset_present_mask(uint8_t expander_mask)
{
    uint32_t present = INTEL_460GX_CHIPSET_FIXED_PRESENT_MASK;
    unsigned i;

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if (expander_mask & BIT(i)) {
            present |= BIT(INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE + i);
        }
    }
    return present;
}

static bool register_target(Intel460GXChipsetState *s, unsigned device,
                            unsigned function,
                            Intel460GXRegisterBlock *block, Error **errp)
{
    return intel_460gx_host_register_chipset_target(
        s->host, device, function, &register_block_ops, block, errp);
}

static void intel_460gx_chipset_realize(DeviceState *dev, Error **errp)
{
    Intel460GXChipsetState *s = INTEL_460GX_CHIPSET(dev);
    uint32_t present;
    unsigned i;
    unsigned function;

    if (!s->host) {
        error_setg(errp, "%s requires the '%s' link",
                   TYPE_INTEL_460GX_CHIPSET,
                   INTEL_460GX_CHIPSET_PROP_HOST);
        return;
    }

    intel_460gx_chipset_init_registers(s);
    present = intel_460gx_chipset_present_mask(s->expander_mask);
    if (!intel_460gx_host_configure_chipset_present(s->host, present, errp)) {
        return;
    }

    for (function = 0; function < G_N_ELEMENTS(s->sac); function++) {
        if (!intel_460gx_host_register_bootstrap_sac(
                s->host, function, &register_block_ops,
                &s->sac[function], errp) ||
            !register_target(s, INTEL_460GX_CHIPSET_SAC_DEVICE, function,
                             &s->sac[function], errp)) {
            return;
        }
    }
    for (function = 0; function < G_N_ELEMENTS(s->sac_memory); function++) {
        if (!register_target(s, INTEL_460GX_CHIPSET_SAC_MEMORY_DEVICE,
                             function + 2, &s->sac_memory[function], errp)) {
            return;
        }
    }
    if (!register_target(s, INTEL_460GX_CHIPSET_SDC_DEVICE, 0,
                         &s->sdc, errp)) {
        return;
    }
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            if (!register_target(
                    s, INTEL_460GX_CHIPSET_MEMORY_CARD_A + i, function,
                    &s->memory_card[i][function], errp)) {
                return;
            }
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if ((s->expander_mask & BIT(i)) &&
            !register_target(s,
                             INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE + i,
                             0, &s->expander[i], errp)) {
            return;
        }
    }
}

static void register_block_reset(Intel460GXRegisterBlock *block)
{
    unsigned i;

    for (i = 0; i < INTEL_460GX_CONFIG_SIZE; i++) {
        block->config[i] = (block->config[i] & block->sticky[i]) |
                           (block->reset[i] & ~block->sticky[i]);
    }
}

static void intel_460gx_chipset_reset(DeviceState *dev)
{
    Intel460GXChipsetState *s = INTEL_460GX_CHIPSET(dev);
    unsigned i;
    unsigned function;

    for (function = 0; function < G_N_ELEMENTS(s->sac); function++) {
        register_block_reset(&s->sac[function]);
    }
    for (function = 0; function < G_N_ELEMENTS(s->sac_memory); function++) {
        register_block_reset(&s->sac_memory[function]);
    }
    register_block_reset(&s->sdc);
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            register_block_reset(&s->memory_card[i][function]);
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        register_block_reset(&s->expander[i]);
    }
}

static bool register_block_post_load(void *opaque, int version_id,
                                     Error **errp)
{
    Intel460GXRegisterBlock *block = opaque;
    unsigned i;

    (void)version_id;
    for (i = 0; i < INTEL_460GX_CONFIG_SIZE; i++) {
        uint8_t mutable = block->wmask[i] | block->w1cmask[i] |
                          block->sticky[i];

        if ((block->config[i] ^ block->reset[i]) & ~mutable) {
            error_setg(errp,
                       "460GX configuration target has immutable bits set");
            return false;
        }
    }
    return true;
}

static const VMStateDescription vmstate_intel_460gx_register_block = {
    .name = TYPE_INTEL_460GX_CHIPSET "/register-block",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load_errp = register_block_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(config, Intel460GXRegisterBlock,
                            INTEL_460GX_CONFIG_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_intel_460gx_chipset = {
    .name = TYPE_INTEL_460GX_CHIPSET,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_EQUAL(expander_mask, Intel460GXChipsetState),
        VMSTATE_STRUCT_ARRAY(sac, Intel460GXChipsetState, 3, 1,
                             vmstate_intel_460gx_register_block,
                             Intel460GXRegisterBlock),
        VMSTATE_STRUCT_ARRAY(sac_memory, Intel460GXChipsetState, 2, 1,
                             vmstate_intel_460gx_register_block,
                             Intel460GXRegisterBlock),
        VMSTATE_STRUCT(sdc, Intel460GXChipsetState, 1,
                       vmstate_intel_460gx_register_block,
                       Intel460GXRegisterBlock),
        VMSTATE_STRUCT_2DARRAY(memory_card, Intel460GXChipsetState, 2, 2, 1,
                               vmstate_intel_460gx_register_block,
                               Intel460GXRegisterBlock),
        VMSTATE_STRUCT_ARRAY(expander, Intel460GXChipsetState,
                             INTEL_460GX_DOWNSTREAM_PORTS, 1,
                             vmstate_intel_460gx_register_block,
                             Intel460GXRegisterBlock),
        VMSTATE_END_OF_LIST()
    },
};

static const Property intel_460gx_chipset_properties[] = {
    DEFINE_PROP_LINK(INTEL_460GX_CHIPSET_PROP_HOST,
                     Intel460GXChipsetState, host,
                     TYPE_INTEL_460GX_HOST, Intel460GXHostState *),
    DEFINE_PROP_UINT8(INTEL_460GX_CHIPSET_PROP_EXPANDER_MASK,
                      Intel460GXChipsetState, expander_mask, 0),
};

static void intel_460gx_chipset_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Intel 460GX chipset configuration targets";
    dc->realize = intel_460gx_chipset_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_intel_460gx_chipset;
    device_class_set_legacy_reset(dc, intel_460gx_chipset_reset);
    device_class_set_props(dc, intel_460gx_chipset_properties);
}

static const TypeInfo intel_460gx_chipset_type_info = {
    .name = TYPE_INTEL_460GX_CHIPSET,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Intel460GXChipsetState),
    .class_init = intel_460gx_chipset_class_init,
};

static void intel_460gx_chipset_register_types(void)
{
    type_register_static(&intel_460gx_chipset_type_info);
}
type_init(intel_460gx_chipset_register_types)
