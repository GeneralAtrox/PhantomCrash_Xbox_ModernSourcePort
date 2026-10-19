"""Original PMT direction, cursor contract and exported interpolation endpoints."""

from pathlib import Path
import math
import unittest
import numpy as np

from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.motion_sampling import (
    decode_motion_sampling, motion_period, native_pmt_track, SAMPLER_WORDS)
from phantomcrash_research.slai.native_geometry import quaternion_matrix

DISC = Path('C:/Projects/SLAI/SLAI.iso')


class MotionSamplingTests(unittest.TestCase):
    def test_native_quaternion_matches_source_scalar_matrix_direction(self):
        q = np.array([.6, .2, -.3, .7])
        q /= np.linalg.norm(q)
        w, x, y, z = q
        source = np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(y*w+x*z)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        track = {'flags': 1, 'keys': [{'duration': 4, 'quaternion_wxyz': q.tolist()}]}
        native = native_pmt_track(track, np.eye(4).ravel())
        nx, ny, nz, nw = native['keys'][0]['rotation']
        actual = quaternion_matrix([nw, nx, ny, nz])[:3, :3]
        basis = np.diag([1, -1, -1])
        np.testing.assert_allclose(actual, basis @ source @ basis, atol=1e-6)
        # Body glTF exports [x,-y,z,w]; the native loader mirrors X/Y.
        np.testing.assert_allclose([nx, ny, nz, nw], [-x, y, z, w], atol=1e-6)

    def test_terminal_duration_is_preserved_but_not_counted_in_the_period(self):
        track = {'flags': 4, 'keys': [
            {'duration': 2, 'translation': [10, 20, -30]},
            {'duration': 3, 'translation': [20, 30, -40]},
            {'duration': 99, 'translation': [30, 40, -50]}]}
        bind = np.eye(4)
        bind[3, :3] = [5, -5, 10]
        out = native_pmt_track(track, bind.ravel())
        np.testing.assert_allclose(out['keys'][0]['position'], [1.5, -1.5, 2])
        self.assertEqual([k['duration'] for k in out['keys']], [2, 3, 99])
        self.assertEqual(motion_period({'tracks': [track]}), 5)
        self.assertEqual(motion_period({'tracks': [{'keys': []}]}), 0)

    def test_rigid_weapon_bind_scale_is_retained_instead_of_baked_twice(self):
        bind = np.eye(4)
        bind[:3, :3] *= .91
        track = {'flags': 1, 'keys': [{'duration': 3, 'quaternion_wxyz': [1, 0, 0, 0]}]}
        rigid = native_pmt_track(track, bind.ravel(), baked_bind_scale=False)
        leg = native_pmt_track(track, bind.ravel())
        np.testing.assert_allclose(np.array(rigid['bind']).reshape(4, 4)[:3, :3], np.eye(3)*.91)
        np.testing.assert_allclose(np.array(leg['bind']).reshape(4, 4)[:3, :3], np.eye(3))

    @unittest.skipUnless(DISC.exists(), 'Local S.L.A.I. disc unavailable')
    def test_original_sampler_instructions_are_guarded(self):
        with SlaiDisc(DISC) as disc:
            program = ProgramData(disc)
            contract = decode_motion_sampling(program)
            self.assertIn('before clock', contract['control_update'])

            class Modified:
                def __init__(self, changed):
                    self.changed = changed

                def read(self, address, size):
                    return bytes(size) if address == self.changed else program.read(address, size)

            for address in SAMPLER_WORDS:
                with self.assertRaisesRegex(ValueError, 'motion sampler'):
                    decode_motion_sampling(Modified(address))


if __name__ == '__main__':
    unittest.main()
