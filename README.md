# qemu-system-ia64

Experimental QEMU full-system emulation for IA-64 guests.

> [!IMPORTANT]
> This codebase was developed with assistance from large language models
> (LLMs). Do not submit project-specific changes to upstream QEMU.

## Quick start

### Download prebuilt binaries

Download a host archive and the separate firmware artifact from
[GitHub Actions](https://github.com/syunnPC/qemu-system-ia64/actions/workflows/build.yml).
Builds are available for Windows AMD64 and Linux AMD64/AArch64.

Place `ia64-firmware.bin` in the working directory, or pass its path with
`-bios`. [GitHub Releases](https://github.com/syunnPC/qemu-system-ia64/releases)
contains selected builds but may not be current.

> [!WARNING]
> The `x86-64-v2-optimized` AMD64 builds require an x86-64-v2 host and disable
> debugging and hardening features for additional speed. They are experimental
> and may be unstable.
> If a guest is unstable, use the matching standard build.

### Build from source

Building the firmware requires an IA-64 ELF cross toolchain with
`ia64-linux-gnu-` tools on `PATH`.

```sh
./configure --target-list=ia64-softmmu --enable-gtk
ninja -C build qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
```

### Command line example

```sh
./build/qemu-system-ia64 \
  -machine itanium2-vpc \
  -drive file=/path/to/guest-media.iso,media=cdrom,format=raw,readonly=on \
  -display gtk
```

Use `nvram=<path>` machine property to specify an NVRAM file. Set `nvram=none` for non-persistent EFI variables.

## Machine profiles

A machine must be selected explicitly with `-machine`.

| Machine | Intended use | Default CPU |
| --- | --- | --- |
| `hp-i2000` | Older IA-64 operating systems, with experimental real-machine emulation | `merced` (fixed) |
| `hp-zx6000` | Most IA-64 operating systems, with experimental real-machine emulation | `madison-zx6000` (fixed) |
| `itanium2-vpc` | Most IA-64 operating systems | `montecito` |
| `itanium-vpc` | Older IA-64 operating systems | `merced` |

`ia64-vpc` is an alias of `itanium2-vpc`. The HP profiles reject `-cpu`
overrides; the virtual PC profiles allow them and providing flexible configurations.
All profiles use `ia64-firmware.bin` by default unless overridden with `-bios`.
Emulation of `hp-i2000` and `hp-zx6000` is still experimental and incomplete.

## Common options

| Purpose | Option |
| --- | --- |
| One vCPU | `-accel tcg,thread=single` |
| Multiple vCPUs | `-accel tcg,thread=multi -smp N` |
| User-mode networking | `-nic user,model=e1000` |
| Disable networking | `-nic none` |
| Serial console | `-serial stdio` |
| AHCI HDD | `-drive ...,if=none,id=<id> -device ide-hd,drive=<id>,bus=ide.<number>` |
| AHCI CD | `-drive ...,media=cdrom,readonly=on,if=none,id=<id> -device ide-cd,drive=<id>,bus=ide.<number>` |

The HP profiles support up to two vCPUs; the virtual PC profiles support up to
64. Two to four vCPUs are usually the best choice, and more than eight is not
recommended.

Press F2, F12, or Delete during startup to open the EFI shell.

## Status

Several IA-64 operating systems boot, but instruction emulation, privileged
behavior, floating-point handling, and device support remain experimental.

## Related projects

- [IA-64 ATI XPDM driver](https://github.com/syunnPC/qemu-system-ia64-ati-xpdm)

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
