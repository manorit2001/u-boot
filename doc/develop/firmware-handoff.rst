.. SPDX-License-Identifier: GPL-2.0+

Firmware handoff helpers
=======================

``FIRMWARE_HANDOFF`` validates a bounded, checksummed Transfer List v2 and
provides tag lookup. Platforms implement ``plat_get_transfer_list()`` with a
reserved memory extent known independently of the list header. Valid lists may
end at the last entry's data, without final alignment padding.

Binman's ``firmware_handoff`` module serializes generic entries and appends a
list at an ELF-defined boundary, checking initialized bytes and runtime storage.
It neither allocates target RAM nor authenticates an image.

Framework-only host checks::

   PYTHONPATH=tools python3 -m unittest binman.firmware_handoff_test -v
   cc '-DCONFIG_IS_ENABLED(x)=0' -Iinclude -Iarch/sandbox/include \
       test/lib/firmware_handoff_host_test.c -o /tmp/firmware-handoff-test
   /tmp/firmware-handoff-test
