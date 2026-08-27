/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_ROOT_COUNT                3U
#define TEST_RESOURCE_DESCRIPTOR_SIZE 46U
#define TEST_DEVICE_PATH_NODE_SIZE    12U

typedef struct {
    UINT8 BusBase;
    UINT8 BusEnd;
    UINT64 IoBase;
    UINT64 IoSize;
    UINT64 MemoryBase;
    UINT64 MemorySize;
} TEST_ROOT_EXPECTED;

typedef struct {
    EFI_HANDLE Handle;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Io;
    UINT8 *DevicePath;
} TEST_ROOT;

static const TEST_ROOT_EXPECTED expected_roots[TEST_ROOT_COUNT] = {
    { 0x00, 0x1f, 0x0000, 0x4000, 0x90000000ULL, 0x10000000ULL },
    { 0x20, 0x3f, 0x4000, 0x4000, 0xa0000000ULL, 0x10000000ULL },
    { 0x40, 0x5f, 0x8000, 0x4000, 0xb0000000ULL, 0x10000000ULL },
};

static UINT8 pci_root_guid[16] = IA64_GUID_PCI_ROOT_IO;
static UINT8 pci_host_guid[16] =
    IA64_GUID_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;

static UINT16 test_get_u16(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 test_get_u32(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 test_get_u64(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT64)test_get_u32(p) |
           ((UINT64)test_get_u32(p + 4) << 32);
}

static BOOLEAN test_root_methods_valid(
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Root)
{
    return Root != NULL && Root->PollMem != NULL && Root->PollIo != NULL &&
           Root->Mem.Read != NULL && Root->Mem.Write != NULL &&
           Root->Io.Read != NULL && Root->Io.Write != NULL &&
           Root->Pci.Read != NULL && Root->Pci.Write != NULL &&
           Root->CopyMem != NULL && Root->Map != NULL &&
           Root->Unmap != NULL && Root->AllocateBuffer != NULL &&
           Root->FreeBuffer != NULL && Root->Flush != NULL &&
           Root->GetAttributes != NULL && Root->SetAttributes != NULL &&
           Root->Configuration != NULL;
}

static BOOLEAN test_host_methods_valid(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host)
{
    return Host != NULL && Host->NotifyPhase != NULL &&
           Host->GetNextRootBridge != NULL &&
           Host->GetAllocAttributes != NULL &&
           Host->StartBusEnumeration != NULL &&
           Host->SetBusNumbers != NULL && Host->SubmitResources != NULL &&
           Host->GetProposedResources != NULL &&
           Host->PreprocessController != NULL;
}

static BOOLEAN test_descriptor(const UINT8 *Descriptor, UINT8 Type,
                               UINT64 Granularity, UINT64 Minimum,
                               UINT64 Maximum, UINT64 Length)
{
    return Descriptor[0] == 0x8a &&
           test_get_u16(Descriptor + 1) == 0x2b &&
           Descriptor[3] == Type && Descriptor[4] == 0 &&
           Descriptor[5] == 0 &&
           test_get_u64(Descriptor + 6) == Granularity &&
           test_get_u64(Descriptor + 14) == Minimum &&
           test_get_u64(Descriptor + 22) == Maximum &&
           test_get_u64(Descriptor + 30) == 0 &&
           test_get_u64(Descriptor + 38) == Length;
}

static INTN test_root_index(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Root)
{
    const UINT8 *descriptor;
    const TEST_ROOT_EXPECTED *expected;
    VOID *configuration = NULL;
    UINT64 bus_base;
    UINTN index;

    if (Root->Configuration(Root, &configuration) != EFI_SUCCESS ||
        configuration == NULL) {
        return -1;
    }
    descriptor = configuration;
    bus_base = test_get_u64(descriptor + 14);
    for (index = 0; index < TEST_ROOT_COUNT; index++) {
        if (expected_roots[index].BusBase == bus_base) {
            break;
        }
    }
    if (index == TEST_ROOT_COUNT) {
        return -1;
    }
    expected = &expected_roots[index];
    if (!test_descriptor(descriptor, 2, 0, expected->BusBase,
                         expected->BusEnd,
                         (UINT64)expected->BusEnd - expected->BusBase + 1U)) {
        return -1;
    }
    descriptor += TEST_RESOURCE_DESCRIPTOR_SIZE;
    if (!test_descriptor(descriptor, 1, 0, expected->IoBase,
                         expected->IoBase + expected->IoSize - 1U,
                         expected->IoSize)) {
        return -1;
    }
    descriptor += TEST_RESOURCE_DESCRIPTOR_SIZE;
    if (!test_descriptor(descriptor, 0, 32, expected->MemoryBase,
                         expected->MemoryBase + expected->MemorySize - 1U,
                         expected->MemorySize)) {
        return -1;
    }
    descriptor += TEST_RESOURCE_DESCRIPTOR_SIZE;
    if (descriptor[0] != 0x79 || descriptor[1] != 0) {
        return -1;
    }
    return (INTN)index;
}

static BOOLEAN test_root_device_path(const TEST_ROOT *Root, UINTN Index)
{
    const UINT8 *path = Root->DevicePath;
    const UINT8 *end;

    if (path == NULL || path[0] != 0x02 || path[1] != 0x01 ||
        test_get_u16(path + 2) != TEST_DEVICE_PATH_NODE_SIZE ||
        test_get_u32(path + 4) != 0x0A0341D0 ||
        test_get_u32(path + 8) != Index) {
        return 0;
    }
    end = path + TEST_DEVICE_PATH_NODE_SIZE;
    return end[0] == 0x7f && end[1] == 0xff &&
           test_get_u16(end + 2) == 4;
}

static BOOLEAN test_handle_in_roots(const TEST_ROOT *Roots,
                                    EFI_HANDLE Handle, UINTN *Index)
{
    UINTN i;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (Roots[i].Handle == Handle) {
            if (Index != NULL) {
                *Index = i;
            }
            return 1;
        }
    }
    return 0;
}

static BOOLEAN test_protocols(EFI_SYSTEM_TABLE *SystemTable,
                              TEST_ROOT *Roots,
                              EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL
                                  **Host,
                              EFI_STATUS *Code, const char **Detail)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_HANDLE *host_handles = NULL;
    EFI_HANDLE *root_handles = NULL;
    UINTN host_count = 0;
    UINTN root_count = 0;
    UINT32 mask = 0;
    UINTN i;
    EFI_STATUS status;
    BOOLEAN valid = 0;

    status = bs->LocateProtocol(pci_host_guid, NULL, (VOID **)Host);
    if (status != EFI_SUCCESS || !test_host_methods_valid(*Host)) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-protocol";
        return 0;
    }
    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_host_guid,
                                    NULL, &host_count, &host_handles);
    if (status != EFI_SUCCESS || host_count != 1 || host_handles == NULL) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-handle";
        goto out;
    }
    status = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, pci_root_guid,
                                    NULL, &root_count, &root_handles);
    if (status != EFI_SUCCESS || root_count != TEST_ROOT_COUNT ||
        root_handles == NULL) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "root-handles";
        goto out;
    }
    for (i = 0; i < root_count; i++) {
        EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *io = NULL;
        UINT8 *device_path = NULL;
        INTN index;

        status = bs->HandleProtocol(root_handles[i], pci_root_guid,
                                    (VOID **)&io);
        if (status != EFI_SUCCESS || !test_root_methods_valid(io) ||
            io->ParentHandle != host_handles[0] || io->SegmentNumber != 0) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "root-protocol";
            goto out;
        }
        status = bs->HandleProtocol(root_handles[i], device_path_guid,
                                    (VOID **)&device_path);
        if (status != EFI_SUCCESS || device_path == NULL) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "root-device-path";
            goto out;
        }
        index = test_root_index(io);
        if (index < 0 || (mask & (1U << index)) != 0) {
            *Code = EFI_DEVICE_ERROR;
            *Detail = "root-resources";
            goto out;
        }
        Roots[index].Handle = root_handles[i];
        Roots[index].Io = io;
        Roots[index].DevicePath = device_path;
        mask |= 1U << index;
    }
    if (mask != (1U << TEST_ROOT_COUNT) - 1U) {
        *Code = EFI_DEVICE_ERROR;
        *Detail = "root-set";
        goto out;
    }
    valid = 1;

out:
    if (root_handles != NULL && bs->FreePool(root_handles) != EFI_SUCCESS) {
        valid = 0;
        *Code = EFI_DEVICE_ERROR;
        *Detail = "root-handle-free";
    }
    if (host_handles != NULL && bs->FreePool(host_handles) != EFI_SUCCESS) {
        valid = 0;
        *Code = EFI_DEVICE_ERROR;
        *Detail = "host-handle-free";
    }
    return valid;
}

static BOOLEAN test_device_paths(const TEST_ROOT *Roots,
                                 EFI_STATUS *Code, const char **Detail)
{
    UINTN i;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        if (!test_root_device_path(&Roots[i], i)) {
            *Code = EFI_DEVICE_ERROR;
            *Detail = "root-device-path";
            return 0;
        }
    }
    return 1;
}

static UINT64 test_pci_address(UINT8 Bus, UINT8 Device)
{
    return ((UINT64)Bus << 24) | ((UINT64)Device << 16);
}

static BOOLEAN test_config_access(const TEST_ROOT *Roots,
                                  EFI_STATUS *Code, const char **Detail)
{
    UINT32 rage_id = 0;
    UINT32 lsi_id = 0;
    UINT32 ignored = 0;
    EFI_STATUS status;

    status = Roots[0].Io->Pci.Read(
        Roots[0].Io, EfiPciWidthUint32, test_pci_address(0x00, 5),
        1, &rage_id);
    if (status != EFI_SUCCESS || rage_id != 0x50461002U) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "rage128-config";
        return 0;
    }
    status = Roots[1].Io->Pci.Read(
        Roots[1].Io, EfiPciWidthUint32, test_pci_address(0x20, 3),
        1, &lsi_id);
    if (status != EFI_SUCCESS || lsi_id != 0x00121000U) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "lsi-config";
        return 0;
    }
    status = Roots[0].Io->Pci.Read(
        Roots[0].Io, EfiPciWidthUint32, test_pci_address(0x20, 3),
        1, &ignored);
    if (status != EFI_INVALID_PARAMETER) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "root-isolation";
        return 0;
    }
    return 1;
}

static BOOLEAN test_host_enumeration(
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Host,
    const TEST_ROOT *Roots, EFI_STATUS *Code, const char **Detail)
{
    EFI_HANDLE handle = NULL;
    UINT32 mask = 0;
    UINTN i;
    EFI_STATUS status;

    for (i = 0; i < TEST_ROOT_COUNT; i++) {
        UINTN index;

        status = Host->GetNextRootBridge(Host, &handle);
        if (status != EFI_SUCCESS ||
            !test_handle_in_roots(Roots, handle, &index) ||
            (mask & (1U << index)) != 0) {
            *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
            *Detail = "host-root-enumeration";
            return 0;
        }
        mask |= 1U << index;
    }
    status = Host->GetNextRootBridge(Host, &handle);
    if (status != EFI_NOT_FOUND ||
        mask != (1U << TEST_ROOT_COUNT) - 1U) {
        *Code = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        *Detail = "host-root-end";
        return 0;
    }
    return 1;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT output = {
        .SystemTable = SystemTable,
        .Suite = "i2000-pci-root",
    };
    TEST_ROOT roots[TEST_ROOT_COUNT] = { 0 };
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *host = NULL;
    EFI_STATUS code = EFI_DEVICE_ERROR;
    const char *detail = "system-table";
    BOOLEAN table_valid;
    BOOLEAN protocols_valid = 0;
    BOOLEAN paths_valid = 0;
    BOOLEAN config_valid = 0;
    BOOLEAN enumeration_valid = 0;

    (void)ImageHandle;
    table_valid = SystemTable != NULL && SystemTable->BootServices != NULL &&
        SystemTable->BootServices->LocateProtocol != NULL &&
        SystemTable->BootServices->LocateHandleBuffer != NULL &&
        SystemTable->BootServices->HandleProtocol != NULL &&
        SystemTable->BootServices->FreePool != NULL;
    ia64_test_check(&output, "system-table", table_valid, code, detail);

    if (table_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        protocols_valid = test_protocols(SystemTable, roots, &host,
                                         &code, &detail);
    }
    ia64_test_check(&output, "protocols", protocols_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        paths_valid = test_device_paths(roots, &code, &detail);
    }
    ia64_test_check(&output, "device-paths", paths_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        config_valid = test_config_access(roots, &code, &detail);
    }
    ia64_test_check(&output, "config-access",
                    config_valid, code, detail);

    code = EFI_NOT_READY;
    detail = "protocol-prerequisite";
    if (protocols_valid) {
        code = EFI_SUCCESS;
        detail = "ok";
        enumeration_valid = test_host_enumeration(
            host, roots, &code, &detail);
    }
    ia64_test_check(&output, "host-enumeration",
                    enumeration_valid, code, detail);

    ia64_test_done(&output);
    return output.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
