# SPDX-License-Identifier: GPL-2.0+
"""Generate and validate TI K3 secure-proxy transfer-list handoff data."""

import argparse
import struct
import sys

import yaml

if __package__:
    from binman.firmware_handoff import serialize_transfer_list
else:
    from firmware_handoff import serialize_transfer_list

TAG_K3_SEC_PROXY = 0xfff000
DEFAULT_CAPACITY = 4096
CONSUMER_IDS = {
    'r5-spl': 1,
    'tf-a': 2,
    'optee': 3,
    'aarch64-spl': 4,
    'u-boot': 5,
    'linux': 6,
}
TX_ROLES = {'request': 0, 'request-high': 1, 'request-low': 2}


class _UniqueLoader(yaml.SafeLoader):
    pass


def _construct_mapping(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str):
            raise ValueError('YAML mapping keys must be strings')
        if key in mapping:
            raise ValueError('duplicate YAML key: %s' % key)
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


_UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
                              _construct_mapping)


def _mapping(value, where, keys):
    if not isinstance(value, dict):
        raise ValueError('%s must be a mapping' % where)
    unknown = set(value) - set(keys)
    if unknown:
        raise ValueError('%s has unknown field(s): %s' %
                         (where, ', '.join(sorted(str(key) for key in unknown))))
    return value


def _uint(value, where, maximum):
    if type(value) is not int or value < 0 or value > maximum:
        raise ValueError('%s must be an integer in range 0..%d' %
                         (where, maximum))
    return value


def load_config(path):
    """Load YAML and enforce the handoff schema without optional validators."""
    with open(path, encoding='utf-8') as stream:
        config = yaml.load(stream, Loader=_UniqueLoader)
    config = _mapping(config, 'config', ('format', 'schema-version', 'platform',
                                          'secure-proxy'))
    if config.get('format') != 'ti-k3-firmware-handoff':
        raise ValueError("format must be 'ti-k3-firmware-handoff'")
    if type(config.get('schema-version')) is not int or config['schema-version'] != 1:
        raise ValueError('schema-version must be 1')
    if not isinstance(config.get('platform'), str) or not config['platform'].strip():
        raise ValueError('platform must be a nonempty string')
    proxy = _mapping(config.get('secure-proxy'), 'secure-proxy', ('consumers',))
    consumers = proxy.get('consumers')
    if not isinstance(consumers, dict) or not consumers:
        raise ValueError('secure-proxy.consumers must be a nonempty mapping')
    for name, consumer in consumers.items():
        if name not in CONSUMER_IDS:
            raise ValueError('unknown consumer: %s' % name)
        consumer = _mapping(consumer, 'consumer %s' % name,
                            ('host-id', 'rx-thread', 'tx-threads'))
        _uint(consumer.get('host-id'), '%s.host-id' % name, 255)
        _uint(consumer.get('rx-thread'), '%s.rx-thread' % name, 65535)
        threads = _mapping(consumer.get('tx-threads'),
                           '%s.tx-threads' % name, TX_ROLES)
        roles = set(threads)
        if not roles or not (roles == {'request'} or
                             roles == {'request-high', 'request-low'}):
            raise ValueError(
                '%s.tx-threads requires request or request-high and request-low' %
                name)
        for role, thread in threads.items():
            _uint(thread, '%s.tx-threads.%s' % (name, role), 65535)
        assigned = [consumer['rx-thread'], *threads.values()]
        if len(set(assigned)) != len(assigned):
            raise ValueError('%s must use distinct RX and TX threads' % name)
    return config


def _consumer_payload(name, consumer):
    threads = consumer['tx-threads']
    payload = struct.pack('<IIII', CONSUMER_IDS[name], consumer['host-id'],
                          consumer['rx-thread'], len(threads))
    for role in sorted(threads, key=TX_ROLES.get):
        payload += struct.pack('<II', TX_ROLES[role], threads[role])
    return payload


def generate(config, consumers, capacity=DEFAULT_CAPACITY):
    """Return a capacity-sized TLv2 containing the requested consumers."""
    records = config['secure-proxy']['consumers']
    if not consumers:
        raise ValueError('at least one consumer is required')
    if len(set(consumers)) != len(consumers):
        raise ValueError('consumer specified more than once')
    entries = []
    for name in consumers:
        if name not in records:
            raise ValueError('consumer %s is not configured' % name)
        entries.append((TAG_K3_SEC_PROXY, _consumer_payload(name, records[name])))
    return serialize_transfer_list(entries, capacity)


def c_header(data):
    """Format bytes as a standalone C header declaration."""
    lines = ['/* Generated by tools/binman/k3_handoff.py; do not edit. */',
             '#ifndef K3_HANDOFF_DATA_H', '#define K3_HANDOFF_DATA_H', '',
             '#define K3_HANDOFF_DATA_LENGTH %dU' % len(data),
             '#define K3_HANDOFF_DATA_SIZE K3_HANDOFF_DATA_LENGTH',
             ('static const unsigned char k3_handoff_data[] '
              '__attribute__((aligned(8))) = {')]
    for offset in range(0, len(data), 12):
        lines.append('    ' + ', '.join('0x%02x' % byte
                                         for byte in data[offset:offset + 12]) + ',')
    lines += ['};', '', '#endif /* K3_HANDOFF_DATA_H */', '']
    return '\n'.join(lines)


def generate_from_file(path, consumers, capacity=DEFAULT_CAPACITY):
    return generate(load_config(path), consumers, capacity)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--consumer', action='append', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--capacity', type=lambda value: int(value, 0),
                        help='Reserved size; C headers default to the used size')
    parser.add_argument('--c-header', action='store_true')
    args = parser.parse_args(argv)
    try:
        capacity = args.capacity
        if capacity is None and not args.c_header:
            capacity = DEFAULT_CAPACITY
        data = generate_from_file(args.config, args.consumer, capacity)
        if args.c_header:
            with open(args.output, 'w', encoding='utf-8') as stream:
                stream.write(c_header(data))
        else:
            with open(args.output, 'wb') as stream:
                stream.write(data)
    except (OSError, ValueError, yaml.YAMLError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    sys.exit(main())
