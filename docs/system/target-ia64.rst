.. _system-target-ia64:

IA-64 System emulator
=====================

QEMU's IA-64 system emulator provides virtual PC and HP workstation machine
models.  It uses TCG and the project-provided EFI firmware.

Machine models
--------------

The machine models are grouped by processor generation:

``itanium-vpc`` and ``hp-i2000`` (Merced generation)
  ``itanium-vpc`` defaults to the ``merced`` CPU model.  ``hp-i2000``
  emulates the Intel 460GX-based HP workstation and requires the same CPU
  model.  Both use PS/2 input by default.

``itanium2-vpc`` and ``hp-zx6000`` (Itanium 2 generation)
  ``itanium2-vpc`` defaults to the ``montecito`` CPU model.  ``hp-zx6000``
  emulates the HP zx1-based workstation and requires ``madison-zx6000``.
  Both use USB input by default.

``ia64-vpc`` is an alias for ``itanium2-vpc``.  The virtual PC models support
up to 64 CPUs; the HP models support one or two.  Use
``-accel tcg,thread=multi`` for more than one CPU.

Building and running
--------------------

The firmware requires an ``ia64-linux-gnu-*`` ELF cross toolchain in
``PATH``::

  ./configure --target-list=ia64-softmmu
  ninja -C build qemu-system-ia64 \
      roms/ia64-firmware/ia64-firmware.bin

All machine models use ``ia64-firmware.bin`` by default.  QEMU searches its
firmware and data directories for this file.  Use ``-bios PATH`` to load a
different image, or ``-bios none`` when booting without firmware.

A typical invocation is::

  build/qemu-system-ia64 \
      -machine hp-i2000,nvram=/path/to/guest.nvram \
      -drive file=/path/to/guest-disk.qcow2,format=qcow2 \
      -display gtk

On the HP models, disks without an explicit interface use SCSI and CD-ROMs
use IDE.  An explicit ``if=scsi`` or ``if=ide`` takes precedence.

Virtual PC options
------------------

``i8042=on|off``
  Select PS/2 input when enabled and USB input when disabled.  It defaults to
  ``on`` for ``itanium-vpc`` and ``off`` for ``itanium2-vpc``.

``firmware-ide-dma=on|off``
  Enable or disable firmware CMD646 bus-master DMA.  The default is ``on``.

``firmware-console=serial|vga``
  Select the console advertised by HCDP.  The default is ``vga``.

``alat=zero|full``
  Select the ALAT model.  The default is ``zero``; ``full`` is limited to one
  CPU.

EFI variable storage
--------------------

All machine models default to ``nvram=auto``.  This uses a file named
``nvram`` beside the selected firmware.  Use ``nvram=PATH`` to select a file
or ``nvram=none`` to disable persistence.  A separate file should be used for
each virtual machine.

New backing files are 512 KiB.  Existing 64 KiB files remain 64 KiB when
updated.  The HP models expose a 512 KiB variable store, while the virtual PC
models use the first 64 KiB and preserve the rest of a larger backing file.

Status
------

The IA-64 target remains incomplete and is intended for experimental use.
