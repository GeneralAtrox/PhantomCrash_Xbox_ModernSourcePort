"""Original arm-action selection and cue-177 lock/animation gate."""

import struct


def melee_actions(program):
    guards = {
        0x504bd8: 0x0c0935d8, 0x504be0: 0xae0200a8,
        0x22ce90: 0xa0430dba, 0x22d060: 0xa0400dba,
        0x50664c: 0x90420dba, 0x50665c: 0xa2220c80,
        0x5066c0: 0xa2200c80, 0x5066dc: 0x0c13a5c4,
        0x4e972c: 0x90a30024, 0x4e9748: 0x92430c80,
        0x4e975c: 0x90840018,
        0x4e9764: 0x24030025, 0x4e9768: 0x24030029,
        0x4e97c0: 0x24030023, 0x4e97c4: 0x24030027,
        0x4e9818: 0xa243019c,
        0x4e983c: 0x24030026, 0x4e9840: 0x2403002a,
        0x4e9898: 0x24030024, 0x4e989c: 0x24030028,
        0x4e98f0: 0xa243019c,
        0x4e9aa4: 0x30430500, 0x4e9be4: 0xa240019c,
        0x511384: 0x92a3019c, 0x511388: 0x1060008f,
    }
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported source melee action/gate at {address:#x}')
    return {'families': {'6': {'free': [39, 40], 'locked': [41, 42]},
                         '7': {'free': [35, 36], 'locked': [37, 38]}},
            'native_slots': {str(action): 37 + action - 35 for action in range(35, 43)},
            'cue_177': 'requires actor +0x19c, set by a free arm action, not by a locked lunge',
            'provenance': {'parameter_pointer': '0x504bd8..504be0;0x24d760',
                           'completed_lock': '0x22ce30..22ce90;0x22d030',
                           'action_selection': '0x506640..5066e0;0x4e9710',
                           'action_completion': '0x4e9a68..4e9bf4',
                           'sound_gate': '0x511378..5113c0'}}
