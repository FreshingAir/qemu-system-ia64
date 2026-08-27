#!/usr/bin/env python3
"""Firmware boot tests for the HP i2000 machine."""

# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path

from qemu_test import QemuSystemTest, wait_for_console_pattern

from ia64.efi_build import app_path
from ia64.media import make_el_torito_iso, make_fat_disk
from ia64.protocol import wait_for_suite


SMOKE_CASES = {
    "entry", "system-table", "loaded-image", "device-path",
    "root-device-path", "console-output",
}

GRAPHICS_CASES = {
    "protocols", "protocol-list", "pci-location", "pci-dma",
    "pci-attributes", "pci-bars", "device-path", "gop", "vbe-mode", "uga",
    "framebuffer-io", "memory-map",
}

LOADER_CASES = {
    "image-placement", "low-memory-map", "runtime-map",
    "firmware-aperture", "sal-entrypoint", "sal-memory-descriptors",
    "sal-call", "direct-alias", "automatic-allocation",
    "address-allocation", "acpi-topology",
}

PCI_ROOT_CASES = {
    "system-table", "protocols", "device-paths", "config-access",
    "host-enumeration",
}

RUNTIME_CASES = {"get-time", "set-time", "fadt-reset"}

SMP_MERCED_CASES = {
    "sal-ap-wake", "merced-rendezvous", "merced-rendezvous-return",
}


class HPI2000Boot(QemuSystemTest):
    def media_path(self, name: str) -> Path:
        configured = os.environ.get("IA64_TEST_MEDIA_DIR")
        if not configured:
            return Path(self.scratch_file(name))

        directory = Path(configured)
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"hp-i2000-{os.getpid()}-{name}"
        self.addCleanup(path.unlink, missing_ok=True)
        return path

    def test_firmware_ready(self):
        self.require_accelerator("tcg")
        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
        )
        vm.launch()

        output = wait_for_console_pattern(
            self, "Firmware ready.",
            failure_message="Invalid IA-64 platform descriptor", vm=vm,
        )
        self.assertIn(b"PCI Root Bridge I/O:  published", output)
        self.assertIn(b"PCI Host Bridge:       published", output)
        self.assertIn(b"ACPI MCFG (PCIe):     suppressed", output)
        self.assertIn(b"ACPI SSDT (CPU/PS2):    published", output)
        self.assertIn(b"SCSI controller:      LSI53C895A", output)
        self.assertIn(b"Console In:           Serial/PS2 ready", output)
        self.assertIn(b"NVRAM Variables:      enabled", output)
        self.assertIn(b"EFI Time Services:    enabled", output)
        self.assertIn(
            b"ResetSystem:          enabled (shutdown unavailable)", output
        )
        self.assertIn(b"Firmware flags:       0x000000000000001F", output)
        self.assertIn(b"GOP/UGA VGA text console ready", output)
        self.assertIn(b"Graphics Output:      GOP/UGA VGA BGRx", output)
        self.assertTrue(vm.is_running(), "QEMU exited during firmware boot")

    def test_scsi_disk_boot(self):
        self.require_accelerator("tcg")
        path = self.media_path("isp12160.img")
        make_fat_disk(path, app_path("smoke"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertIn("SCSI controller:      LSI53C895A",
                      result.raw_console)
        self.assertTrue(vm.is_running(), "QEMU exited after SCSI disk boot")

    def test_default_optical_boot(self):
        self.require_accelerator("tcg")
        path = self.media_path("optical.iso")
        make_el_torito_iso(path, app_path("smoke"), platform_id=0xEF)

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive",
            f"file={path},format=raw,media=cdrom,readonly=on",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smoke", SMOKE_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(), "QEMU exited after optical boot")

    def test_pci_root_protocols(self):
        self.require_accelerator("tcg")
        path = self.media_path("pci-root.img")
        make_fat_disk(path, app_path("i2000-pci-root"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-pci-root", PCI_ROOT_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after PCI root protocol test")

    def test_runtime_services(self):
        self.require_accelerator("tcg")
        path = self.media_path("runtime.img")
        make_fat_disk(path, app_path("i2000-runtime"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-runtime", RUNTIME_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        event = vm.event_wait("RESET", timeout=10.0)
        self.assertTrue(event["data"]["guest"])
        self.assertEqual(event["data"]["reason"], "guest-reset")
        self.assertTrue(vm.is_running(),
                        "QEMU exited after runtime service test")

    def test_graphics_protocols(self):
        self.require_accelerator("tcg")
        path = self.media_path("graphics.img")
        make_fat_disk(path, app_path("graphics"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "2G",
            "-smp", "1",
            "-display", "none",
            "-vga", "ati",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "graphics", GRAPHICS_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after graphics protocol test")

    def test_loader_memory_layout(self):
        self.require_accelerator("tcg")
        path = self.media_path("loader.img")
        make_fat_disk(path, app_path("i2000-loader"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg",
            "-m", "4G",
            "-smp", "2",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "i2000-loader", LOADER_CASES, 60.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after loader memory-layout test")

    def test_smp_rendezvous(self):
        self.require_accelerator("tcg")
        path = self.media_path("smp-merced.img")
        make_fat_disk(path, app_path("smp-merced"))

        vm = self.get_vm()
        vm.set_machine("hp-i2000,nvram=none")
        vm.set_console()
        vm.add_args(
            "-accel", "tcg,thread=multi",
            "-cpu", "merced",
            "-m", "4G",
            "-smp", "2",
            "-display", "none",
            "-net", "none",
            "-drive", f"file={path},format=raw,if=scsi,index=0",
        )
        vm.launch()

        result = wait_for_suite(
            vm.console_socket, "smp-merced", SMP_MERCED_CASES, 180.0,
            process_alive=vm.is_running,
        )
        self.assertEqual(result.failed, 0)
        self.assertSetEqual(set(result.cases), SMP_MERCED_CASES)
        self.assertTrue(vm.is_running(),
                        "QEMU exited after SMP rendezvous test")


if __name__ == "__main__":
    QemuSystemTest.main()
