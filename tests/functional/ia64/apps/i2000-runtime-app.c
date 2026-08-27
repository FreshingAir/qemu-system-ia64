/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define ACPI_FADT_SIGNATURE          0x50434146U
#define ACPI_SDT_HEADER_SIZE         36U
#define ACPI_FADT_FLAGS_OFFSET       112U
#define ACPI_FADT_RESET_GAS_OFFSET   116U
#define ACPI_FADT_RESET_VALUE_OFFSET 128U
#define ACPI_FADT_RESET_REG_SUPPORTED (1U << 10)
#define ACPI_GAS_SYSTEM_IO           1U
#define I8042_COMMAND_PORT            0x64U
#define I8042_RESET_COMMAND           0xfeU
#define EFI_RESET_COLD                0U

static UINT8 acpi20_guid[16] = IA64_GUID_ACPI20;

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static VOID *find_config_table(EFI_SYSTEM_TABLE *SystemTable,
                               const UINT8 *Guid)
{
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (ia64_bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                             Guid, 16)) {
            return (VOID *)(UINTN)
                SystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static BOOLEAN fadt_reset_valid(EFI_SYSTEM_TABLE *SystemTable)
{
    const UINT8 *rsdp = find_config_table(SystemTable, acpi20_guid);
    const UINT8 *xsdt;
    UINT32 length;
    UINTN count;
    UINTN i;

    if (rsdp == NULL) {
        return 0;
    }
    xsdt = (const UINT8 *)(UINTN)get_u64(rsdp + 24U);
    if (xsdt == NULL) {
        return 0;
    }
    length = get_u32(xsdt + 4U);
    if (length < ACPI_SDT_HEADER_SIZE || length > 4096U ||
        ((length - ACPI_SDT_HEADER_SIZE) & 7U) != 0) {
        return 0;
    }
    count = (length - ACPI_SDT_HEADER_SIZE) / 8U;
    for (i = 0; i < count; i++) {
        const UINT8 *fadt = (const UINT8 *)(UINTN)get_u64(
            xsdt + ACPI_SDT_HEADER_SIZE + i * 8U);
        const UINT8 *gas;

        if (fadt == NULL || get_u32(fadt) != ACPI_FADT_SIGNATURE) {
            continue;
        }
        if (get_u32(fadt + 4U) <= ACPI_FADT_RESET_VALUE_OFFSET) {
            return 0;
        }
        gas = fadt + ACPI_FADT_RESET_GAS_OFFSET;
        return (get_u32(fadt + ACPI_FADT_FLAGS_OFFSET) &
                ACPI_FADT_RESET_REG_SUPPORTED) != 0 &&
            gas[0] == ACPI_GAS_SYSTEM_IO && gas[1] == 8U &&
            gas[2] == 0U && gas[3] == 0U &&
            get_u64(gas + 4U) == I8042_COMMAND_PORT &&
            fadt[ACPI_FADT_RESET_VALUE_OFFSET] == I8042_RESET_COMMAND;
    }
    return 0;
}

static BOOLEAN time_valid(const EFI_TIME *Time)
{
    return Time->Year >= 2020U && Time->Year <= 9999U &&
           Time->Month >= 1U && Time->Month <= 12U &&
           Time->Day >= 1U && Time->Day <= 31U &&
           Time->Hour <= 23U && Time->Minute <= 59U &&
           Time->Second <= 59U && Time->Nanosecond < 1000000000U;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "i2000-runtime",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_RUNTIME_SERVICES *runtime = SystemTable->RuntimeServices;
    EFI_TIME_CAPABILITIES capabilities = { 0 };
    EFI_TIME before;
    EFI_TIME after;
    EFI_STATUS status;

    (void)ImageHandle;
    status = runtime->GetTime(&before, &capabilities);
    ia64_test_check(&context, "get-time",
                    status == EFI_SUCCESS && time_valid(&before) &&
                        capabilities.Resolution != 0 &&
                        runtime->GetTime(NULL, NULL) ==
                            EFI_INVALID_PARAMETER,
                    status, "rtc-read");

    status = runtime->SetTime(&before);
    ia64_test_check(&context, "set-time",
                    status == EFI_SUCCESS &&
                        runtime->GetTime(&after, NULL) == EFI_SUCCESS &&
                        time_valid(&after) &&
                        after.Year == before.Year &&
                        after.Month == before.Month &&
                        after.Day == before.Day &&
                        after.Hour == before.Hour &&
                        after.Minute == before.Minute,
                    status, "rtc-write");

    ia64_test_check(&context, "fadt-reset",
                    fadt_reset_valid(SystemTable), EFI_DEVICE_ERROR,
                    "system-io-8042-reset");

    ia64_test_done(&context);
    if (context.Failed != 0) {
        return EFI_DEVICE_ERROR;
    }
    runtime->ResetSystem(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL);
    return EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
