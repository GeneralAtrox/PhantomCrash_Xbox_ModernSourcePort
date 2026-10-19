"""Source branch validation for robot-shared paired weapon sounds."""

import struct
import unittest

from phantomcrash_research.slai.fire_sound import fire_cycles, fire_gate, fire_sustain


class Program:
    def __init__(self):
        self.words = {
            0x511238: 0x240300A6, 0x511240: 0x240300C0,
            0x51124C: 0x240300C5, 0x511254: 0x240300CA,
            0x511260: 0x240300EB, 0x511404: 0x240300A6,
            0x511410: 0x240300C0, 0x511418: 0x240300C5,
            0x511424: 0x240300CA, 0x51142C: 0x240300EB,
            0x5112B0: 0x240200C5, 0x511294: 0x26660001,
            0x5112E0: 0x26660001, 0x511354: 0x26660001,
            0x511594: 0x8EA5000C, 0x511598: 0x26660001,
            0x51158C: 0x0C142960, 0x5115A0: 0x0C142960,
            0x5115AC: 0x26660002, 0x5115C0: 0x0C142A58,
            0x511280: 0x2407FFFF, 0x5112A0: 0x2407FFFF,
            0x511340: 0x2407FFFF,
            0x5060ac: 0xa0640014, 0x505c4c: 0x0c141828,
            0x506148: 0x0c141828, 0x5064dc: 0x0c141828,
            0x50e584: 0x0c144414, 0x50e590: 0x0c143c90,
            0x50f710: 0x90630014, 0x50f71c: 0xa0a00000,
            0x51112c: 0x90630014, 0x511148: 0xc44100d8,
            0x511154: 0x46000834, 0x51144c: 0x8ea20e34,
            0x5114bc: 0xc44100d8, 0x5114c8: 0x46000836,
            0x505218: 0xc6000064, 0x50521c: 0x46800020,
            0x505220: 0xe60000d8, 0x505ec8: 0xe6a000d8,
            0x59b684: 0x005060b0,
            0x5111a0: 0x240300f7, 0x5111a8: 0x240300d1, 0x5111b4: 0x240300e4,
            0x5111fc: 0x0c142ab0, 0x511200: 0x0200382d,
            0x511208: 0x146000ef, 0x511218: 0x0200482d, 0x511228: 0x0c142a58,
            0x5061ec: 0x3c024100, 0x5061fc: 0x46000834, 0x506220: 0xe66000ac,
            0x506300: 0xc66100ac, 0x506330: 0xe66000ac, 0x506338: 0xc66100ac,
            0x506344: 0x46000836, 0x506368: 0x10800009, 0x506370: 0x0c13fccc,
            0x506378: 0xae60001c, 0x5063a0: 0x0c142a2c,
            0x50a8d0: 0x240600d1, 0x50a8e4: 0x240600f7, 0x50a8f8: 0x240600e4,
            0x50a90c: 0x240600d2, 0x50a920: 0x240600f8, 0x50a93c: 0x240600e5,
        }

    def read(self, at, size):
        assert size == 4
        return struct.pack('<I', self.words[at])


class FireSoundTests(unittest.TestCase):
    def test_sustained_mounts_use_accepted_shots_and_the_beam_release_counter(self):
        cues = fire_sustain(Program())
        self.assertEqual(list(cues), [247, 209, 228])
        self.assertEqual(cues[209]['fade_frames'], 8)
        self.assertEqual(cues[228]['stop_cues'], [209, 247, 228, 210, 248, 229])
        self.assertIn('mount', cues[247]['ownership'])
        for at in (0x59b684, 0x511200, 0x511208, 0x5061ec, 0x506344, 0x5063a0, 0x50a920):
            program = Program()
            program.words[at] = 0
            with self.assertRaises(ValueError):
                fire_sustain(program)

    def test_start_pulses_and_held_retention_have_distinct_ammo_boundaries(self):
        gate = fire_gate(Program())
        self.assertIn('accepted shot', gate['activation'])
        self.assertIn('ammo > 0', gate['pair_retention'])
        self.assertIn('not charge', gate['ammo_field'])
        for at in (0x5060ac, 0x50f71c, 0x51144c, 0x5114c8, 0x505220):
            program = Program()
            program.words[at] = 0
            with self.assertRaises(ValueError):
                fire_gate(program)

    def test_original_branches_identify_start_secondary_and_ending_members(self):
        cycles = fire_cycles(Program())
        self.assertEqual(list(cycles), [166, 192, 197, 202, 235])
        self.assertEqual([k for k, v in cycles.items() if v['start_together']], [197])
        self.assertEqual(cycles[235]['secondary_cue'], 236)
        self.assertEqual(cycles[235]['ending_cue'], 237)

    def test_changed_owner_or_ending_branch_cannot_be_silently_imported(self):
        for at, replacement in [(0x511280, 0x24070000),
                                (0x5115AC, 0x26660001),
                                (0x511418, 0x240300C0)]:
            program = Program()
            program.words[at] = replacement
            with self.assertRaises(ValueError):
                fire_cycles(program)
