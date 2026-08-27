/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define ATI_RAGE128_ID        0x50461002U
#define ATI_RV100_ID          0x51591002U
#define I2000_FRAMEBUFFER     0x0000000090000000ULL
#define ZX6000_FRAMEBUFFER    0x00000000a0000000ULL
#define ZX6000_ACPI_PM_BASE   0x00000000ff5c0000ULL
#define ZX6000_ACPI_PM_SIZE   0x0000000000002000ULL
#define ZX6000_NVRAM_BASE     0x00000000feb00000ULL
#define ZX6000_NVRAM_SIZE     0x0000000000080000ULL
#define ZX6000_RTC_BASE       0x00000000feb80000ULL
#define ZX6000_RTC_SIZE       0x0000000000002000ULL
#define ZX6000_CONTROL_BASE   0x00000000feb82000ULL
#define ZX6000_CONTROL_SIZE   0x0000000000002000ULL
#define ACPI_HID_PNP0A03      0x0a0341d0U
#define ACPI_HID_HWP0002      0x000222f0U
#define VGA_LEGACY_BASE       0x00000000000a0000ULL
#define VGA_LEGACY_SIZE       0x0000000000020000ULL
#define PCI_COMMAND_OFFSET    0x04U
#define PCI_COMMAND_MASTER    0x0004U
#define VBE_INDEX_PORT        0x01ceU
#define VBE_DATA_PORT         0x01d0U
#define VBE_INDEX_XRES        0x0001U
#define VBE_INDEX_YRES        0x0002U
#define VBE_INDEX_BPP         0x0003U
#define VBE_INDEX_ENABLE      0x0004U
#define VBE_ENABLED           0x0001U
#define GRAPHICS_PROTOCOLS    6U

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} __attribute__((packed)) GRAPHICS_DEVICE_PATH_NODE;

typedef struct {
    GRAPHICS_DEVICE_PATH_NODE Acpi;
    UINT32 Hid;
    UINT32 Uid;
    GRAPHICS_DEVICE_PATH_NODE Pci;
    UINT8 Function;
    UINT8 Device;
    GRAPHICS_DEVICE_PATH_NODE End;
} __attribute__((packed)) GRAPHICS_DEVICE_PATH;

static UINT8 gop_guid[16] = IA64_GUID_GOP;
static UINT8 uga_guid[16] = IA64_GUID_UGA_DRAW;
static UINT8 uga_io_guid[16] = IA64_GUID_UGA_IO;
static UINT8 pci_io_guid[16] = IA64_GUID_PCI_IO;
static UINT8 device_path_guid[16] = IA64_GUID_DEVICE_PATH;
static UINT8 text_output_guid[16] = IA64_GUID_TEXT_OUTPUT;
static UINT8 pci_dma_buffer[64] __attribute__((aligned(64)));

static UINT32 framebuffer_read(UINT64 Address);

static BOOLEAN guid_equal(const VOID *Left, const VOID *Right)
{
    const UINT8 *left = (const UINT8 *)Left;
    const UINT8 *right = (const UINT8 *)Right;
    UINTN i;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (i = 0; i < 16U; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static BOOLEAN graphics_protocol_list_valid(EFI_BOOT_SERVICES *BootServices,
                                            EFI_HANDLE Handle,
                                            EFI_STATUS *Result)
{
    VOID *expected[GRAPHICS_PROTOCOLS] = {
        text_output_guid,
        gop_guid,
        uga_guid,
        uga_io_guid,
        device_path_guid,
        pci_io_guid,
    };
    BOOLEAN found[GRAPHICS_PROTOCOLS] = { 0 };
    VOID **protocols = NULL;
    UINTN count = 0;
    UINTN i;
    EFI_STATUS status;
    BOOLEAN valid = 1;

    if (BootServices->ProtocolsPerHandle == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }
    status = BootServices->ProtocolsPerHandle(Handle, &protocols, &count);
    if (status != EFI_SUCCESS || protocols == NULL ||
        count != GRAPHICS_PROTOCOLS) {
        valid = 0;
    }
    for (i = 0; valid && i < count; i++) {
        VOID *interface = NULL;
        UINTN expected_index;

        status = BootServices->HandleProtocol(
            Handle, protocols[i], &interface);
        if (status != EFI_SUCCESS || interface == NULL) {
            valid = 0;
            break;
        }
        for (expected_index = 0; expected_index < GRAPHICS_PROTOCOLS;
             expected_index++) {
            if (guid_equal(protocols[i], expected[expected_index])) {
                break;
            }
        }
        if (expected_index == GRAPHICS_PROTOCOLS || found[expected_index]) {
            valid = 0;
            break;
        }
        found[expected_index] = 1;
    }
    for (i = 0; valid && i < GRAPHICS_PROTOCOLS; i++) {
        if (!found[i]) {
            valid = 0;
        }
    }
    if (protocols != NULL && BootServices->FreePool(protocols) != EFI_SUCCESS) {
        valid = 0;
    }
    *Result = valid ? EFI_SUCCESS :
        (status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status);
    return valid;
}

static BOOLEAN graphics_pci_dma_valid(EFI_PCI_IO_PROTOCOL *Pci,
                                      EFI_STATUS *Result)
{
    EFI_PHYSICAL_ADDRESS device_address = 0;
    VOID *mapping = NULL;
    VOID *allocation = NULL;
    UINTN bytes = sizeof(pci_dma_buffer);
    UINTN common_bytes = EFI_PAGE_SIZE;
    EFI_STATUS status;
    EFI_STATUS cleanup_status;

    if (Pci->Map == NULL || Pci->Unmap == NULL ||
        Pci->AllocateBuffer == NULL || Pci->FreeBuffer == NULL ||
        Pci->Flush == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }

    status = Pci->Map(Pci, EfiPciOperationBusMasterRead, pci_dma_buffer,
                      &bytes, &device_address, &mapping);
    if (status != EFI_SUCCESS || mapping == NULL ||
        bytes != sizeof(pci_dma_buffer) ||
        device_address != (EFI_PHYSICAL_ADDRESS)(UINTN)pci_dma_buffer) {
        if (mapping != NULL) {
            (void)Pci->Unmap(Pci, mapping);
        }
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    status = Pci->Unmap(Pci, mapping);
    mapping = NULL;
    if (status != EFI_SUCCESS) {
        *Result = status;
        return 0;
    }

    status = Pci->AllocateBuffer(Pci, AllocateAnyPages,
                                 EfiBootServicesData, 1, &allocation, 0);
    if (status != EFI_SUCCESS || allocation == NULL) {
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    device_address = 0;
    status = Pci->Map(Pci, EfiPciOperationBusMasterCommonBuffer,
                      allocation, &common_bytes, &device_address, &mapping);
    if (status != EFI_SUCCESS || mapping == NULL ||
        common_bytes != EFI_PAGE_SIZE ||
        device_address != (EFI_PHYSICAL_ADDRESS)(UINTN)allocation) {
        if (mapping != NULL) {
            (void)Pci->Unmap(Pci, mapping);
        }
        (void)Pci->FreeBuffer(Pci, 1, allocation);
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    status = Pci->Unmap(Pci, mapping);
    mapping = NULL;
    if (status == EFI_SUCCESS) {
        status = Pci->Flush(Pci);
    }
    cleanup_status = Pci->FreeBuffer(Pci, 1, allocation);
    if (status == EFI_SUCCESS) {
        status = cleanup_status;
    }
    *Result = status;
    return status == EFI_SUCCESS;
}

static BOOLEAN graphics_pci_attributes_valid(EFI_PCI_IO_PROTOCOL *Pci,
                                             EFI_STATUS *Result)
{
    UINT64 supported = 0;
    UINT64 attributes = 0;
    UINT16 original_command = 0;
    UINT16 command = 0;
    UINT16 restored_command = 0;
    EFI_STATUS status;
    EFI_STATUS restore_status;
    BOOLEAN original_read = 0;
    BOOLEAN valid = 0;

    if (Pci->Attributes == NULL || Pci->Pci.Read == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }
    status = Pci->Pci.Read(Pci, EfiPciWidthUint16, PCI_COMMAND_OFFSET,
                           1, &original_command);
    if (status != EFI_SUCCESS) {
        *Result = status;
        return 0;
    }
    original_read = 1;
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationSupported,
                             0, &supported);
    if (status != EFI_SUCCESS ||
        (supported & EFI_PCI_ATTRIBUTE_BUS_MASTER) == 0) {
        goto out;
    }
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationGet,
                             0, &attributes);
    if (status != EFI_SUCCESS ||
        (attributes & EFI_PCI_ATTRIBUTE_BUS_MASTER) == 0 ||
        (original_command & PCI_COMMAND_MASTER) == 0) {
        goto out;
    }
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationDisable,
                             EFI_PCI_ATTRIBUTE_BUS_MASTER, NULL);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    status = Pci->Pci.Read(Pci, EfiPciWidthUint16, PCI_COMMAND_OFFSET,
                           1, &command);
    if (status != EFI_SUCCESS || (command & PCI_COMMAND_MASTER) != 0) {
        goto out;
    }
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationGet,
                             0, &attributes);
    if (status != EFI_SUCCESS ||
        (attributes & EFI_PCI_ATTRIBUTE_BUS_MASTER) != 0) {
        goto out;
    }
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationEnable,
                             EFI_PCI_ATTRIBUTE_BUS_MASTER, NULL);
    if (status != EFI_SUCCESS) {
        goto out;
    }
    status = Pci->Pci.Read(Pci, EfiPciWidthUint16, PCI_COMMAND_OFFSET,
                           1, &command);
    if (status != EFI_SUCCESS || (command & PCI_COMMAND_MASTER) == 0) {
        goto out;
    }
    status = Pci->Attributes(Pci, EfiPciIoAttributeOperationGet,
                             0, &attributes);
    valid = status == EFI_SUCCESS &&
        (attributes & EFI_PCI_ATTRIBUTE_BUS_MASTER) != 0;

out:
    if (original_read) {
        restore_status = Pci->Attributes(
            Pci, (original_command & PCI_COMMAND_MASTER) != 0 ?
                EfiPciIoAttributeOperationEnable :
                EfiPciIoAttributeOperationDisable,
            EFI_PCI_ATTRIBUTE_BUS_MASTER, NULL);
        if (restore_status == EFI_SUCCESS) {
            restore_status = Pci->Pci.Read(
                Pci, EfiPciWidthUint16, PCI_COMMAND_OFFSET,
                1, &restored_command);
        }
        if (restore_status != EFI_SUCCESS ||
            restored_command != original_command) {
            status = restore_status == EFI_SUCCESS ?
                EFI_DEVICE_ERROR : restore_status;
            valid = 0;
        }
    }
    if (!valid && status == EFI_SUCCESS) {
        status = EFI_DEVICE_ERROR;
    }
    *Result = status;
    return valid;
}

static BOOLEAN graphics_pci_bars_valid(EFI_BOOT_SERVICES *BootServices,
                                       EFI_PCI_IO_PROTOCOL *Pci,
                                       EFI_STATUS *Result)
{
    UINT32 io_value = 0;
    UINT32 memory_value = 0;
    UINT64 supports = 0;
    VOID *resources = NULL;
    EFI_STATUS status;

    if (Pci->Io.Read == NULL || Pci->Mem.Read == NULL ||
        Pci->GetBarAttributes == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }
    status = Pci->Io.Read(Pci, EfiPciWidthUint32, 1, 0, 1, &io_value);
    if (status == EFI_SUCCESS) {
        status = Pci->Mem.Read(Pci, EfiPciWidthUint32, 2, 0, 1,
                               &memory_value);
    }
    if (status != EFI_SUCCESS || io_value != memory_value) {
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    status = Pci->GetBarAttributes(Pci, 1, &supports, &resources);
    if (status != EFI_SUCCESS || supports != EFI_PCI_ATTRIBUTE_IO ||
        resources == NULL) {
        if (resources != NULL) {
            (void)BootServices->FreePool(resources);
        }
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    status = BootServices->FreePool(resources);
    resources = NULL;
    if (status != EFI_SUCCESS) {
        *Result = status;
        return 0;
    }
    supports = 0;
    status = Pci->GetBarAttributes(Pci, 2, &supports, &resources);
    if (status != EFI_SUCCESS || supports != EFI_PCI_ATTRIBUTE_MEMORY ||
        resources == NULL) {
        if (resources != NULL) {
            (void)BootServices->FreePool(resources);
        }
        *Result = status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
        return 0;
    }
    status = BootServices->FreePool(resources);
    *Result = status;
    return status == EFI_SUCCESS;
}

static EFI_STATUS vbe_register_read(EFI_PCI_IO_PROTOCOL *Pci,
                                    UINT16 Index, UINT16 *Value)
{
    EFI_STATUS status;

    status = Pci->Io.Write(Pci, EfiPciWidthUint16,
                           EFI_PCI_IO_PASS_THROUGH_BAR,
                           VBE_INDEX_PORT, 1, &Index);
    if (status != EFI_SUCCESS) {
        return status;
    }
    return Pci->Io.Read(Pci, EfiPciWidthUint16,
                        EFI_PCI_IO_PASS_THROUGH_BAR,
                        VBE_DATA_PORT, 1, Value);
}

static BOOLEAN graphics_vbe_mode_valid(EFI_PCI_IO_PROTOCOL *Pci,
                                       EFI_STATUS *Result)
{
    UINT16 xres = 0;
    UINT16 yres = 0;
    UINT16 bpp = 0;
    UINT16 enable = 0;
    EFI_STATUS status;

    if (Pci->Io.Write == NULL || Pci->Io.Read == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }
    status = vbe_register_read(Pci, VBE_INDEX_XRES, &xres);
    if (status == EFI_SUCCESS) {
        status = vbe_register_read(Pci, VBE_INDEX_YRES, &yres);
    }
    if (status == EFI_SUCCESS) {
        status = vbe_register_read(Pci, VBE_INDEX_BPP, &bpp);
    }
    if (status == EFI_SUCCESS) {
        status = vbe_register_read(Pci, VBE_INDEX_ENABLE, &enable);
    }
    *Result = status;
    return status == EFI_SUCCESS && xres == 800 && yres == 600 &&
        bpp == 32 && (enable & VBE_ENABLED) != 0;
}

static BOOLEAN graphics_uga_valid(EFI_UGA_DRAW_PROTOCOL *Uga,
                                  UINT64 Framebuffer, EFI_STATUS *Result)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel = { 0x33, 0x22, 0x11, 0 };
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 depth = 0;
    UINT32 refresh = 0;
    EFI_STATUS status;
    EFI_STATUS restore_status;
    BOOLEAN valid = 0;

    if (Uga->GetMode == NULL || Uga->SetMode == NULL || Uga->Blt == NULL) {
        *Result = EFI_UNSUPPORTED;
        return 0;
    }
    status = Uga->GetMode(Uga, &width, &height, &depth, &refresh);
    if (status != EFI_SUCCESS || width != 800 || height != 600 ||
        depth != 32 || refresh != 60) {
        goto restore;
    }
    status = Uga->SetMode(Uga, 1024, 768, 32, 60);
    if (status != EFI_SUCCESS) {
        goto restore;
    }
    status = Uga->GetMode(Uga, &width, &height, &depth, &refresh);
    if (status != EFI_SUCCESS || width != 1024 || height != 768 ||
        depth != 32 || refresh != 60) {
        goto restore;
    }
    status = Uga->Blt(Uga, &pixel, EfiBltVideoFill,
                      0, 0, 2, 0, 1, 1, 0);
    valid = status == EFI_SUCCESS &&
        framebuffer_read(Framebuffer + 8U) == 0x00112233U;

restore:
    restore_status = Uga->SetMode(Uga, 800, 600, 32, 60);
    if (restore_status == EFI_SUCCESS) {
        restore_status = Uga->GetMode(
            Uga, &width, &height, &depth, &refresh);
    }
    if (restore_status != EFI_SUCCESS || width != 800 || height != 600 ||
        depth != 32 || refresh != 60) {
        status = restore_status == EFI_SUCCESS ?
            EFI_DEVICE_ERROR : restore_status;
        valid = 0;
    }
    if (!valid && status == EFI_SUCCESS) {
        status = EFI_DEVICE_ERROR;
    }
    *Result = status;
    return valid;
}

static BOOLEAN memory_map_contains(EFI_MEMORY_DESCRIPTOR *Map,
                                   UINTN MapSize, UINTN DescriptorSize,
                                   UINT32 Type, UINT64 Start, UINT64 Size,
                                   UINT64 Attributes)
{
    UINTN offset;
    UINT64 end = Start + Size;

    if (Map == NULL || Size == 0 || end < Start ||
        DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return 0;
    }
    for (offset = 0; offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= MapSize;
         offset += DescriptorSize) {
        EFI_MEMORY_DESCRIPTOR *descriptor =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + offset);
        UINT64 descriptor_size = descriptor->NumberOfPages << 12;
        UINT64 descriptor_end = descriptor->PhysicalStart + descriptor_size;

        if (descriptor->Type == Type &&
            (descriptor->Attribute & Attributes) == Attributes &&
            descriptor_end >= descriptor->PhysicalStart &&
            Start >= descriptor->PhysicalStart && end <= descriptor_end) {
            return 1;
        }
    }
    return 0;
}

static UINT32 framebuffer_read(UINT64 Address)
{
    /* Framebuffer MMIO requires volatile CPU loads. */
    return *(volatile UINT32 *)(UINTN)Address;
}

static BOOLEAN graphics_memory_map_valid(EFI_SYSTEM_TABLE *SystemTable,
                                         UINT64 Framebuffer)
{
    EFI_BOOT_SERVICES *boot_services = SystemTable->BootServices;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS status;
    BOOLEAN valid;

    status = boot_services->GetMemoryMap(
        &map_size, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0) {
        return 0;
    }
    map_size += 4U * descriptor_size;
    if (boot_services->AllocatePool(EfiLoaderData, map_size,
                                    (VOID **)&map) != EFI_SUCCESS) {
        return 0;
    }
    status = boot_services->GetMemoryMap(
        &map_size, map, &map_key, &descriptor_size, &descriptor_version);
    valid = status == EFI_SUCCESS &&
        memory_map_contains(map, map_size, descriptor_size,
                            EfiMemoryMappedIO, VGA_LEGACY_BASE,
                            VGA_LEGACY_SIZE, EFI_MEMORY_UC) &&
        memory_map_contains(map, map_size, descriptor_size,
                            EfiMemoryMappedIO, Framebuffer,
                            EFI_PAGE_SIZE, EFI_MEMORY_UC) &&
        (Framebuffer != ZX6000_FRAMEBUFFER ||
         memory_map_contains(map, map_size, descriptor_size,
                             EfiMemoryMappedIO, ZX6000_ACPI_PM_BASE,
                             ZX6000_ACPI_PM_SIZE,
                             EFI_MEMORY_UC | EFI_MEMORY_RUNTIME)) &&
        (Framebuffer != ZX6000_FRAMEBUFFER ||
         memory_map_contains(map, map_size, descriptor_size,
                             EfiRuntimeServicesData, ZX6000_NVRAM_BASE,
                             ZX6000_NVRAM_SIZE,
                             EFI_MEMORY_UC | EFI_MEMORY_RUNTIME)) &&
        (Framebuffer != ZX6000_FRAMEBUFFER ||
         memory_map_contains(map, map_size, descriptor_size,
                             EfiRuntimeServicesData, ZX6000_RTC_BASE,
                             ZX6000_RTC_SIZE,
                             EFI_MEMORY_UC | EFI_MEMORY_RUNTIME)) &&
        (Framebuffer != ZX6000_FRAMEBUFFER ||
         memory_map_contains(map, map_size, descriptor_size,
                             EfiRuntimeServicesData, ZX6000_CONTROL_BASE,
                             ZX6000_CONTROL_SIZE,
                             EFI_MEMORY_UC | EFI_MEMORY_RUNTIME));
    (void)boot_services->FreePool(map);
    return valid;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "graphics",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    EFI_BOOT_SERVICES *boot_services = SystemTable->BootServices;
    EFI_HANDLE *handles = NULL;
    EFI_HANDLE graphics_handle = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_UGA_DRAW_PROTOCOL *uga = NULL;
    EFI_PCI_IO_PROTOCOL *pci = NULL;
    GRAPHICS_DEVICE_PATH *path = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
    UINTN info_size = 0;
    UINTN handle_count = 0;
    UINTN segment = ~(UINTN)0;
    UINTN bus = ~(UINTN)0;
    UINTN device = ~(UINTN)0;
    UINTN function = ~(UINTN)0;
    UINTN expected_bus = ~(UINTN)0;
    UINTN expected_device = ~(UINTN)0;
    UINT32 identifier = 0;
    UINT32 expected_hid = 0;
    UINT32 expected_uid = 0;
    UINT64 framebuffer = 0;
    UINT32 written = 0x00112233U;
    UINT32 readback = 0;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel = { 0x66, 0x55, 0x44, 0 };
    EFI_STATUS status;
    BOOLEAN protocols_ready;
    BOOLEAN operation_valid;

    (void)ImageHandle;
    status = boot_services->LocateHandleBuffer(
        EFI_LOCATE_BY_PROTOCOL, gop_guid, NULL, &handle_count, &handles);
    if (status == EFI_SUCCESS && handle_count == 1U) {
        graphics_handle = handles[0];
        status = boot_services->HandleProtocol(
            graphics_handle, gop_guid, (VOID **)&gop);
        if (status == EFI_SUCCESS) {
            status = boot_services->HandleProtocol(
                graphics_handle, uga_guid, (VOID **)&uga);
        }
        if (status == EFI_SUCCESS) {
            status = boot_services->HandleProtocol(
                graphics_handle, pci_io_guid, (VOID **)&pci);
        }
        if (status == EFI_SUCCESS) {
            status = boot_services->HandleProtocol(
                graphics_handle, device_path_guid, (VOID **)&path);
        }
    }
    protocols_ready = status == EFI_SUCCESS && gop != NULL && uga != NULL &&
        pci != NULL && path != NULL;

    ia64_test_check(&context, "protocols", protocols_ready,
                    status, "graphics-handle-protocols");

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready && graphics_protocol_list_valid(
        boot_services, graphics_handle, &status);
    ia64_test_check(&context, "protocol-list", operation_valid,
                    status, "protocols-per-handle");

    if (protocols_ready && pci->GetLocation != NULL && pci->Pci.Read != NULL &&
        pci->GetLocation(pci, &segment, &bus, &device, &function) ==
            EFI_SUCCESS &&
        pci->Pci.Read(pci, EfiPciWidthUint32, 0, 1, &identifier) ==
            EFI_SUCCESS) {
        if (identifier == ATI_RV100_ID) {
            framebuffer = ZX6000_FRAMEBUFFER;
            expected_bus = 0x80;
            expected_device = 0;
            expected_hid = ACPI_HID_HWP0002;
            expected_uid = 0x400;
        } else if (identifier == ATI_RAGE128_ID) {
            framebuffer = I2000_FRAMEBUFFER;
            expected_bus = 0;
            expected_device = 5;
            expected_hid = ACPI_HID_PNP0A03;
        }
    }
    ia64_test_check(
        &context, "pci-location",
        framebuffer != 0 && segment == 0 && bus == expected_bus &&
            device == expected_device && function == 0 &&
            pci->RomSize == 0 && pci->RomImage == NULL,
        EFI_DEVICE_ERROR, "bdf-id-rom");

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready &&
        graphics_pci_dma_valid(pci, &status);
    ia64_test_check(&context, "pci-dma", operation_valid,
                    status, "map-common-buffer-flush");

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready &&
        graphics_pci_attributes_valid(pci, &status);
    ia64_test_check(&context, "pci-attributes", operation_valid,
                    status, "bus-master-command");

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready &&
        graphics_pci_bars_valid(boot_services, pci, &status);
    ia64_test_check(&context, "pci-bars", operation_valid,
                    status, "io-bar1-memory-bar2");

    ia64_test_check(
        &context, "device-path",
        protocols_ready && path->Acpi.Type == 0x02 &&
            path->Acpi.SubType == 0x01 && path->Acpi.Length == 12 &&
            path->Hid == expected_hid && path->Uid == expected_uid &&
            path->Pci.Type == 0x01 && path->Pci.SubType == 0x01 &&
            path->Pci.Length == 6 && path->Function == 0 &&
            path->Device == expected_device && path->End.Type == 0x7f &&
            path->End.SubType == 0xff && path->End.Length == 4,
        EFI_DEVICE_ERROR, "acpi-pci-path");

    status = protocols_ready && gop->QueryMode != NULL ?
        gop->QueryMode(gop, 1, &info_size, &info) : EFI_NOT_FOUND;
    ia64_test_check(
        &context, "gop",
        status == EFI_SUCCESS && info != NULL &&
            info_size >= sizeof(*info) &&
            info->HorizontalResolution == 800 &&
            info->VerticalResolution == 600 &&
            gop->Mode != NULL && gop->Mode->MaxMode == 4 &&
            gop->Mode->FrameBufferBase == framebuffer &&
            gop->SetMode != NULL &&
            gop->SetMode(gop, 1) == EFI_SUCCESS &&
            gop->Mode->Mode == 1 &&
            gop->Mode->FrameBufferSize == 800U * 600U * 4U,
        status, "mode-framebuffer");
    if (info != NULL) {
        (void)boot_services->FreePool(info);
    }

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready &&
        graphics_vbe_mode_valid(pci, &status);
    ia64_test_check(&context, "vbe-mode", operation_valid,
                    status, "set-mode-registers");

    status = protocols_ready ? EFI_SUCCESS : EFI_NOT_FOUND;
    operation_valid = protocols_ready && framebuffer != 0 &&
        graphics_uga_valid(uga, framebuffer, &status);
    ia64_test_check(&context, "uga", operation_valid,
                    status, "set-mode-blt-restore");

    status = protocols_ready && framebuffer != 0 && pci->Mem.Write != NULL &&
             pci->Mem.Read != NULL && gop->Blt != NULL ?
        pci->Mem.Write(pci, EfiPciWidthUint32, 0, 0, 1, &written) :
        EFI_NOT_FOUND;
    if (status == EFI_SUCCESS) {
        status = pci->Mem.Read(pci, EfiPciWidthUint32, 0, 0, 1, &readback);
    }
    if (status == EFI_SUCCESS) {
        status = gop->Blt(gop, &pixel, EfiBltVideoFill,
                          0, 0, 1, 0, 1, 1, 0);
    }
    ia64_test_check(
        &context, "framebuffer-io",
        status == EFI_SUCCESS && readback == written &&
            framebuffer_read(framebuffer) == written &&
            framebuffer_read(framebuffer + 4U) == 0x00445566U,
        status, "pci-io-gop-blt");

    ia64_test_check(&context, "memory-map",
                    framebuffer != 0 &&
                        graphics_memory_map_valid(SystemTable, framebuffer),
                    EFI_DEVICE_ERROR, "vga-mmio-ranges");

    if (handles != NULL) {
        (void)boot_services->FreePool(handles);
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
