# SPDX-License-Identifier: GPL-2.0+
"""BL31 with an opt-in TI K3 secure-proxy transfer list."""

import os

from binman import state
from binman.etype.atf_bl31 import Entry_atf_bl31
from binman.firmware_handoff import append_transfer_list
from binman.k3_handoff import DEFAULT_CAPACITY, generate_from_file
from u_boot_pylib import tools


class Entry_ti_k3_handoff(Entry_atf_bl31):
    """Combined BL31 and a capacity-padded secure handoff transfer list."""

    def ObtainContents(self, fake_size=0):
        config = os.environ.get('K3_HANDOFF_CONFIG') or state.GetEntryArg(
            'k3-handoff-config')
        if not config:
            self.Raise('K3 handoff requires K3_HANDOFF_CONFIG or k3-handoff-config')
        super().ObtainContents(fake_size)
        if self.missing:
            self.Raise('K3 handoff requires a real BL31 binary')
        elf_path = state.GetEntryArg('atf-bl31-elf-path') or 'bl31.elf'
        elf_path = tools.get_input_filename(elf_path)
        if not elf_path:
            self.Raise('Cannot find BL31 ELF for K3 handoff')
        try:
            handoff = generate_from_file(config, ('tf-a', 'optee'),
                                         DEFAULT_CAPACITY)
            combined = append_transfer_list(self.data, elf_path, handoff)
        except (OSError, ValueError) as exc:
            self.Raise('K3 handoff: %s' % exc)
        self.SetContents(combined)
        return True
