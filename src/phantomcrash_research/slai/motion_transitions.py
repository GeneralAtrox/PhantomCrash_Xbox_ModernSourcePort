"""Original secondary-channel transition counters for mapped actor actions."""

import struct


# Native action, source action, source t1 producer, expected instruction.
# Turns have a leg-kind/state-dependent producer and are deliberately omitted.
PRODUCERS = ((0, 0, 0x4ea9b0, 0x24090008),
             (1, 2, 0x4ea68c, 0x00a0482d),
             (2, 1, 0x4ea768, 0x24090008),
             (5, 4, 0x4eac4c, 0x24090002),
             (10, 20, 0x4eb5b0, 0x24090003),
             (11, 22, 0x4e9458, 0x24090005),
             (12, 46, 0x4f3e3c, 0x2409000a),
             (28, 47, 0x4ea1d8, 0x2409000c))


def decode_motion_transitions(program):
    guards = {0x4ea670: 0x24050002,
              0x1b43c0: 0xa65100e0, 0x1b43c4: 0xae5300e8,
              0x1b43d8: 0xae4000ec,
              0x1b4818: 0x8e0300e8, 0x1b481c: 0x8e0200ec,
              0x1b4820: 0x0043082b,
              0x1b48a8: 0x24420001, 0x1b48b0: 0xae0200ec,
              0x19f0fc: 0x8e420094, 0x19f10c: 0x78460000,
              0x19f2fc: 0x8c4400e8, 0x19f300: 0x8c4200ec,
              0x19f364: 0x46000d03,
              0x4ebba0: 0x24030016, 0x4ebba8: 0x2403001c,
              0x4ebbc4: 0x2409000a, 0x4ebbcc: 0x24090001,
              0x4ebbd0: 0x24020008, 0x4ebbd4: 0x0043480b,
              0x2548c8: 0x86020006,
              0x1b4888: 0xa60300de, 0x1b488c: 0xa60200e0}
    # The R5900 quadword instruction is guarded as a raw word; no host
    # disassembler interpretation is used to read their vectors.
    for native, source, address, word in PRODUCERS:
        guards[address] = word
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported source motion transition at {address:#x}')
    frames = [0] * 45
    for native, source, address, word in PRODUCERS:
        frames[native] = 2 if native == 1 else word & 0xffff
    immediate = lambda address: struct.unpack('<I', program.read(address, 4))[0] & 0xffff
    turns = {'special_alternate_models': [immediate(0x4ebba0), immediate(0x4ebba8)],
             'special_frames': immediate(0x4ebbc4), 'idle_frames': immediate(0x4ebbcc),
             'moving_frames': immediate(0x4ebbd0),
             'record_model': '0x2548c8: leg record +6 (alternate model)',
             'producer': '0x4ebb90..4ebbd4: alternate model / actor state +0x180'}
    return {'native_transition_frames': frames, 'turns': turns,
            'producers': [{'native_motion': native, 'source_action': source,
                           'address': hex(address), 'frames': frames[native]}
                          for native, source, address, word in PRODUCERS],
            'counter': '0x1b4818..1b48b0',
            'secondary_start': '0x1b43c0..1b4400;0x19ecc0..19ee18',
            'previous_pose_feedback': '0x19f0e8..19f140;0x19f3f8..19f424',
            'weight': '0x19f2f8..19f3f4',
            'promotion': '0x1b4848..1b48a4;0x19ee20..19ee40',
            'pending': ['unmapped actor states',
                        'non-blending queued secondary actions', 'original update cadence']}


def turn_transition_frames(alternate_model, contract):
    turns = contract['turns']
    if alternate_model in turns['special_alternate_models']:
        return [turns['special_frames']] * 2
    return [turns['idle_frames'], turns['moving_frames']]
