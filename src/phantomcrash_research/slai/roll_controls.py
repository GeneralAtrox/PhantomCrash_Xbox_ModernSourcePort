"""Compiled per-model wheel coefficients and original accumulator ownership."""

import struct

from .motion_resources import ROLL_CALLBACKS, SPHERICAL_CALLBACK
from .program import ProgramData
from .world_units import world_units


def decode_roll_controls(program, units):
    guards = {0x2548c8: 0x86020006, 0x4e5b14: 0x000218c0,
              0x4e5b24: 0x2442a7f0, 0x4e5b28: 0x26060c10,
              0x4e5bb8: 0x26060c14, 0x4e5c48: 0x26060c18,
              0x4e5cd8: 0x26060c1c, 0x4e5500: 0x266500c0,
              0x4e5504: 0x0c043a8e, 0x4e5508: 0x266600d0,
              0x4e5d68: 0x26060c10, 0x4e5d74: 0x0c13952c,
              0x4e5d78: 0x26070c14, 0x4dcd0c: 0x26040c20,
              0x4dcd48: 0x0c043aca, 0x4e5598: 0x16000045,
              0x4e5a08: 0x0c043b1c, 0x4e5a18: 0x0c043af2,
              0x4e5a28: 0x0c043b46, 0x4e5a34: 0x0c043ab0,
              0x4e5a38: 0x26440050, 0x4e5a4c: 0x24450110,
              0x4e5a88: 0x26440080, 0x4e5a90: 0x27a500b0}
    guards.update({0x4e55f0: 0x3463fa35, 0x4e55f4: 0xc662008c,
                   0x4e55fc: 0xc6600e18, 0x4e5614: 0x46010042,
                   0x4e56dc: 0xc6610e18, 0x4e56fc: 0x46000d43,
                   0x4de7dc: 0xe6200e18})
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError('Unsupported S.L.A.I. wheel coefficient selector')
    values = struct.unpack('<70f', program.read(0x59a7f0, 280))
    return {'coefficients': [list(values[i:i+2]) for i in range(0, 70, 2)],
            'pi': struct.unpack('<f', bytes.fromhex('db0f4940'))[0],
            'max_step': struct.unpack('<f', bytes.fromhex('cabc4a40'))[0],
            'source_units_per_native': 1 / units['world_scale'],
            'steering_radians_per_degree': struct.unpack('<f', struct.pack('<I', 0x3c8efa35))[0],
            'native_clock_rate': 60,
            'world_units': units,
            'provenance': {'table': '0x59a7f0', 'model_selector': '0x254890',
                           'update': '0x4e54b0', 'axis_normalize': '0x10e8c0',
                           'steering': '0x4e55e0..56fc',
                           'pad_input': '0x4de778..7dc',
                           'native_clock_rate': 'Native seconds to frame-clock adapter; original cadence unverified'}}


def roll_nodes(profile, controls):
    coefficients = controls['coefficients'][profile['index']]
    groups = list(ROLL_CALLBACKS)
    return {node: {'group': 0 if int(callback, 0) == SPHERICAL_CALLBACK else groups.index(int(callback, 0)),
                   'coefficient': coefficients[0 if int(callback, 0) == SPHERICAL_CALLBACK
                                               else groups.index(int(callback, 0)) // 2],
                   'spherical': int(callback, 0) == SPHERICAL_CALLBACK,
                   'pi': controls['pi'], 'max_step': controls['max_step'],
                   'source_units_per_native': controls['source_units_per_native'],
                   'steering_radians_per_degree': controls['steering_radians_per_degree'],
                   'native_clock_rate': controls['native_clock_rate'],
                   'callback': callback}
            for node, callback in enumerate(profile['node_callbacks'])
            if callback and (int(callback, 0) in ROLL_CALLBACKS or int(callback, 0) == SPHERICAL_CALLBACK)}


def read_roll_controls(disc):
    program = ProgramData(disc)
    return decode_roll_controls(program, world_units(program))
