# SPDX-License-Identifier: GPL-2.0+

import os
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

from binman import k3_handoff
from binman import state
from binman.etype import ti_k3_handoff
from binman.etype.atf_bl31 import Entry_atf_bl31
from binman.k3_handoff_test import CONFIG
from u_boot_pylib import tools


class TestK3HandoffEntry(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.input_dir = os.path.join(self.temp.name, 'firmware')
        os.makedirs(os.path.join(self.input_dir, 'ti-sysfw'))
        with open(os.path.join(self.input_dir, 'bl31.elf'), 'wb') as stream:
            stream.write(b'ELF')

    def _obtain(self, config):
        entry = ti_k3_handoff.Entry_ti_k3_handoff.__new__(
            ti_k3_handoff.Entry_ti_k3_handoff)
        entry._node = SimpleNamespace(path='/test')

        def obtain_bl31(obj, fake_size=0):
            obj.data = b'BL31'
            obj.missing = False

        with mock.patch.dict(os.environ, {'K3_HANDOFF_CONFIG': config}), \
                mock.patch.object(tools, 'indir', [self.input_dir]), \
                mock.patch.object(state, 'GetEntryArg', return_value=None), \
                mock.patch.object(Entry_atf_bl31, 'ObtainContents',
                                  obtain_bl31), \
                mock.patch.object(ti_k3_handoff.Entry_ti_k3_handoff,
                                  'SetContents'), \
                mock.patch.object(ti_k3_handoff, 'append_transfer_list',
                                  return_value=b'combined'):
            self.assertTrue(entry.ObtainContents())

    def test_resolves_config_from_binman_input_dir(self):
        config_path = os.path.join(self.input_dir, 'ti-sysfw',
                                   'k3-handoff-example.yaml')
        with open(config_path, 'w', encoding='utf-8') as stream:
            stream.write(CONFIG)

        with mock.patch.object(ti_k3_handoff, 'generate_from_file',
                               wraps=k3_handoff.generate_from_file) as generate:
            self._obtain('ti-sysfw/k3-handoff-example.yaml')

        generate.assert_called_once_with(
            config_path, ('tf-a', 'optee'), k3_handoff.DEFAULT_CAPACITY)

    def test_keeps_absolute_config_path(self):
        config_path = os.path.join(self.temp.name, 'absolute.yaml')
        with open(config_path, 'w', encoding='utf-8') as stream:
            stream.write(CONFIG)

        with mock.patch.object(ti_k3_handoff, 'generate_from_file',
                               wraps=k3_handoff.generate_from_file) as generate:
            self._obtain(config_path)

        generate.assert_called_once_with(
            config_path, ('tf-a', 'optee'), k3_handoff.DEFAULT_CAPACITY)

    def test_reports_config_missing_from_input_dirs(self):
        with self.assertRaisesRegex(
                ValueError, "K3 handoff: Filename 'missing.yaml' not found"):
            self._obtain('missing.yaml')


if __name__ == '__main__':
    unittest.main()
