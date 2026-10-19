import struct
import unittest

from phantomcrash_research.slai.suspension import decode_suspension, suspension_nodes


class Program:
    words = {0x4dcd5c: 0x3c024080, 0x4dcd70: 0xae020b38, 0x4dcd74: 0xae000b3c,
             0x4e5db8: 0x46011040, 0x4e5dc8: 0xaca00000, 0x4e5e20: 0x46010000,
             0x4e5e60: 0x46010000, 0x4e5e94: 0x24a50b38, 0x4e5ed4: 0x24a50b3c,
             0x4e5dac: 0x3c034100, 0x4e5dd0: 0x3c034080,
             0x4e5e00: 0x3c03bd4c, 0x4e5e04: 0x3463cccd,
             0x4e5df8: 0x3c033e19, 0x4e5dfc: 0x3465999a,
             0x4e5e3c: 0x3c033dcc, 0x4e5e40: 0x3463cccd,
             0x4e5e0c: 0xc4800084, 0x4e5e28: 0xe4800084,
             0x4e5e4c: 0xc4800084, 0x4e5e64: 0xe4800084}

    def read(self, address, size):
        return struct.pack('<I', self.words[address])


class SuspensionTests(unittest.TestCase):
    def test_original_constants_convert_y_axis_and_keep_independent_counter_phases(self):
        cycle = decode_suspension(Program(), {'world_scale': 1})
        self.assertEqual((cycle['period'], cycle['split']), (8, 4))
        self.assertAlmostEqual(cycle['low'], .05)
        self.assertAlmostEqual(cycle['span'], -.15)
        self.assertAlmostEqual(cycle['high'], -.1)
        nodes = suspension_nodes({'node_callbacks':['', '0x4e5eb0', '0x4e5ad0', '0x4e5e70']}, cycle)
        self.assertEqual(set(nodes), {1,3})
        self.assertEqual([nodes[i]['phase'] for i in (1,3)], [0,4])

    def test_different_counter_producer_is_rejected(self):
        program = Program()
        program.words = {**program.words, 0x4dcd74: 0}
        with self.assertRaises(ValueError):
            decode_suspension(program, {'world_scale': 1})

    def test_world_y_offset_uses_the_world_unit_conversion(self):
        cycle = decode_suspension(Program(), {'world_scale': 2})
        self.assertAlmostEqual(cycle['low'], .1)
        self.assertAlmostEqual(cycle['span'], -.3)
