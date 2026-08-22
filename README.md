# qemu-system-ia64

Experimental QEMU full-system emulation target for IA-64/Itanium guests.

> [!IMPORTANT]
> This codebase was developed with assistance from large language models
> (LLMs). Do not submit project-specific changes to upstream QEMU.

## Quick start

### Prebuilt binaries

[GitHub Actions](https://github.com/syunnPC/qemu-system-ia64/actions/workflows/build.yml)
is the canonical source for current Windows and Linux x86-64 host archives and
the separate firmware artifact. Download one host archive and the firmware,
then pass the extracted `ia64-firmware.bin` path to `-bios`.

[GitHub Releases](https://github.com/syunnPC/qemu-system-ia64/releases)
retains selected builds for convenience, but may lag behind or omit some host
archives. Use GitHub Actions for the latest binaries.

### Build

Building the firmware requires an IA-64 ELF cross toolchain whose
`ia64-linux-gnu-`-prefixed executables are on `PATH`.

```sh
./configure --target-list=ia64-softmmu --enable-gtk
ninja -C build qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
```

For Windows cross-build instructions and performance-oriented configurations,
see [the IA-64 build guide](docs/devel/ia64-builds.rst).

### Boot installation media

```sh
./build/qemu-system-ia64 \
  -machine itanium2-vpc \
  -bios ./build/roms/ia64-firmware/ia64-firmware.bin \
  -drive file=/path/to/guest-media.iso,media=cdrom,format=raw,readonly=on \
  -display gtk
```

## Emulated platform

QEMU has no default IA-64 machine, so `-machine` is required. The canonical
machine names are `itanium2-vpc` and `itanium-vpc`. `ia64-vpc` is 
a backward-compatible alias of `itanium2-vpc`.

### Machine profiles

| Machine | Target operating systems | Default CPU |
| --- | --- | --- |
| `itanium2-vpc` | Most IA-64 operating systems | `montecito` |
| `itanium-vpc` | Older systems, such as Windows XP 64-Bit Edition (Version 2002) and earlier releases | `merced` |

`itanium-vpc` enables first-generation firmware and SAL
compatibility workarounds and omits ICH9 AHCI by default.

Machine and CPU selection are independent. A `-cpu` override does not change
the selected machine profile's firmware compatibility behavior.

### Defaults and facilities

| Component | Default | Options and limits |
| --- | --- | --- |
| vCPUs | 1 | 1 to 64 |
| RAM | 2 GiB | Set with `-m` |
| Firmware | None | Pass the project firmware with `-bios`; sources are under `roms/ia64-firmware/` |
| Graphics | ATI-compatible PCI graphics | Standard VGA with `-vga std` |
| Input | PS/2 | USB with `i8042=off` |
| Network | 82540EM-compatible e1000 | User-mode and TAP backends; no EFI network boot |
| Boot storage | LSI53C895A SCSI | Optional CMD646 IDE/ATAPI |

The machines also provide OHCI/UHCI USB, local and I/O SAPIC, ACPI tables,
RTC, watchdog, persistent NVRAM, serial and debug ports, and `savevm`/`loadvm`
snapshot support.

### CPU models

| CPU | IA-32 execution | Additional instructions |
| --- | --- | --- |
| `merced` | Yes | None |
| `madison` | Yes | `brl` |
| `montecito` | No | `brl`; `ld16`/`st16`/`cmp8xchg16` families; `vmsw.0`/`vmsw.1` (architectural faults only) |

## Runtime configuration

### SMP and input

Use `-accel tcg,thread=single` for one vCPU and
`-accel tcg,thread=multi -smp N` for 2 to 64 vCPUs. With `i8042=off`, the
machine adds a USB keyboard and absolute tablet without requiring `-usb`.

> [!NOTE]
> Having more processors does not necessarily guarantee better results.
> As the number of cores increases, performance penalties can occur due to inter-core synchronisation and other factors.
> In most cases, 2–4 SMPs are optimal, and we recommend a maximum of 8.

### Graphics

To set a preferred VBE resolution and 32 MiB of video memory, add:

```sh
-vga ati \
-global ati-vga.xres=3840 \
-global ati-vga.yres=2160 \
-global ati-vga.vgamem_mb=32
```

Specify `xres` and `yres` together; `xres` must be a multiple of eight. Unless
`xmax` and `ymax` are also supplied, the preferred resolution is the largest
advertised through INT 10h VBE. The fixed PCI layout supports up to 64 MiB of
video memory, and the predefined mode list extends through 5120x2880 at 32 bpp.
The default is 1280x1024 with 16 MiB. Host monitor resolution is not detected
automatically.

The IA-64 XPDM driver for the emulated ATI device supports Windows XP through
Windows Server 2008 R2 and is available from
[qemu-system-ia64-ati-xpdm](https://github.com/syunnPC/qemu-system-ia64-ati-xpdm).

### Networking

With libslirp, connect the default e1000 controller using
`-nic user,model=e1000`. Standard TAP backends are also supported; use
`-nic none` to omit the controller.

### Serial, debug, and EFI variables

Use `-serial stdio` for serial output. `-debug-port` exposes the transport
described by the ACPI DBGP table; for example,
`-debug-port tcp::4444,server=on,wait=on,nodelay=on`.

EFI variables default to a 64 KiB `nvram` file beside the selected firmware.
Use a separate file for each VM with
`-machine itanium2-vpc,nvram=/path/to/guest.nvram`, or use `nvram=none` for
process-local variables.

## EFI firmware and shell

The firmware waits three seconds for F2, F12, or Delete before boot. Any of
these keys opens the EFI shell from the graphical or serial console.

Common shell commands include:

```text
map
ls fs0:\EFI\BOOT
run fs0:\EFI\BOOT\TOOL.EFI argument
boot fs0:
bootorder Boot0001 Boot0000
bootnext Boot0001
```

`boot fsN:` launches `\EFI\BOOT\BOOTIA64.EFI`. `bootnext` is consumed by the
next automatic boot; boot-order changes persist when an NVRAM file is
configured. Attach an installed EFI disk with, for example,
`-drive file=/path/to/guest-disk.qcow2,format=qcow2`.

## Tests

Use the build-local Meson selected by `configure`:

```sh
build/pyvenv/bin/meson test -C build --suite ia64 --print-errorlogs
build/pyvenv/bin/meson test -C build --suite qtest-ia64 --print-errorlogs
build/pyvenv/bin/meson test -C build --suite func-ia64 --print-errorlogs
build/pyvenv/bin/meson test -C build --suite func-ia64-thorough --print-errorlogs
```

See [the IA-64 testing guide](docs/devel/testing/ia64.rst) for focused runs and
test-authoring rules.

## Status

The emulator boots several IA-64 operating-systems. Automated tests
cover four-vCPU Itanium 2 and 64-vCPU Merced configurations. Instruction
emulation, privileged behavior, floating-point handling, and device support
remain experimental.

### Screenshots

<table align="center">
  <tr>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Server 2008 R2"
        src="https://github.com/user-attachments/assets/ff3563ff-7fb2-4245-bc42-6ec86ed51ce6"
      />
    </td>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Codename Longhorn build 4051"
        src="https://github.com/user-attachments/assets/bab22228-ff1e-421b-bc79-c314850c2cab"
      />
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Whistler Advanced Server 64-bit Edition"
        src="https://github.com/user-attachments/assets/3c2bf20f-51eb-4ef3-b560-7dc75a01f6ac"
      />
    </td>
    <td width="50%">
      <img
        width="100%"
        alt="Debian 7.11.0 with GUI"
        src="https://github.com/user-attachments/assets/d1e5cdaa-64d6-4f91-9215-277423e268a2"
      />
    </td>
  </tr>
</table>

## Legal disclaimer

Proprietary guest operating-system images, installation media, and firmware
are not included. Users must supply them under the applicable licenses.

This independent project is not affiliated with, endorsed by, sponsored by,
or supported by Intel, HPE, the QEMU Project, or Microsoft Corporation.

QEMU is licensed under the GNU General Public License, version 2; see the
license files in this repository. Microsoft and Windows are trademarks of the
Microsoft group of companies. All other product and company names and
trademarks are the property of their respective owners.
