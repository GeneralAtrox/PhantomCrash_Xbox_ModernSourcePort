"""The shipped eight-callback boss suspension cycle (PAL battle overlay)."""

import struct

from .program import ProgramData
from .world_units import world_units


def decode_suspension(program, units):
    guards = {0x4dcd5c: 0x3c024080, 0x4dcd70: 0xae020b38,
              0x4dcd74: 0xae000b3c, 0x4e5db8: 0x46011040,
              0x4e5dc8: 0xaca00000, 0x4e5e20: 0x46010000,
              0x4e5e60: 0x46010000, 0x4e5e94: 0x24a50b38,
              0x4e5ed4: 0x24a50b3c, 0x4e5e0c: 0xc4800084,
              0x4e5e28: 0xe4800084, 0x4e5e4c: 0xc4800084,
              0x4e5e64: 0xe4800084}
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError('Unsupported S.L.A.I. suspension callback')

    def constant(high, low=None):
        bits = struct.unpack('<I', program.read(high, 4))[0] & 0xffff
        bits <<= 16
        if low is not None:
            bits |= struct.unpack('<I', program.read(low, 4))[0] & 0xffff
        return struct.unpack('<f', struct.pack('<I', bits))[0]

    return {'period': constant(0x4e5dac), 'split': constant(0x4e5dd0),
            'low': constant(0x4e5e00, 0x4e5e04) * -units['world_scale'],
            'span': constant(0x4e5df8, 0x4e5dfc) * -units['world_scale'],
            'high': constant(0x4e5e3c, 0x4e5e40) * -units['world_scale'],
            'world_units': units,
            'initial_phases': [constant(0x4dcd5c), 0],
            'frame_rate': 60, 'clock': 'Native fixed-tick adapter',
            'provenance': {'update': '0x4e5da0', 'initialize': '0x4dcd5c..74',
                           'callbacks': ['0x4e5e70', '0x4e5eb0']}}


def suspension_nodes(profile, cycle):
    callbacks = cycle['provenance']['callbacks']
    return {node: {**{key: cycle[key] for key in ('period', 'split', 'low', 'span', 'high', 'frame_rate')},
                   'phase': cycle['initial_phases'][callbacks.index(callback)],
                   'callback': callback}
            for node, callback in enumerate(profile['node_callbacks']) if callback in callbacks}


def read_suspension(disc):
    program = ProgramData(disc)
    return decode_suspension(program, world_units(program))
