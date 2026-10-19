"""Recover original accepted-shot, paired and sustained weapon sound gates."""

import struct


def fire_gate(program):
    """Accepted-shot pulses start cues; held buttons only retain an existing pair."""
    guards = {
        0x5060ac: 0xa0640014,  # setter writes one to context +14 + mount
        0x505c4c: 0x0c141828, 0x506148: 0x0c141828, 0x5064dc: 0x0c141828,
        0x50e584: 0x0c144414,  # sound observer precedes marker reset
        0x50e590: 0x0c143c90,
        0x50f710: 0x90630014, 0x50f71c: 0xa0a00000,
        0x51112c: 0x90630014,  # current mount's accepted-shot marker
        0x511148: 0xc44100d8, 0x511154: 0x46000834,  # ammo < 0 rejects
        0x51144c: 0x8ea20e34,  # supporting mounts use held button bits
        0x5114bc: 0xc44100d8, 0x5114c8: 0x46000836,  # ammo <= 0 rejects
        0x505218: 0xc6000064, 0x50521c: 0x46800020,
        0x505220: 0xe60000d8, 0x505ec8: 0xe6a000d8,
    }
    for address, expected in guards.items():
        if program.read(address, 4) != struct.pack('<I', expected):
            raise ValueError(f'Unsupported weapon sound gate at {address:#x}')
    return {'activation': 'accepted shot in this update, with remaining ammo >= 0',
            'pair_retention': 'matching held mount with remaining ammo > 0',
            'mount_order': [0, 1, 2, 3],
            'ammo_field': 'weapon runtime +0xd8 (not charge)',
            'provenance': {'marker_setter': '0x5060a0',
                           'marker_consumption': '0x51112c..511168',
                           'marker_clear': '0x50f700..50f72c',
                           'held_input': 'actor +0xe34;0x51144c',
                           'ammo_initialization': '0x505218..505224',
                           'ammo_spending': '0x505eb4..505ec8'}}


def fire_cycles(program):
    def word(at):
        return struct.unpack('<I', program.read(at, 4))[0]

    def constant(at, register):
        raw = word(at)
        if raw >> 16 != 0x2400 + register:
            raise ValueError(f'Nonconstant weapon sound branch at {at:#x}')
        return raw & 65535

    active = [constant(at, 3) for at in
              (0x511238, 0x511240, 0x51124C, 0x511254, 0x511260)]
    stopped = [constant(at, 3) for at in
               (0x511404, 0x511410, 0x511418, 0x511424, 0x51142C)]
    together = constant(0x5112B0, 2)
    expected = {
        0x511294: 0x26660001,  # ADDIU A2,S3,1: second member
        0x5112E0: 0x26660001,
        0x511354: 0x26660001,
        0x511594: 0x8EA5000C,  # robot handle supplied to stop
        0x511598: 0x26660001,
        0x51158C: 0x0C142960,  # JAL 0x50a580, stop first
        0x5115A0: 0x0C142960,  # stop second
        0x5115AC: 0x26660002,  # ending member
        0x5115C0: 0x0C142A58,  # JAL 0x50a960, play ending
        0x511280: 0x2407FFFF,  # queries shared across mounts
        0x5112A0: 0x2407FFFF,
        0x511340: 0x2407FFFF,
    }
    if active != stopped or len(set(active)) != 5 or together not in active:
        raise ValueError('Weapon sound start/stop groups disagree')
    for at, expected_word in expected.items():
        if word(at) != expected_word:
            raise ValueError(f'Unsupported paired weapon sound instruction at {at:#x}')
    return {cue: {'group': cue, 'secondary_cue': cue+1, 'ending_cue': cue+2,
                  'start_together': cue == together,
                  'ownership': 'one pair per robot across all matching mounts',
                  'source_active_branch': '0x511270',
                  'source_stop_branch': '0x511438'} for cue in active}


def fire_sustain(program):
    """These three mount-owned cues stop with the beam, not on trigger release."""
    guards = {
        0x59b684: 0x005060b0,  # kind-5 weapon update callback
        0x5111a0: 0x240300f7, 0x5111a8: 0x240300d1, 0x5111b4: 0x240300e4,
        0x5111fc: 0x0c142ab0, 0x511200: 0x0200382d,
        0x511208: 0x146000ef, 0x511218: 0x0200482d,
        0x511228: 0x0c142a58,
        0x5061ec: 0x3c024100, 0x5061fc: 0x46000834,
        0x506220: 0xe66000ac, 0x506300: 0xc66100ac,
        0x506330: 0xe66000ac, 0x506338: 0xc66100ac,
        0x506344: 0x46000836, 0x506368: 0x10800009,
        0x506370: 0x0c13fccc, 0x506378: 0xae60001c,
        0x5063a0: 0x0c142a2c,
        0x50a8d0: 0x240600d1, 0x50a8e4: 0x240600f7, 0x50a8f8: 0x240600e4,
        0x50a90c: 0x240600d2, 0x50a920: 0x240600f8, 0x50a93c: 0x240600e5,
    }
    for address, expected in guards.items():
        if program.read(address, 4) != struct.pack('<I', expected):
            raise ValueError(f'Unsupported sustained weapon sound gate at {address:#x}')
    return {cue: {'ownership': 'one cue per robot and mount',
                  'activation': 'accepted shot; query current mount before play',
                  'stop_cues': [209, 247, 228, 210, 248, 229],
                  'fade_frames': 8,
                  'stop': 'weapon +0xac <= 0 releases its beam and stops mount cues',
                  'native_lifetime': 'live native beam, including release fade',
                  'provenance': {'activation': '0x5111a0..511234',
                                 'fade_accumulation': '0x5061ec..506220',
                                 'fade_decay': '0x506300..506330',
                                 'stop': '0x506338..5063a4',
                                 'mount_stop': '0x50a8b0'}} for cue in (247, 209, 228)}
