# SPDX-License-Identifier: GPL-2.0+
"""Generic serialization and placement tests, including a real host ELF."""

from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from binman import firmware_handoff


class TestFirmwareHandoff(unittest.TestCase):
    def test_serialization(self):
        data = firmware_handoff.serialize_transfer_list([(2, b'abc')], 64)
        self.assertEqual(len(data), 64)
        self.assertEqual(struct.unpack_from('<IBBBBIIII', data),
                         (0x4a0fb10b, data[4], 2, 24, 3, 40, 64, 1, 0))
        self.assertEqual(sum(data[:40]) & 255, 0)
        self.assertEqual(data[32:35], b'abc')
        self.assertEqual(data[35:], bytes(29))
        for entries, capacity in [([(0x1000000, b'')], None),
                                  ([(True, b'')], None),
                                  ([(2, b'abc')], 32), ([], 25)]:
            with self.subTest(entries=entries, capacity=capacity):
                with self.assertRaises(ValueError):
                    firmware_handoff.serialize_transfer_list(entries, capacity)

    def test_real_elf_identity_placement_and_bounds(self):
        from binman import elf

        if not elf.ELF_TOOLS or not all(shutil.which(tool) for tool in
                                       ('cc', 'ld', 'objcopy')):
            self.skipTest('requires pyelftools and native GNU build tools')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'image.c'
            script = root / 'image.lds'
            image = root / 'image.elf'
            source.write_text('const char data[] = "abc"; char bss[16];\n')
            script.write_text('''
PHDRS { image PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x1000; __BL31_START__ = .;
    .data : { *(.rodata*) *(.data*) } :image
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :image
    __BL31_END__ = .;
    __TRANSFER_LIST_START__ = 0x2000;
    __TRANSFER_LIST_END__ = 0x3000;
    /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
''')
            for command in [
                    ['cc', '-c', '-fno-pie', str(source), '-o', str(root / 'image.o')],
                    ['ld', '-T', str(script), str(root / 'image.o'), '-o', str(image)],
                    ['objcopy', '-O', 'binary', str(image), str(root / 'image.bin')]]:
                subprocess.run(command, check=True, capture_output=True)
            binary = (root / 'image.bin').read_bytes()
            result = firmware_handoff.append_transfer_list(binary, image, b'test TL')
            self.assertEqual(result[:len(binary)], binary)
            self.assertEqual(result[len(binary):0x1000], bytes(0x1000 - len(binary)))
            self.assertEqual(result[0x1000:0x1007], b'test TL')
            self.assertEqual(result[0x1007:], bytes(4096 - 7))
            self.assertEqual(len(result), 0x2000)
            for bad, message in [(b'x' + binary[1:], 'does not match'),
                                 (binary + b'x', 'length does not match')]:
                with self.assertRaisesRegex(ValueError, message):
                    firmware_handoff.append_transfer_list(bad, image, b'TL')
            with self.assertRaisesRegex(ValueError, 'exceeds ELF region'):
                firmware_handoff.append_transfer_list(binary, image, bytes(4097))
            symbols, loads, memory = firmware_handoff._read_elf(
                image, firmware_handoff.DEFAULT_SYMBOLS.values())
            self.assertTrue(any(end - start > len(binary) for start, end in memory))
            for extra_memory, extra_load in [([(0x1100, 0x3100)], []),
                                             ([(0x2000, 0x2001)],
                                              [(0x2000, 0x2000, 0x2001, b'z')])]:
                with mock.patch.object(firmware_handoff, '_read_elf', return_value=(
                        symbols, loads + extra_load, memory + extra_memory)):
                    with self.assertRaisesRegex(ValueError, 'overlaps'):
                        firmware_handoff.append_transfer_list(binary, image, b'TL')
            subprocess.run(['objcopy', '--strip-symbol=__TRANSFER_LIST_END__',
                            str(image)], check=True, capture_output=True)
            with self.assertRaisesRegex(ValueError, 'missing symbol'):
                firmware_handoff.append_transfer_list(binary, image, b'TL')


if __name__ == '__main__':
    unittest.main()
