# SPDX-License-Identifier: GPL-2.0+
"""Generic ELF-validated placement of serialized firmware handoff data."""

import struct


DEFAULT_SYMBOLS = {
    'transfer_start': '__TRANSFER_LIST_START__',
    'transfer_end': '__TRANSFER_LIST_END__',
    'runtime_start': '__BL31_START__',
    'runtime_end': '__BL31_END__',
}


def serialize_transfer_list(entries, capacity=None):
    """Serialize (24-bit tag, payload bytes) entries as a checksummed TLv2."""
    output = bytearray(24)
    for tag, payload in entries:
        if type(tag) is not int or not 0 <= tag <= 0xffffff:
            raise ValueError('transfer-list tag must fit in 24 bits')
        output += struct.pack('<II', (8 << 24) | tag, len(payload))
        output += payload
        output += bytes(-len(output) % 8)
    used = len(output)
    capacity = used if capacity is None else capacity
    if type(capacity) is not int or capacity < 24 or capacity % 8:
        raise ValueError('capacity must be at least 24 and a multiple of 8')
    if capacity > 0xffffffff or used > capacity:
        raise ValueError('handoff data size %d exceeds capacity %d' %
                         (used, capacity))
    struct.pack_into('<IBBBBIIII', output, 0, 0x4a0fb10b, 0, 2, 24, 3,
                     used, capacity, 1, 0)
    output[4] = -sum(output) & 0xff
    return bytes(output) + bytes(capacity - used)


def append_transfer_list(binary, elf_path, transfer_list, symbols=None):
    """Append a serialized transfer list at its ELF-defined image offset.

    The binary must be the flat image corresponding to the ELF's initialized
    PT_LOAD bytes. NOBITS memory is deliberately excluded. The transfer-list
    region is zero-filled to its ELF symbol size. Runtime allocations, including
    NOBITS segments, must end before the region starts.

    Args:
        binary (bytes): Flat binary image.
        elf_path (str): Matching ELF image.
        transfer_list (bytes): Already serialized handoff data.
        symbols (dict): Optional mapping overriding DEFAULT_SYMBOLS keys.

    Returns:
        bytes: Binary followed by zero padding and the full transfer region.
    """
    names = dict(DEFAULT_SYMBOLS)
    if symbols:
        unknown = set(symbols) - set(names)
        if unknown:
            raise ValueError('unknown handoff symbol key(s): %s' %
                             ', '.join(sorted(unknown)))
        names.update(symbols)

    found, loads, memory = _read_elf(elf_path, names.values())
    missing = [name for name in names.values() if name not in found]
    if missing:
        raise ValueError('ELF missing symbol(s): %s' % ', '.join(missing))
    start, end, runtime_start, runtime_end = (
        found[names[key]] for key in
        ('transfer_start', 'transfer_end', 'runtime_start', 'runtime_end'))

    if runtime_start >= runtime_end:
        raise ValueError('invalid BL31 runtime boundary symbols')
    if start >= end or start < runtime_end:
        raise ValueError('transfer-list region overlaps or precedes BL31 runtime')
    if any(finish > start for _, finish in memory):
        raise ValueError('transfer-list region overlaps ELF runtime allocations')
    region_size = end - start
    if len(transfer_list) > region_size:
        raise ValueError('serialized transfer list exceeds ELF region')
    base, _, load_delta = _validate_binary(binary, loads)
    start -= load_delta
    runtime_end -= load_delta
    if start < runtime_end:
        raise ValueError('transfer-list region overlaps BL31 runtime')
    for address, _, finish, _ in loads:
        if finish > start:
            raise ValueError('transfer-list region overlaps initialized ELF data')
    offset = start - base
    if offset < len(binary):
        raise ValueError('transfer-list region is inside the binary image')
    region = transfer_list + bytes(region_size - len(transfer_list))
    return binary + bytes(offset - len(binary)) + region


def _read_elf(elf_path, wanted):
    from binman import elf

    if not elf.ELF_TOOLS:
        raise ValueError('pyelftools is required to validate firmware ELF')
    from elftools.elf.elffile import ELFError, ELFFile

    found = {}
    loads = []
    memory = []
    try:
        with open(elf_path, 'rb') as stream:
            parsed = ELFFile(stream)
            wanted = set(wanted)
            for section in parsed.iter_sections():
                if section['sh_type'] in ('SHT_SYMTAB', 'SHT_DYNSYM'):
                    for symbol in section.iter_symbols():
                        if symbol.name in wanted:
                            found[symbol.name] = symbol.entry['st_value']
            for segment in parsed.iter_segments():
                if segment['p_type'] != 'PT_LOAD':
                    continue
                virtual = segment['p_vaddr']
                memory.append((virtual, virtual + segment['p_memsz']))
                if segment['p_filesz']:
                    physical = segment['p_paddr']
                    finish = physical + segment['p_filesz']
                    loads.append((physical, virtual, finish, segment.data()))
    except ELFError as exc:
        raise ValueError('invalid firmware ELF: %s' % exc) from exc
    return found, loads, memory


def _validate_binary(binary, loads):
    if not loads:
        raise ValueError('ELF has no initialized PT_LOAD data')
    deltas = {virtual - physical for physical, virtual, _, _ in loads}
    if len(deltas) != 1:
        raise ValueError('unsupported ELF with inconsistent PT_LOAD addresses')
    load_delta = deltas.pop()
    base = min(item[0] for item in loads)
    limit = max(item[2] for item in loads)
    if len(binary) != limit - base:
        raise ValueError('binary length does not match ELF initialized range')
    for address, _, finish, data in loads:
        offset = address - base
        if binary[offset:offset + finish - address] != data:
            raise ValueError('binary does not match ELF PT_LOAD bytes')
    return base, limit, load_delta
