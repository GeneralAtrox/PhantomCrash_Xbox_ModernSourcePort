import struct
import unittest
from pathlib import Path

from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.supply_assets import read_supply_path, supply_assets, stage_supply_routes
from phantomcrash_research.slai.world_units import world_units


class SupplyAssetTests(unittest.TestCase):
    def test_curve_header_and_axes(self):
        header = struct.pack('<5h2xf4x', 1, 0, 0, 0, 0, 3.5)
        raw = header + struct.pack('<16f', *([1, 2, 3, 1] * 4))
        path = read_supply_path(raw, 2)
        self.assertEqual(path['segments'], [[[2, -4, -6]] * 4])
        self.assertEqual(path['source_header_float'], 3.5)
        with self.assertRaises(ValueError):
            read_supply_path(raw[:-1], 2)
        self.assertEqual(read_supply_path(bytes(20), 2)['segments'], [])

    def test_current_package_routes_and_period_aliases(self):
        image = Path('C:/Projects/SLAI/SLAI.iso')
        if not image.is_file():
            self.skipTest('Source disc unavailable')
        with SlaiDisc(image) as disc:
            program = ProgramData(disc)
            assets = supply_assets(disc, program, world_units(program))
        self.assertEqual(sum(len(rows) for rows in assets['routes'].values()), 18)
        self.assertEqual(sum(len(r['segments']) for rows in assets['routes'].values() for r in rows), 489)
        self.assertEqual(stage_supply_routes(assets, 'DAY_TOKYO2.BG'),
                         stage_supply_routes(assets, 'NIGHT_TOKYO.BG'))
        self.assertEqual(len(stage_supply_routes(assets, 'DAY_ENGLAND.BG')), 3)
        self.assertEqual(len(stage_supply_routes(assets, 'PRACTICE.BG')), 1)
        self.assertEqual(stage_supply_routes(assets, 'MOTION.BG'), [])
        self.assertEqual(assets['pulse']['colour_steps'], 96)
