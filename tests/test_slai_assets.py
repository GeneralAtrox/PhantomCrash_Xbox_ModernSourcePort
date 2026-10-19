"""Format tests for source asset conversion; no extracted assets committed."""

import io
import struct
import unittest

import numpy as np

from phantomcrash_research.gltf_edit import Builder
from phantomcrash_research.slai.archive import SlaiDisc
from phantomcrash_research.slai.model import local_matrix, texture_names, model_document
from phantomcrash_research.slai.motion import read_motion
from phantomcrash_research.slai.sound import decode_adpcm, midi_events
from phantomcrash_research.slai.particles import read_particle, write_particle
from phantomcrash_research.slai.sony_midi import convert_events
from phantomcrash_research.slai.sound_bank import read_instruments


class SlaiAssetTests(unittest.TestCase):
    def test_archive_names_allow_the_shipped_german_texture_hyphens(self):
        raw = bytearray(2048)
        names = [b"G38_GROUND.PIG", b"G39_ELE-POLE.PIG", b"KABE13.15.PIG"]
        for i, name in enumerate(names):
            struct.pack_into("<24sII", raw, i * 32, name, 128 + i * 16, 16)
            raw[128 + i * 16:132 + i * 16] = b"WpNO"
        disc = SlaiDisc.__new__(SlaiDisc)
        disc._file = io.BytesIO(raw)
        disc._archive = (0, len(raw))
        packages = list(disc._scan())
        self.assertEqual([f.name for f in packages[0][1]], [n.decode() for n in names])

    def test_archive_does_not_discover_suffix_of_large_table(self):
        count = 80
        raw = bytearray(4096)
        for i in range(count):
            name = f"S{i:04d}.PO2".encode().ljust(24, b"\0")
            struct.pack_into("<24sII", raw, i * 32, name, 2560, 100)
        raw[2560:2564] = b"WpNO"
        disc = SlaiDisc.__new__(SlaiDisc)
        disc._file = io.BytesIO(raw)
        disc._archive = (0, len(raw))
        packages = list(disc._scan())
        self.assertEqual(len(packages), 1)
        self.assertEqual(len(packages[0][1]), count)

    def test_archive_accepts_aligned_table_gap(self):
        raw = bytearray(2048)
        for i in range(7):
            name = f"G{i:02d}.PIG".encode().ljust(24, b"\0")
            struct.pack_into("<24sII", raw, i * 32, name, 768, 40)
        raw[768:772] = b"WpNO"
        disc = SlaiDisc.__new__(SlaiDisc)
        disc._file = io.BytesIO(raw)
        disc._archive = (0, len(raw))
        self.assertEqual(len(list(disc._scan())), 1)

    def test_compressed_motion_uses_signed_thousandths_and_durations(self):
        raw = bytearray(128)
        struct.pack_into("<4sII", raw, 0, b"P2MT", 70, 1)
        struct.pack_into("<3I", raw, 16, 96, 2, 0x58015)
        struct.pack_into("<16f", raw, 32, *np.eye(4).ravel())
        struct.pack_into("<H7h", raw, 96, 3, -1000, 2000, 3000, -1000, 0, 0, 0)
        struct.pack_into("<H7h", raw, 112, 4, 500, -1500, 0, -707, 707, 0, 0)
        track = read_motion(bytes(raw))["tracks"][0]
        self.assertEqual(track["keys"][0]["translation"], [-1, 2, 3])
        self.assertEqual(track["keys"][1]["frame"], 3)
        self.assertEqual(track["duration_frames"], 7)
        self.assertEqual(track["keys"][0]["quaternion_wxyz"], [-1, 0, 0, 0])

    def test_uncompressed_translation_stride_is_two_quadwords(self):
        raw = bytearray(160)
        struct.pack_into("<4sII", raw, 0, b"P2MT", 70, 1)
        struct.pack_into("<3I", raw, 16, 96, 2, 4)
        struct.pack_into("<16f", raw, 32, *np.eye(4).ravel())
        struct.pack_into("<I", raw, 96, 2)
        struct.pack_into("<3f", raw, 112, 1, 2, 3)
        struct.pack_into("<I", raw, 128, 3)
        struct.pack_into("<3f", raw, 144, 4, 5, 6)
        keys = read_motion(bytes(raw))["tracks"][0]["keys"]
        self.assertEqual(keys[1]["frame"], 2)
        self.assertEqual(keys[1]["translation"], [4, 5, 6])

    def test_motion_bounds_are_checked(self):
        raw = b"P2MT" + struct.pack("<II", 70, 99) + b"\0" * 4
        with self.assertRaisesRegex(ValueError, "Truncated"):
            read_motion(raw)

    def test_animation_buffer_view_has_no_gpu_target(self):
        builder = Builder()
        builder.add(np.array([[0], [1]], np.float32), "SCALAR", 5126, 0, bounds=True)
        self.assertNotIn("target", builder.views[0])

    def test_coordinate_conversion_keeps_local_hierarchy(self):
        matrix = np.eye(4)
        matrix[3, :3] = [10, 20, 30]
        value = local_matrix(matrix.ravel())
        np.testing.assert_allclose(value[3, :3], [1, -2, 3])
        np.testing.assert_allclose(value[:3, :3], np.eye(3))

    def test_texture_list_uses_offsets_not_consecutive_fixed_names(self):
        raw = struct.pack("<III", 2, 12, 16) + b"ONE\0SECOND\0"
        self.assertEqual(texture_names(raw), ["ONE", "SECOND"])

    def test_psx_adpcm_nibble_order_and_loop_points(self):
        block = bytes([12, 6]) + bytes([0x8F]) * 14
        pcm, loop = decode_adpcm(block + bytes([12, 3]) + b"\0" * 14)
        self.assertEqual(struct.unpack_from("<4h", pcm), (-1, -8, -1, -8))
        self.assertEqual(loop, {"start_sample": 0, "end_sample": 56, "repeat": True})

    def test_invalid_psx_predictor_rejected(self):
        with self.assertRaisesRegex(ValueError, "Invalid"):
            decode_adpcm(bytes([0xF0]) + b"\0" * 15)

    def test_particle_round_trip_preserves_unassigned_words(self):
        header = b"PTKUN212" + bytes(1016)
        emitter = b"EMI0\0".ljust(256, b"\0") + struct.pack("<192I", *range(192))
        raw = header + struct.pack("<I", 1) + emitter + struct.pack("<I", 0)
        document = read_particle(raw)
        self.assertEqual(write_particle(document), raw)
        self.assertEqual(document["emitters"][0]["parameter_words"][100], 100)
        document["emitters"][0]["name"] = "NEW"
        self.assertEqual(read_particle(write_particle(document))["emitters"][0]["name"], "NEW")

    def test_particle_record_bounds_are_checked(self):
        raw = b"PTKUN212" + bytes(1016) + struct.pack("<II", 2, 0)
        with self.assertRaisesRegex(ValueError, "exceed"):
            read_particle(raw)

    def test_midi_end_marker_inside_meta_data_is_not_a_track_end(self):
        track = b"\x00\xff\x01\x03\xff\x2f\x00\x00\xff\x2f\x00"
        self.assertEqual(midi_events(track + b"padding"), track)

    def test_midi_running_status_and_bounds(self):
        track = b"\x00\x90\x3c\x7f\x01\x3c\x00\x00\xff\x2f\x00"
        self.assertEqual(midi_events(track), track)
        with self.assertRaises(ValueError):
            midi_events(b"\x00\x90\x3c")

    def test_midi_sysex_payload_length(self):
        track = b"\x00\xf0\x03\x01\x02\xf7\x00\xff\x2f\x00"
        self.assertEqual(midi_events(track + b"\0"), track)

    def test_sony_note_off_has_no_velocity_and_can_skip_next_delta(self):
        source = b"\x00\x90\x3c\x7f\x01\x80\xbc\xff\x2f"
        expected = b"\x00\x90\x3c\x7f\x01\x80\x3c\x00\x00\xff\x2f\x00"
        self.assertEqual(convert_events(source), expected)
        self.assertEqual(midi_events(expected), expected)

    def test_sony_compressed_data_retains_controller_and_running_status(self):
        source = b"\x00\xb0\x07\xff\x0a\x40\x01\xff\x2f"
        expected = b"\x00\xb0\x07\x7f\x00\xb0\x0a\x40\x01\xff\x2f\x00"
        self.assertEqual(convert_events(source), expected)

    def test_unlit_model_ignores_absent_normal_buffer_and_material_colour(self):
        raw = bytearray(432)
        struct.pack_into("<4sII", raw, 0, b"POD2", 1, 1)
        struct.pack_into("<I", raw, 28, 64)
        struct.pack_into("<4hI", raw, 64, -1, -1, -1, 0, 144)
        struct.pack_into("<16f", raw, 80, *np.eye(4).ravel())
        struct.pack_into("<4I", raw, 144, 1, 8, 1, 208)
        struct.pack_into("<4I", raw, 208, 1, 6, 0, 256)
        struct.pack_into("<I", raw, 256, 3)
        struct.pack_into("<4I", raw, 272, 288, 0, 336, 384)
        for i, position in enumerate([(0, 0, 0), (1, 0, 0), (0, 0, 1)]):
            struct.pack_into("<3fI", raw, 288 + i * 16, *position, 32768 if i == 2 else 0)
            struct.pack_into("<4f", raw, 336 + i * 16, 64, 64, 64, 128)
            struct.pack_into("<4f", raw, 384 + i * 16, 0, 0, 1, 0)
        model = model_document(bytes(raw), [], {}, {})
        material = model["materials"][0]
        self.assertEqual(material["pbrMetallicRoughness"]["baseColorFactor"], [1, 1, 1, 1])
        self.assertIn("KHR_materials_unlit", material["extensions"])
        self.assertNotIn("NORMAL", model["meshes"][0]["primitives"][0]["attributes"])
        bind = np.eye(4)
        bind[3, :3] = [10, 20, 30]
        motion = {"tracks": [{"node": 0, "keys": [], "flags": 0x58000,
                              "bind_matrix": bind.ravel().tolist(), "duration_frames": 0}]}
        animated = model_document(bytes(raw), [], {}, {}, [("constant", motion)])
        self.assertEqual(len(animated["animations"][0]["channels"]), 2)
        self.assertNotIn("matrix", animated["nodes"][0])
        scaled_bind = np.eye(4)
        scaled_bind[:3, :3] *= 0.91
        struct.pack_into("<16f", raw, 80, *scaled_bind.ravel())
        scaled = model_document(bytes(raw), [], {}, {}, [("scaled", motion)])
        np.testing.assert_allclose(scaled["nodes"][0]["scale"], [0.91, 0.91, 0.91])

    def test_sound_bank_signed_tuning_envelopes_and_sample_links(self):
        sample = bytearray(42)
        struct.pack_into("<H", sample, 0, 9)
        sample[2], sample[4], sample[11], sample[12] = 1, 127, 60, 253
        struct.pack_into("<HH", sample, 18, 0x80FF, 0x1234)
        samples = self._sound_chunk(b"IECSlpmS", sample)
        sets = self._sound_chunk(b"IECStesS", bytes([0, 1, 127, 1, 0, 0]))
        program = bytearray(56)
        struct.pack_into("<IBB", program, 0, 36, 1, 20)
        program[38], program[40] = 12, 12
        programs = self._sound_chunk(b"IECSgorP", program)
        bank = read_instruments(samples + sets + programs, 0, len(samples), len(samples) + len(sets))
        self.assertEqual(bank["samples"][0]["wave"], 9)
        self.assertEqual(bank["samples"][0]["detune"], -3)
        self.assertEqual(bank["samples"][0]["adsr1"], 0x80FF)
        self.assertEqual(bank["programs"][0]["splits"][0]["note_min"], 12)
        self.assertEqual(bank["sample_sets"][0]["samples"], [0])

    def test_sound_bank_rejects_invalid_sample_set_link(self):
        samples = self._sound_chunk(b"IECSlpmS", bytes(42))
        sets = self._sound_chunk(b"IECStesS", bytes([0, 1, 127, 1, 1, 0]))
        with self.assertRaisesRegex(ValueError, "missing sample"):
            read_instruments(samples + sets, 0, len(samples), 0)

    @staticmethod
    def _sound_chunk(tag, payload):
        return tag + struct.pack("<III", 20 + len(payload), 0, 20) + payload


if __name__ == "__main__":
    unittest.main()
