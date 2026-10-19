"""Validate melee decoder gates against the user's shipped executable."""
from pathlib import Path
import unittest

from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.melee import melee_actions


class MeleeTests(unittest.TestCase):
    def setUp(self):
        image = Path('C:/Projects/SLAI/SLAI.iso')
        if not image.exists():
            self.skipTest('Local user disc is unavailable')
        self.disc = SlaiDisc(image)
        self.disc.__enter__()
        self.addCleanup(self.disc.__exit__, None, None, None)
        self.program = ProgramData(self.disc)

    def test_original_kind_side_and_completed_lock_choose_distinct_actions(self):
        result = melee_actions(self.program)
        self.assertEqual(result['families']['6'], {'free': [39, 40], 'locked': [41, 42]})
        self.assertEqual(result['families']['7'], {'free': [35, 36], 'locked': [37, 38]})
        self.assertEqual(list(result['native_slots'].values()), list(range(37, 45)))

    def test_changed_target_or_action_flag_is_rejected(self):
        base = self.program
        for address in (0x22ce90, 0x50664c, 0x4e9818, 0x4e9be4, 0x511384):
            class ChangedProgram:
                def read(self, location, size):
                    return bytes(size) if location == address else base.read(location, size)
            with self.subTest(address=hex(address)), self.assertRaises(ValueError):
                melee_actions(ChangedProgram())
