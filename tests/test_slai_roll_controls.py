import struct
import unittest

from phantomcrash_research.slai.roll_controls import decode_roll_controls, roll_nodes


class Program:
    words = {0x2548c8: 0x86020006, 0x4e5b14: 0x000218c0,
             0x4e5b24: 0x2442a7f0, 0x4e5b28: 0x26060c10,
             0x4e5bb8: 0x26060c14, 0x4e5c48: 0x26060c18, 0x4e5cd8: 0x26060c1c,
             0x4e5500: 0x266500c0, 0x4e5504: 0x0c043a8e, 0x4e5508: 0x266600d0,
             0x4e5d68: 0x26060c10, 0x4e5d74: 0x0c13952c,
             0x4e5d78: 0x26070c14, 0x4dcd0c: 0x26040c20,
             0x4dcd48: 0x0c043aca, 0x4e5598: 0x16000045,
             0x4e5a08: 0x0c043b1c, 0x4e5a18: 0x0c043af2,
             0x4e5a28: 0x0c043b46, 0x4e5a34: 0x0c043ab0,
             0x4e5a38: 0x26440050, 0x4e5a4c: 0x24450110,
             0x4e5a88: 0x26440080, 0x4e5a90: 0x27a500b0}
    words.update({0x4e55f0: 0x3463fa35, 0x4e55f4: 0xc662008c,
                  0x4e55fc: 0xc6600e18, 0x4e5614: 0x46010042,
                  0x4e56dc: 0xc6610e18, 0x4e56fc: 0x46000d43,
                  0x4de7dc: 0xe6200e18})

    def read(self, address, size):
        if address == 0x59a7f0:
            return struct.pack('<70f', *[value for i in range(35) for value in (i+.25, i+.5)])
        return struct.pack('<I', self.words[address])


class RollControlsTests(unittest.TestCase):
    def test_all_model_rows_and_accumulators_are_retained_without_spherical_aliasing(self):
        controls = decode_roll_controls(Program(), {'world_scale': 1})
        self.assertEqual(controls['source_units_per_native'], 1)
        self.assertEqual(len(controls['coefficients']), 35)
        callbacks = ['', '0x4e5ad0', '0x4e5ad0', '0x4e5b60', '0x4e5bf0', '0x4e5c80', '0x4e5d10']
        for index in (0, 17, 30, 34):
            nodes = roll_nodes({'index':index, 'node_callbacks':callbacks}, controls)
            self.assertEqual(set(nodes), {1,2,3,4,5,6})
            self.assertEqual([nodes[i]['group'] for i in range(1,6)], [0,0,1,2,3])
            self.assertEqual([nodes[i]['coefficient'] for i in range(1,6)],
                             [index+.25]*3 + [index+.5]*2)
            self.assertTrue(nodes[6]['spherical'])
            self.assertEqual(nodes[6]['group'], 0)
            self.assertEqual(nodes[6]['coefficient'], index+.25)
            self.assertFalse(any(nodes[i]['spherical'] for i in range(1,6)))

    def test_different_model_selector_is_rejected(self):
        program = Program()
        program.words = {**program.words, 0x2548c8: 0}
        with self.assertRaises(ValueError):
            decode_roll_controls(program, {'world_scale': 1})

    def test_world_displacement_conversion_is_independent_of_robot_mesh_units(self):
        controls = decode_roll_controls(Program(), {'world_scale': 2})
        self.assertEqual(controls['source_units_per_native'], .5)

    def test_spherical_initialization_matrix_order_and_position_restore_are_guarded(self):
        for address in (0x4dcd48, 0x4e5d78, 0x4e5a08, 0x4e5a18,
                        0x4e5a28, 0x4e5a38, 0x4e5a4c, 0x4e5a90):
            program = Program()
            program.words = {**program.words, address: 0}
            with self.assertRaises(ValueError):
                decode_roll_controls(program, {'world_scale': 1})

    def test_steering_input_and_spherical_clock_cancellation_are_guarded(self):
        controls = decode_roll_controls(Program(), {'world_scale': 1})
        profile = {'index': 0, 'node_callbacks': ['0x4e5ad0', '0x4e5d10']}
        nodes = roll_nodes(profile, controls)
        for node in nodes.values():
            self.assertAlmostEqual(node['steering_radians_per_degree'], .01745329238474369)
            self.assertEqual(node['native_clock_rate'], 60)
        for address in (0x4e55f4, 0x4e55fc, 0x4e5614, 0x4e56fc, 0x4de7dc):
            program = Program()
            program.words = {**program.words, address: 0}
            with self.assertRaises(ValueError):
                decode_roll_controls(program, {'world_scale': 1})
