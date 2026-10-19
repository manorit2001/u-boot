# SPDX-License-Identifier: GPL-2.0+

import os
import sys
import tempfile
import types
import unittest
from unittest import mock

from binman import firmware_handoff, k3_handoff


CONFIG = '''\
format: ti-k3-firmware-handoff
schema-version: 1
platform: test-board
secure-proxy:
  consumers:
    u-boot:
      host-id: 14
      tx-threads: {request-high: 10, request-low: 11}
      rx-thread: 12
    tf-a:
      host-id: 11
      tx-threads: {request: 3}
      rx-thread: 4
    optee:
      host-id: 12
      tx-threads: {request: 5}
      rx-thread: 6
'''


class TestK3Handoff(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.config = os.path.join(self.temp.name, 'config.yaml')
        with open(self.config, 'w', encoding='utf-8') as stream:
            stream.write(CONFIG)

    def test_wire_format_is_deterministic_and_padded(self):
        data = k3_handoff.generate_from_file(self.config, ['u-boot', 'tf-a'])
        self.assertEqual(data, k3_handoff.generate_from_file(
            self.config, ['u-boot', 'tf-a']))
        self.assertEqual(len(data), 4096)
        self.assertEqual(data[:4], bytes.fromhex('0bb10f4a'))
        self.assertEqual(data[5:8], bytes((2, 24, 3)))
        self.assertEqual(int.from_bytes(data[8:12], 'little'), 96)
        self.assertEqual(int.from_bytes(data[12:16], 'little'), 4096)
        self.assertEqual(sum(data[:96]) & 0xff, 0)
        self.assertEqual(data[96:], bytes(4096 - 96))
        # First TE header, then little-endian consumer/host/rx/count.
        self.assertEqual(int.from_bytes(data[24:28], 'little'),
                         (8 << 24) | k3_handoff.TAG_K3_SEC_PROXY)
        self.assertEqual(data[32:48], bytes.fromhex(
            '050000000e0000000c00000002000000'))

    def test_capacity_and_consumer_validation(self):
        with self.assertRaisesRegex(ValueError, 'exceeds capacity'):
            k3_handoff.generate_from_file(self.config, ['u-boot'], 40)
        with self.assertRaisesRegex(ValueError, 'not configured'):
            k3_handoff.generate_from_file(self.config, ['linux'])
        with self.assertRaisesRegex(ValueError, 'more than once'):
            k3_handoff.generate_from_file(self.config, ['tf-a', 'tf-a'])
        with self.assertRaisesRegex(ValueError, 'multiple of 8'):
            k3_handoff.generate_from_file(self.config, ['tf-a'], 25)

    def test_rejects_duplicate_keys_and_invalid_types_or_roles(self):
        for replacement, error in (
                ('host-id: 14\n      host-id: 15', 'duplicate YAML key'),
                ('host-id: 14', 'host-id must be an integer'),
                ('request-high: 10, request-low: 11', 'requires request')):
            with self.subTest(replacement=replacement):
                bad = CONFIG.replace('host-id: 14\n      tx-threads:',
                                     replacement + '\n      tx-threads:')
                if replacement.startswith('request-high'):
                    bad = CONFIG.replace(
                        'tx-threads: {request-high: 10, request-low: 11}',
                        'tx-threads: {request-high: 10}')
                elif replacement == 'host-id: 14':
                    bad = CONFIG.replace('host-id: 14', 'host-id: true')
                path = os.path.join(self.temp.name, 'bad.yaml')
                with open(path, 'w', encoding='utf-8') as stream:
                    stream.write(bad)
                with self.assertRaisesRegex(ValueError, error):
                    k3_handoff.load_config(path)

    def test_c_header_contract(self):
        data = k3_handoff.generate_from_file(self.config, ['tf-a'])
        header = k3_handoff.c_header(data)
        self.assertIn('#define K3_HANDOFF_DATA_LENGTH 4096U', header)
        self.assertIn('#define K3_HANDOFF_DATA_SIZE K3_HANDOFF_DATA_LENGTH', header)
        self.assertIn('k3_handoff_data[] __attribute__((aligned(8)))', header)

    def test_compact_stage_header(self):
        output = os.path.join(self.temp.name, 'stage.h')
        self.assertEqual(k3_handoff.main([
            '--config', self.config, '--consumer', 'tf-a',
            '--output', output, '--c-header']), 0)
        with open(output, encoding='utf-8') as stream:
            self.assertIn('#define K3_HANDOFF_DATA_LENGTH 56U', stream.read())

    def test_rejects_aliased_rx_tx_threads(self):
        for threads in ('{request-high: 10, request-low: 10}',
                        '{request-high: 10, request-low: 12}'):
            with self.subTest(threads=threads):
                with open(self.config, 'w', encoding='utf-8') as stream:
                    stream.write(CONFIG.replace(
                        '{request-high: 10, request-low: 11}', threads))
                with self.assertRaisesRegex(ValueError, 'distinct RX and TX'):
                    k3_handoff.load_config(self.config)

    def test_elf_identity_placement_and_padding(self):
        from binman import elf

        class Segment:
            def __init__(self, address, data, memsz=None):
                self.values = {'p_type': 'PT_LOAD', 'p_filesz': len(data),
                               'p_memsz': memsz or len(data),
                               'p_paddr': address, 'p_vaddr': address}
                self._data = data

            def __getitem__(self, key):
                return self.values[key]

            def data(self):
                return self._data

        class Symbol:
            def __init__(self, name, value):
                self.name = name
                self.entry = {'st_value': value}

        class Section:
            def __getitem__(self, key):
                return 'SHT_SYMTAB'

            def iter_symbols(self):
                return iter((Symbol('__TRANSFER_LIST_START__', 0x2000),
                             Symbol('__TRANSFER_LIST_END__', 0x3000),
                             Symbol('__BL31_START__', 0x1000),
                             Symbol('__BL31_END__', 0x1004)))

        class FakeElf:
            segments = [Segment(0x1000, b'abcd'), Segment(0x1008, b'xy'),
                        Segment(0x1010, b'', memsz=0x100)]

            def __init__(self, stream):
                pass

            def iter_sections(self):
                return iter((Section(),))

            def iter_segments(self):
                return iter(self.segments)

        root = types.ModuleType('elftools')
        root.__path__ = []
        package = types.ModuleType('elftools.elf')
        package.__path__ = []
        module = types.ModuleType('elftools.elf.elffile')
        module.ELFFile = FakeElf
        module.ELFError = Exception
        elf_path = os.path.join(self.temp.name, 'bl31.elf')
        with open(elf_path, 'wb') as stream:
            stream.write(b'fake')
        with mock.patch.object(elf, 'ELF_TOOLS', True), mock.patch.dict(
                sys.modules, {'elftools': root, 'elftools.elf': package,
                              'elftools.elf.elffile': module}):
            result = firmware_handoff.append_transfer_list(
                b'abcd\0\0\0\0xy', elf_path, b'test TL')
            self.assertEqual(result[:10], b'abcd\0\0\0\0xy')
            self.assertEqual(result[10:0x1000], bytes(0x1000 - 10))
            self.assertEqual(len(result), 0x2000)
            self.assertEqual(result[0x1000:0x1000 + 7], b'test TL')
            self.assertEqual(result[0x1000 + 7:], bytes(4096 - 7))
            with self.assertRaisesRegex(ValueError, 'does not match'):
                firmware_handoff.append_transfer_list(
                    b'xxxx\0\0\0\0xy', elf_path, b'test TL')
            with self.assertRaisesRegex(ValueError, 'exceeds ELF region'):
                firmware_handoff.append_transfer_list(
                    b'abcd\0\0\0\0xy', elf_path, bytes(4097))
            FakeElf.segments.append(Segment(0x1100, b'', memsz=0x2000))
            with self.assertRaisesRegex(ValueError, 'overlaps ELF runtime'):
                firmware_handoff.append_transfer_list(
                    b'abcd\0\0\0\0xy', elf_path, b'test TL')
            FakeElf.segments.pop()
            FakeElf.segments.append(Segment(0x2000, b'z'))
            overlap_binary = b'abcd\0\0\0\0xy' + bytes(0x2000 - 0x100a) + b'z'
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                firmware_handoff.append_transfer_list(
                    overlap_binary, elf_path, b'test TL')


if __name__ == '__main__':
    unittest.main()
