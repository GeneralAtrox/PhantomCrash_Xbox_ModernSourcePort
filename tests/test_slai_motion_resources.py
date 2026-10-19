"""Original compiled action lookup, aliases and body/leg package ownership."""

import struct
import unittest

from phantomcrash_research.slai.motion_resources import (
    decode_motion_resources, body_leg_profiles, native_clip_names, rolling_nodes, body_attachment_nodes)


class CompiledProgram:
    def __init__(self):
        self.data = {0x250f98: struct.pack('<I', 0x2ae2003b),
                     0x250d24: struct.pack('<I', 0x00041140),
                     0x250d28: struct.pack('<I', 0x00441023),
                     0x250d30: struct.pack('<I', 0x00621023),
                     0x2525e8: struct.pack('<I', 0x8c460088),
                     0x2525e4: struct.pack('<I', 0x0260282d),
                     0x268364: struct.pack('<I', 0x86660154),
                     0x2684a4: struct.pack('<I', 0x86660156),
                     0x2685e4: struct.pack('<I', 0x86660158),
                     0x268724: struct.pack('<I', 0x8666015a),
                     0x4ef2cc: struct.pack('<I', 0x24050016),
                     0x4e0b18: struct.pack('<I', 0x2405002e),
                     0x4eb59c: struct.pack('<I', 0x24050014),
                     0x4eb5ac: struct.pack('<I', 0x0c13a528),
                     0x2689b4: struct.pack('<I', 0x86060154),
                     0x4e8fb8: struct.pack('<I', 0xc60c0e18),
                     0x4e8ff4: struct.pack('<I', 0xe6000e24),
                     0x4ebbd8: struct.pack('<I', 0xc6000e24),
                     0x4ebbe4: struct.pack('<I', 0x460c0034),
                     0x4ebbf0: struct.pack('<I', 0x2405002d),
                     0x4ebbfc: struct.pack('<I', 0x0c13a528),
                     0x4ebc14: struct.pack('<I', 0x2405002c),
                     0x4ebc20: struct.pack('<I', 0x0c13a528),
                     0x1b2a9c: struct.pack('<I', 0x16420004),
                     0x1b2aa4: struct.pack('<I', 0x0c043aca),
                     0x1b56e8: struct.pack('<I', 0x00c23026),
                     0x1b56ec: struct.pack('<I', 0x0006302b),
                     0x19f480: struct.pack('<I', 0x14620005),
                     0x19f48c: struct.pack('<I', 0x0c043aa0),
                     0x19f4cc: struct.pack('<I', 0x14620006),
                     0x333220: struct.pack('<8I', 0x10000, 0x20000, 0x30000, 0x30000,
                                          0x40000, 0x40000, 0, 0)}
        self.strings = {}
        self.row(0x10000, 'drones', 'S9L01H', 'S9L01H')
        self.row(0x10000+372, 'photon4legs', 'S1L02H', 'S1L02H')
        # Body order is independent of leg order, and Photon uses a Holy
        # motion prefix in the shipped table despite retaining its own mesh.
        self.row(0x20000, 'photon4legs', 'S1B01H', 'S3B05H')
        self.row(0x20000+372, 'drones', 'S9B01H', 'S9B01H')
        self.row(0x30000, 'arm', 'S1A01H', 'S1A01H')
        self.row(0x40000, 'shoulder', 'S1S01H', 'S1S01H')

    def row(self, address, folder, model, prefix):
        row = bytearray(372)
        for index, value in enumerate((folder, model, prefix)):
            pointer = address+0x80000+index*128
            self.strings[pointer] = value
            struct.pack_into('<I', row, index*4, pointer)
        numbers = [-1]*59
        for action, number in {0:1, 1:2, 2:3, 4:5, 20:21, 22:24, 35:37, 36:37,
                               44:47, 45:48, 46:49, 47:50}.items():
            numbers[action] = number
        struct.pack_into('<59h', row, 12, *numbers)
        for node, callback in ((1, 0x4e43a0), (2, 0x4e5ad0), (3, 0x4e5b60),
                               (4, 0x4e5bf0), (5, 0x4e5c80), (6, 0x4e5d10)):
            struct.pack_into('<I', row, 0x88+node*4, callback)
        struct.pack_into('<16h', row, 0x154, 3, 5, 23, 24, *([-1]*12))
        self.data[address] = bytes(row)

    def read(self, address, size):
        return self.data.get(address, bytes(size))

    def string(self, address):
        return self.strings.get(address, '')


class MotionResourcesTests(unittest.TestCase):
    def test_compiled_action_aliases_and_disabled_slots_are_preserved(self):
        resources = decode_motion_resources(CompiledProgram())
        leg = resources['tables']['legs'][0]
        self.assertEqual(len(leg['source_action_clips']), 59)
        self.assertEqual(leg['source_action_clips'][35:37], ['S9L01H37']*2)
        self.assertEqual(leg['source_action_clips'][3], '')
        names = native_clip_names(leg)
        self.assertEqual(len(names), 45)
        self.assertEqual(names[37:45], leg['source_action_clips'][35:43])
        self.assertEqual([names[i] for i in (0,1,2,5,28)],
                         ['S9L01H01','S9L01H03','S9L01H02','S9L01H05','S9L01H50'])
        self.assertEqual([names[i] for i in (11,12)], ['S9L01H24','S9L01H49'])
        self.assertEqual(names[10], 'S9L01H21')
        self.assertEqual([names[i] for i in (8,9)], ['S9L01H47','S9L01H48'])

    def test_body_package_matching_uses_the_folder_and_authored_motion_prefix(self):
        resources = decode_motion_resources(CompiledProgram())
        pairs = body_leg_profiles(resources)
        self.assertEqual([(leg['model'], body['model']) for leg,body in pairs],
                         [('S9L01H.PO2','S9B01H.PO2'), ('S1L02H.PO2','S1B01H.PO2')])
        self.assertEqual(native_clip_names(pairs[1][1])[28], 'S3B05H50')

    def test_a_different_loader_is_rejected_before_using_fixed_table_addresses(self):
        program = CompiledProgram()
        program.data[0x250f98] = bytes(4)
        with self.assertRaises(ValueError):
            decode_motion_resources(program)

    def test_jump_selection_rejects_a_changed_action_or_dispatcher(self):
        for address in (0x4eb59c, 0x4eb5ac, 0x2689b4):
            program = CompiledProgram()
            program.data[address] = bytes(4)
            with self.assertRaises(ValueError):
                decode_motion_resources(program)

    def test_turn_selection_rejects_a_changed_input_direction_or_action(self):
        for address in (0x4e8fb8, 0x4e8ff4, 0x4ebbe4, 0x4ebbf0, 0x4ebc14):
            program = CompiledProgram()
            program.data[address] = bytes(4)
            with self.assertRaises(ValueError):
                decode_motion_resources(program)

    def test_root_translation_exclusion_rejects_a_changed_control_or_matrix_path(self):
        for address in (0x1b2a9c, 0x1b2aa4, 0x1b56e8, 0x19f480, 0x19f48c):
            program = CompiledProgram()
            program.data[address] = bytes(4)
            with self.assertRaises(ValueError):
                decode_motion_resources(program)

    def test_roll_assignment_uses_compiled_callbacks_and_keeps_other_controls_separate(self):
        row = decode_motion_resources(CompiledProgram())['tables']['legs'][0]
        self.assertEqual(len(row['node_callbacks']), 51)
        self.assertEqual(rolling_nodes(row), [2, 3, 4, 5, 6])
        self.assertEqual(row['node_callbacks'][1], '0x4e43a0')
        self.assertEqual(row['node_callbacks'][6], '0x4e5d10')

    def test_body_mounts_use_compiled_nodes_and_keep_disabled_sides(self):
        row = decode_motion_resources(CompiledProgram())['tables']['bodies'][0]
        self.assertEqual(body_attachment_nodes(row),
                         {'arm_mount_nodes':[5,3], 'shoulder_mount_nodes':[24,23]})
        row['attachment_nodes'][:4] = [-1,2,-1,-1]
        self.assertEqual(body_attachment_nodes(row),
                         {'arm_mount_nodes':[2,-1], 'shoulder_mount_nodes':[-1,-1]})

    def test_disabled_jump_air_and_death_actions_are_not_replaced_by_filename_guesses(self):
        row = decode_motion_resources(CompiledProgram())['tables']['legs'][0]
        row['source_action_clips'][22] = ''
        row['source_action_clips'][20] = ''
        row['source_action_clips'][46] = ''
        names = native_clip_names(row)
        self.assertEqual([names[i] for i in (10,11,12)], ['', '', ''])
