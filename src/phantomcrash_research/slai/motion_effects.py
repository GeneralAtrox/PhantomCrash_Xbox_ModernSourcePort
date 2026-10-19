"""Compiled leg ASB selection and primary motion-frame trigger contract."""
import struct

from .sound_cues import constant_case


def motion_effect_contract(program):
    guards = {
        0x507b60: 0x2ca1005a, 0x507b70: 0x2463aaf0,
        0x509a60: 0x0c141ed8, 0x509a70: 0x2442cdb0,
        0x2513f0: 0x0c094184, 0x251400: 0x2406ffff,
        0x251408: 0x0c142688,
        0x50e840: 0x8ca201a4, 0x50e85c: 0x0c06d160,
        0x50e950: 0x846300de, 0x50e964: 0x8e83027c,
        0x50e968: 0x14600217, 0x50eb1c: 0x80640004,
        0x50eb5c: 0xae830264, 0x50eb64: 0xae830268,
        0x50eb84: 0xe6940264, 0x50eb98: 0xe6940268,
        0x50eba0: 0x4614a836, 0x50ebb0: 0x0c06d12c,
        0x50ebbc: 0xc44100e4, 0x50ebc4: 0x46000834,
        0x50ebdc: 0x4600a580, 0x50ec3c: 0x30830001,
        0x50ec58: 0x30830004, 0x50ec68: 0x4600a834,
        0x50ec74: 0x4600b034,
        0x508210: 0x24020100, 0x508214: 0x10c20006,
        0x508218: 0x24020080, 0x508234: 0x80a20002,
        0x508238: 0x80a20001, 0x50ebf4: 0x2403001b,
        0x50ec00: 0x82c30041, 0x50ec10: 0x0c142530,
        0x50ec18: 0x1c40001f, 0x50ec24: 0x4600b034,
        0x5094cc: 0x30630001, 0x5094dc: 0x04600008,
        0x5094e4: 0x1465000a, 0x5094ec: 0x8d030004,
        0x509514: 0x28e30040, 0x508764: 0x24030400,
        0x508768: 0x26e50040, 0x19c190: 0x0c043aca,
    }
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported source motion effect gate at {address:#x}')
    return {'supported_flags': [6, 7, 38, 39],
            'clock': 'leg model primary action/frame; original secondary clock takes precedence when active',
            'entry': 'bit 1 overrides bit 4; first matching update only',
            'crossing': 'previous < authored_frame <= current; on wrap add duration only if previous+speed >= duration',
            'retention': 'bit 0x20 skips all record work while its last effect exists',
            'alternate': 'actor +0xdf4 exactly 0x80 or 0x100 selects alternate; native producer pending',
            'special_27': 'bank mode zero bypasses ordinary crossings: owned resource-27 count > 0 or adjusted frame >= key',
            'death_attachment': 'resource 27 overrides parent mask to 0x400 and follows model-object translation; joint matrix is discarded at 0x19c190',
            'bank_mode': 'mode 1 suppresses non-27 descriptors after trigger bookkeeping; original byte +0x41 producer pending',
            'negative_parent': 'two descriptors use -1; direct node getter 0x1b3c80 has no negative guard; allocation/producer semantics pending',
            'provenance': {'leg_record': '0x2513f0..251408;0x250610',
                           'bank_selector': '0x507b60;0x5baaf0;0x59cdb0;0x509a60',
                           'clock': '0x50e840..50e86c;0x1b4580',
                           'triggers': '0x50e92c..50ec90'}}


def leg_action_route(program, entries, record):
    if not 0 <= record < 90:
        raise ValueError('Invalid source leg record')
    leaf = struct.unpack('<I', program.read(0x5baaf0 + record * 4, 4))[0]
    if not 0x507b88 <= leaf <= 0x507ca8:
        raise ValueError('Unsupported source leg bank selector')
    kind = constant_case(program, leaf)
    if not 0 <= kind < 35:
        raise ValueError('Missing source leg bank kind')
    name = program.string(struct.unpack('<I', program.read(0x59cdb0 + kind * 4, 4))[0])
    matches = [e for e in entries if e['source'] == name.upper() + '.ASB']
    if len(matches) != 1:
        raise ValueError(f'Missing or ambiguous source leg action bank {name}')
    return {'leg_kind': kind, 'bank': 'slai/assets/' + matches[0]['file'],
            'source_name': name, 'selector_address': hex(leaf),
            'loader_address': '0x509a60'}
