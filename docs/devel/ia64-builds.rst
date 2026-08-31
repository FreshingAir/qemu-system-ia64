IA-64 build configurations
==========================

The root ``README.md`` contains the standard Linux build.  This document
covers manual Windows cross-building and performance-oriented builds.

Windows cross-build
-------------------

GitHub Actions is the canonical source for prebuilt Windows archives.  The
following procedure is for local cross-builds on Debian or Ubuntu, including
WSL.  It is less thoroughly tested than the Linux build.

The firmware target also requires an IA-64 ELF toolchain whose
``ia64-linux-gnu-``-prefixed executables are on ``PATH``.

Install host tools
~~~~~~~~~~~~~~~~~~

.. code-block:: sh

   sudo apt-get update
   sudo apt-get install --no-install-recommends \
     build-essential \
     bison \
     binutils-mingw-w64-x86-64 \
     ca-certificates \
     curl \
     flex \
     g++-mingw-w64-x86-64 \
     gcc-mingw-w64-x86-64 \
     git \
     meson \
     ninja-build \
     pkg-config \
     python3 \
     python3-venv \
     zstd

Fetch the pinned dependencies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Run all remaining commands from the source-tree root.  The helper verifies the
downloaded packages and prints the extracted sysroot path.

.. code-block:: sh

   WIN_SYSROOT="$(./scripts/fetch-win64-deps.sh)"

Configure and build
~~~~~~~~~~~~~~~~~~~

.. code-block:: sh

   HOST_PKG_CONFIG="$(command -v pkg-config)"
   mkdir -p build-win64

   (
     cd build-win64
     PKG_CONFIG="$HOST_PKG_CONFIG" \
     PKG_CONFIG_LIBDIR="$WIN_SYSROOT/mingw64/lib/pkgconfig" \
     PKG_CONFIG_SYSROOT_DIR="$WIN_SYSROOT" \
     ../configure \
       --cross-prefix=x86_64-w64-mingw32- \
       --host-cc=gcc \
       --python=/usr/bin/python3 \
       --target-list=ia64-softmmu \
       --enable-system \
       --enable-tcg \
       --enable-pixman \
       --enable-fdt=internal \
       --enable-sdl \
       --enable-slirp \
       --enable-vnc \
       --disable-docs \
       --disable-werror
   )

   ninja -C build-win64 -j"$(nproc)" \
     qemu-system-ia64.exe \
     qemu-system-ia64w.exe \
     roms/ia64-firmware/ia64-firmware.bin

Create a relocatable directory
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The executables dynamically link to the pinned dependencies and compiler
runtime DLLs.  Stage them with the IA-64 data files in a distribution directory
before copying it to Windows.

.. code-block:: sh

   WIN_SYSROOT="$(./scripts/fetch-win64-deps.sh)"
   rm -rf build-win64-dist
   WIN_DIST="$PWD/build-win64-dist"
   LIBSSP_DLL="$(
     x86_64-w64-mingw32-gcc -print-file-name=libssp-0.dll
   )"
   WINPTHREAD_DLL="$(
     x86_64-w64-mingw32-gcc -print-file-name=libwinpthread-1.dll
   )"

   mkdir -p "$WIN_DIST/share/keymaps" "$WIN_DIST/licenses"

   install -m 0755 \
     build-win64/qemu-system-ia64.exe \
     build-win64/qemu-system-ia64w.exe \
     "$WIN_DIST/"

   for dll in \
     SDL2.dll \
     libffi-8.dll \
     libgio-2.0-0.dll \
     libglib-2.0-0.dll \
     libgmodule-2.0-0.dll \
     libgobject-2.0-0.dll \
     libiconv-2.dll \
     libintl-8.dll \
     libpcre2-8-0.dll \
     libpixman-1-0.dll \
     libslirp-0.dll \
     zlib1.dll; do
     install -m 0755 "$WIN_SYSROOT/mingw64/bin/$dll" "$WIN_DIST/$dll"
   done
   install -m 0755 "$LIBSSP_DLL" "$WIN_DIST/libssp-0.dll"
   install -m 0755 "$WINPTHREAD_DLL" "$WIN_DIST/libwinpthread-1.dll"

   install -m 0644 \
     build-win64/roms/ia64-firmware/ia64-firmware.bin \
     "$WIN_DIST/share/ia64-firmware.bin"
   install -m 0644 \
     pc-bios/efi-e1000.rom \
     pc-bios/vgabios-ati.bin \
     pc-bios/vgabios-stdvga.bin \
     "$WIN_DIST/share/"
   install -m 0644 pc-bios/keymaps/* "$WIN_DIST/share/keymaps/"

   install -m 0644 COPYING COPYING.LIB LICENSE README.md "$WIN_DIST/"
   cp -R "$WIN_SYSROOT/mingw64/share/licenses/." "$WIN_DIST/licenses/"

   x86_64-w64-mingw32-strip \
     "$WIN_DIST/qemu-system-ia64.exe" \
     "$WIN_DIST/qemu-system-ia64w.exe"

Open Command Prompt in ``build-win64-dist`` and run:

.. code-block:: bat

   qemu-system-ia64.exe ^
     -machine itanium2-vpc ^
     -bios share\ia64-firmware.bin ^
     -drive file="C:\path\to\guest-media.iso",media=cdrom,format=raw,readonly=on ^
     -display sdl ^
     -nic user,model=e1000

Use ``qemu-system-ia64w.exe`` to run without a separate console window.

Performance build
-----------------

The following IA-64-only configuration uses ``-O3`` and LTO, targets
x86-64-v2, and disables assertions.  ``QEMU_DISABLE_DEBUG_ASSERTS`` propagates
``NDEBUG`` and ``G_DISABLE_ASSERT`` to C, C++, and Objective-C sources and
disables assertion-dependent debug options.

.. code-block:: sh

   mkdir -p build-ia64-perf
   (
     cd build-ia64-perf
     ../configure --target-list=ia64-softmmu \
       --x86-version=2 \
       --enable-lto \
       --disable-debug-info \
       --enable-strip \
       --disable-qom-cast-debug \
       --disable-stack-protector \
       --disable-hardening \
       --disable-pie \
       --enable-trace-backends=nop \
       --extra-cflags='-DQEMU_DISABLE_DEBUG_ASSERTS -fomit-frame-pointer -ffunction-sections -fdata-sections' \
       --extra-cxxflags='-fomit-frame-pointer -ffunction-sections -fdata-sections' \
       --extra-ldflags='-Wl,-O2 -Wl,--gc-sections -no-pie' \
       -Doptimization=3 \
       --enable-gtk
     ninja qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
     strip --strip-unneeded qemu-system-ia64
   )

Meson's ``--enable-strip`` applies when installing targets rather than when
copying them directly from the build directory, hence the explicit ``strip``
above.

Profile-guided optimization
---------------------------

Use a dedicated build directory and the same compiler for both stages.

.. code-block:: sh

   mkdir -p build-ia64-pgo
   (
     cd build-ia64-pgo
     ../configure --target-list=ia64-softmmu \
       --enable-lto \
       --disable-qom-cast-debug \
       --disable-stack-protector \
       --disable-hardening \
       -Doptimization=2 \
       -Db_pgo=generate
     ninja qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
   )

Run representative guest workloads with ``build-ia64-pgo/qemu-system-ia64``,
then rebuild using the generated profiles:

.. code-block:: sh

   (
     cd build-ia64-pgo
     ./pyvenv/bin/meson configure -Db_pgo=use -Dwerror=false
     ninja qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
   )

Missing-profile warnings for untrained auxiliary targets are expected.  Profile
data is tied to the build tree, compiler, and source revision.
