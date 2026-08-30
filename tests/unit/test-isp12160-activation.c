/*
 * ISP12160 activation-token unit tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/scsi/isp12160.h"

static const uint8_t mailbox_activation[ISP12160_QEMU_ACTIVATION_BYTES] = {
    0x00, 0x00, 0x01, 0x00, 0x00, 0x10,
    0x51, 0x45, 0x4d, 0x55, 0x53, 0x30, 0x01, 0x00,
};

static const uint8_t queue_activation[ISP12160_QEMU_ACTIVATION_BYTES] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x10,
    0x51, 0x45, 0x4d, 0x55, 0x53, 0x31, 0x01, 0x00,
};

static const uint8_t scsi_activation[ISP12160_QEMU_ACTIVATION_BYTES] = {
    0x00, 0x02, 0x00, 0x00, 0x00, 0x10,
    0x51, 0x45, 0x4d, 0x55, 0x53, 0x32, 0x01, 0x00,
};

static void test_generate(void)
{
    uint8_t actual[ISP12160_QEMU_ACTIVATION_BYTES];

    g_assert_true(isp12160_qemu_activation_generate(
        ISP12160_VARIANT_MAILBOX, actual, sizeof(actual)));
    g_assert_cmpmem(actual, sizeof(actual), mailbox_activation,
                    sizeof(mailbox_activation));

    g_assert_true(isp12160_qemu_activation_generate(
        ISP12160_VARIANT_QUEUE, actual, sizeof(actual)));
    g_assert_cmpmem(actual, sizeof(actual), queue_activation,
                    sizeof(queue_activation));

    g_assert_true(isp12160_qemu_activation_generate(
        ISP12160_VARIANT_SCSI, actual, sizeof(actual)));
    g_assert_cmpmem(actual, sizeof(actual), scsi_activation,
                    sizeof(scsi_activation));
}

static void test_validation_and_separation(void)
{
    uint8_t mutated[ISP12160_QEMU_ACTIVATION_BYTES];
    unsigned int i;

    g_assert_true(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_MAILBOX, mailbox_activation, sizeof(mailbox_activation)));
    g_assert_true(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_QUEUE, queue_activation, sizeof(queue_activation)));
    g_assert_true(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_SCSI, scsi_activation, sizeof(scsi_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_MAILBOX, queue_activation, sizeof(queue_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_QUEUE, mailbox_activation, sizeof(mailbox_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_SCSI, mailbox_activation, sizeof(mailbox_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_SCSI, queue_activation, sizeof(queue_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_MAILBOX, scsi_activation, sizeof(scsi_activation)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_QUEUE, scsi_activation, sizeof(scsi_activation)));

    for (i = 0; i < sizeof(mutated); i++) {
        memcpy(mutated, queue_activation, sizeof(mutated));
        mutated[i] ^= 1;
        g_assert_false(isp12160_qemu_activation_validate(
            ISP12160_VARIANT_QUEUE, mutated, sizeof(mutated)));
    }
    for (i = 0; i < sizeof(mutated); i++) {
        memcpy(mutated, scsi_activation, sizeof(mutated));
        mutated[i] ^= 1;
        g_assert_false(isp12160_qemu_activation_validate(
            ISP12160_VARIANT_SCSI, mutated, sizeof(mutated)));
    }
}

static void test_invalid_arguments(void)
{
    uint8_t untouched[ISP12160_QEMU_ACTIVATION_BYTES];
    uint8_t expected[ISP12160_QEMU_ACTIVATION_BYTES];

    memset(untouched, 0xa5, sizeof(untouched));
    memcpy(expected, untouched, sizeof(expected));
    g_assert_false(isp12160_qemu_activation_generate(
        UINT16_MAX, untouched, sizeof(untouched)));
    g_assert_cmpmem(untouched, sizeof(untouched), expected,
                    sizeof(expected));
    g_assert_false(isp12160_qemu_activation_generate(
        ISP12160_VARIANT_QUEUE, untouched, sizeof(untouched) - 1));
    g_assert_false(isp12160_qemu_activation_generate(
        ISP12160_VARIANT_QUEUE, NULL, sizeof(untouched)));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_QUEUE, queue_activation, sizeof(queue_activation) - 1));
    g_assert_false(isp12160_qemu_activation_validate(
        ISP12160_VARIANT_QUEUE, NULL, sizeof(queue_activation)));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/isp12160-activation/generate", test_generate);
    g_test_add_func("/isp12160-activation/validation-separation",
                    test_validation_and_separation);
    g_test_add_func("/isp12160-activation/invalid-arguments",
                    test_invalid_arguments);

    return g_test_run();
}
