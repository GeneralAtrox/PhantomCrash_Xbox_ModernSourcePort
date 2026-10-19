"""Guarded bank routing and motion effect triggers from the shipped program."""
from pathlib import Path
import json
import unittest

from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.motion_effects import motion_effect_contract, leg_action_route
from phantomcrash_research.slai.motion_resources import native_motion_actions
from phantomcrash_research.slai.action_bank import weapon_state_contract


class MotionEffectsTests(unittest.TestCase):
    def setUp(self):
        image = Path('C:/Projects/SLAI/SLAI.iso')
        if not image.exists():
            self.skipTest('Local user disc is unavailable')
        self.disc = SlaiDisc(image)
        self.disc.__enter__()
        self.addCleanup(self.disc.__exit__, None, None, None)
        self.program = ProgramData(self.disc)

    def test_compiled_leg_record_order_selects_authored_banks(self):
        manifest = Path('mods/slai/native_particles.json')
        if not manifest.exists():
            self.skipTest('Local converted banks are unavailable')
        entries = json.loads(manifest.read_text(encoding='utf-8'))['action_banks']
        expected = {0: 'photon2legs', 2: 'photon6legs', 3: 'photon3wheel',
                    6: 'brineman2legs', 12: 'holy2legs', 18: 'kubel2legs',
                    24: 'aeron2legs', 30: 'boss1', 33: 'drones', 34: 'sweeper'}
        for record, name in expected.items():
            self.assertEqual(leg_action_route(self.program, entries, record)['source_name'], name)
        routes = [leg_action_route(self.program, entries, i) for i in range(35)]
        self.assertEqual(len({r['bank'] for r in routes}), 35)
        with self.assertRaises(ValueError):
            leg_action_route(self.program, entries, 90)
        with self.assertRaises(ValueError):
            leg_action_route(self.program, entries + entries, 0)

    def test_changed_clock_wrap_or_entry_branch_is_rejected(self):
        self.assertEqual(motion_effect_contract(self.program)['supported_flags'], [6, 7, 38, 39])
        base = self.program
        for address in (0x507b70, 0x50e840, 0x50eb84, 0x50ebc4, 0x50ec3c, 0x508214):
            class ChangedProgram:
                def read(self, location, size):
                    return bytes(size) if location == address else base.read(location, size)
            with self.subTest(address=hex(address)), self.assertRaises(ValueError):
                motion_effect_contract(ChangedProgram())

    def test_death_gate_and_translation_override_reject_changed_source_branches(self):
        contract = motion_effect_contract(self.program)
        self.assertIn('owned resource-27 count', contract['special_27'])
        self.assertIn('model-object translation', contract['death_attachment'])
        base = self.program
        for address in (0x50ec18, 0x50ec24, 0x5094dc, 0x508764, 0x19c190):
            class ChangedProgram:
                def read(self, location, size):
                    return bytes(size) if location == address else base.read(location, size)
            with self.subTest(address=hex(address)), self.assertRaises(ValueError):
                motion_effect_contract(ChangedProgram())

    def test_part_loss_gate_is_state_two_and_independent_of_fire(self):
        contract = weapon_state_contract(self.program)
        self.assertEqual((contract['flags'], contract['required_state']), (256, 2))
        base = self.program
        for address in (0x50eadc, 0x50eae8, 0x50d03c, 0x4e11fc, 0x4e42f0):
            class ChangedProgram:
                def read(self, location, size):
                    return bytes(size) if location == address else base.read(location, size)
            with self.subTest(address=hex(address)), self.assertRaises(ValueError):
                weapon_state_contract(ChangedProgram())

    def test_untraced_motion_slots_do_not_manufacture_source_actions(self):
        actions = native_motion_actions()
        self.assertEqual(len(actions), 45)
        self.assertEqual(actions[28], 47)
        self.assertEqual(actions[37:], list(range(35, 43)))
        self.assertEqual(actions[3], -1)
