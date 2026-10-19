.. SPDX-License-Identifier: GPL-2.0+

TI K3 secure-proxy handoff (experimental)
========================================

The opt-in ``ti-k3-handoff`` binman entry combines BL31 with a 4096-byte
Transfer List v2 region. The combined output must be signed as a whole; signing
only the original BL31 binary does not authenticate the handoff configuration.
The configuration must also agree with SYSFW ownership and security/RM
configuration. This mechanism is experimental and uses the non-standard tag
``0xfff000``; it is not a production upstream tag allocation.

Pass ``K3_HANDOFF_CONFIG=path/to/config.yaml`` in the environment or the
binman entry argument ``k3-handoff-config=path/to/config.yaml``. The BL31 ELF
is selected with ``atf-bl31-elf-path=path/to/bl31.elf`` (default
``bl31.elf``). U-Boot's normal build accepts ``BL31_ELF=path/to/bl31.elf``.
The ELF must define ``__TRANSFER_LIST_START__``, ``__TRANSFER_LIST_END__``,
``__BL31_START__`` and ``__BL31_END__``; the 4096-byte handoff region must
be page-aligned, follow BL31 runtime storage and contain no initialized load
data. This entry requires a handoff config; feature-disabled builds keep the
unchanged ``atf-bl31`` entry type.

The shared generator can also produce a stage-local C header for Kbuild::

  python3 tools/binman/k3_handoff.py --config config.yaml \
      --consumer u-boot --consumer linux --output k3_handoff.h --c-header

See ``k3-handoff-schema.yaml`` and ``k3-handoff-example.yaml``. Example thread
and host values are dummy values only and must not be used on a board.

Build and packaging flow
------------------------

Use one external YAML file for the stack. Its intended home is alongside the
corresponding TI firmware release; the example here is only a test fixture.
There is no configurable proxy-instance selector: the platform's existing
secure-proxy driver determines its MMIO instance. The YAML changes host IDs and
threads, not SoC addresses or SYSFW resource ownership.

Build TF-A with ``K3_SEC_PROXY_HANDOFF=1`` and retain its matching ELF. Build
OP-TEE with ``CFG_K3_SEC_PROXY_HANDOFF=y``; that selects its standard transfer-list
boot path. Neither feature is enabled by default.

For both the R5 and AArch64 U-Boot configurations, enable
``CONFIG_K3_SEC_PROXY_HANDOFF`` and ``CONFIG_SPL_K3_SEC_PROXY_HANDOFF`` using
the normal configuration workflow. No board defconfig is enabled with the
dummy example. For example, after selecting the appropriate defconfigs::

  scripts/config --file build/r5/.config -e K3_SEC_PROXY_HANDOFF \
      -e SPL_K3_SEC_PROXY_HANDOFF
  scripts/config --file build/a72/.config -e K3_SEC_PROXY_HANDOFF \
      -e SPL_K3_SEC_PROXY_HANDOFF

Provide the validated, board-specific configuration to every build invocation::

  export K3_HANDOFF_CONFIG=/absolute/path/to/firmware-handoff.yaml
  make O=build/r5 CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig
  make O=build/r5 CROSS_COMPILE=arm-linux-gnueabihf- \
      BINMAN_INDIRS=/path/to/ti-linux-firmware
  make O=build/a72 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
  make O=build/a72 CROSS_COMPILE=aarch64-linux-gnu- \
      BL31=/path/to/bl31.bin BL31_ELF=/path/to/bl31/bl31.elf \
      TEE=/path/to/tee-raw.bin BINMAN_INDIRS=/path/to/ti-linux-firmware

The common K3 Binman templates select ``ti-k3-handoff`` for both signed and
unsigned BL31 images when this feature is enabled. Secure-image authentication
therefore covers the combined BL31 plus list. The loader must load and reserve
that complete extent, including the padding over BL31-owned runtime storage.
Follow the board's existing signing/key configuration; do not deploy the
unsigned test output as an authenticated image.

Kbuild generates compact, immutable stage-local lists: R5 SPL contains only
consumer 1; AArch64 SPL only consumer 4; U-Boot proper contains consumers 5 and
6. A priority-pair R5 record occupies 64 bytes, including the list header. These
bytes are part of the existing U-Boot images, so no extra early SRAM buffer or
incoming-register convention is needed. TI-SCI selects its configuration before
requesting mailbox channels or sending its first transaction.

BL31 discovers its list relative to its own loaded image, consumes consumer 2,
and supplies the secure list with a final FDT entry to OP-TEE through the standard
AArch64 Firmware Handoff register convention. That secure list is never passed
to non-secure U-Boot. Before Linux boot, the common K3 FDT fixup translates
consumer 6 into the existing ``ti,host-id`` and ``mboxes`` bindings, preserving
mailbox providers and names. Linux needs no new transfer-list implementation.

The host and runtime transports currently use the request thread, or the low
priority request thread when a priority pair is specified. High-priority metadata
does not introduce new priority scheduling. Ordinary configuration updates
rebuild/repackage U-Boot and its embedded records, but reuse the same BL31 binary
and ELF. Do not mix the binary from one TF-A build with another build's ELF.

Reusable and vendor-specific pieces
----------------------------------

``tools/binman/firmware_handoff.py`` serializes standard containers and verifies
ELF/image identity and runtime-safe placement without knowing TI payloads.
``tools/binman/k3_handoff.py`` validates the TI YAML and encodes only the TI
entry payloads. The ``ti-k3-handoff`` entry is the K3 packaging integration.
Runtime discovery and bounded container validation are framework responsibilities;
K3 provides its stage-local storage and TI-SCI consumer integration.

Validation
----------

Host generator and placement tests::

  PYTHONPATH=tools python3 -m unittest binman.k3_handoff_test -v

Standalone container/payload tests use native sandbox types::

  cc '-DCONFIG_IS_ENABLED(x)=0' -Iinclude -Iarch/sandbox/include \
      test/boot/k3_handoff_host_test.c -o /tmp/k3-handoff-test
  /tmp/k3-handoff-test

After building U-Boot's host libfdt objects, run the Linux DT regression test
(replace ``build/a72`` with your build output directory)::

  cc '-DCONFIG_IS_ENABLED(x)=1' -include include/compiler.h \
      -idirafter include -idirafter arch/arm/include \
      -idirafter dts/upstream/include -I scripts/dtc/libfdt -I tools \
      -DUSE_HOSTCC -D__KERNEL_STRICT_NAMES -D_GNU_SOURCE \
      test/boot/k3_handoff_fdt_host_test.c \
      build/a72/scripts/dtc/libfdt/*.o -o /tmp/k3-handoff-fdt-test
  /tmp/k3-handoff-fdt-test

These tests do not verify target mailbox ownership, reset-time authentication,
or successful hardware boot. Confirm those with the actual firmware release,
resource configuration and board before deploying.
