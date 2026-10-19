"""Source transition producers, guarded decoding and native metadata delivery."""

from pathlib import Path
import unittest

from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.motion_resources import read_motion_resources
from phantomcrash_research.slai.motion_transitions import decode_motion_transitions, turn_transition_frames
from phantomcrash_research.slai.native_animation import articulated_model

DISC = Path('C:/Projects/SLAI/SLAI.iso')


@unittest.skipUnless(DISC.exists(), 'Local S.L.A.I. disc unavailable')
class MotionTransitionTests(unittest.TestCase):
    def test_original_actor_transition_lengths_and_conditional_turns(self):
        with SlaiDisc(DISC) as disc:
            contract = decode_motion_transitions(ProgramData(disc))
        frames = contract['native_transition_frames']
        self.assertEqual(len(frames), 45)
        self.assertEqual([frames[i] for i in (0, 1, 2, 5, 10, 11, 12, 28)],
                         [8, 2, 8, 2, 3, 5, 10, 12])
        self.assertEqual([frames[i] for i in (8, 9, 37, 44)], [0, 0, 0, 0])
        self.assertEqual(turn_transition_frames(21, contract), [1, 8])
        self.assertEqual(turn_transition_frames(22, contract), [10, 10])
        self.assertEqual(turn_transition_frames(28, contract), [10, 10])

    def test_changed_source_counter_or_producer_is_rejected(self):
        with SlaiDisc(DISC) as disc:
            program = ProgramData(disc)

            class Modified:
                def __init__(self, address):
                    self.address = address

                def read(self, address, size):
                    if address == self.address:
                        return bytes(size)
                    return program.read(address, size)

            for address in (0x4ea68c, 0x1b48a8, 0x19f10c, 0x19f364, 0x2548c8, 0x4ebbd4):
                with self.assertRaisesRegex(ValueError, 'motion transition'):
                    decode_motion_transitions(Modified(address))

    def test_all_body_profiles_pass_transition_data_to_native_models(self):
        with SlaiDisc(DISC) as disc:
            resources = read_motion_resources(disc)
        frames = resources['transitions']['native_transition_frames']
        source = {'nodes': [{}], 'scenes': [{'nodes': [0]}], 'asset': {'version': '2.0'},
                  'buffers': [{'uri': 'data:application/octet-stream;base64,', 'byteLength': 0}],
                  'meshes': []}
        bodies = resources['tables']['bodies']
        self.assertEqual(len(bodies), 35)
        for profile in bodies:
            native = articulated_model(source, '', profile['model'][:-4], profile=profile)
            self.assertEqual(native['asset']['extras']['source_transition_frames'], frames)


if __name__ == '__main__':
    unittest.main()
