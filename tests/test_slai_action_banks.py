"""Compiled action-bank structure and writable recovered effect fields."""

import hashlib
import json
from pathlib import Path
import struct
import unittest

from phantomcrash_research.slai.action_bank import (
    bind_weapon_action_bank, read_action_bank, weapon_action_route, write_action_bank,
)


class SlaiActionBankTests(unittest.TestCase):
    def sample(self):
        row = bytearray((37*i+11) & 255 for i in range(640))
        struct.pack_into("<b", row, 0x14, 0)
        struct.pack_into("<b", row, 0x34, 0)
        struct.pack_into("<I", row, 0x260, 1)
        struct.pack_into("<3B", row, 0x80, 26, 21, 22)
        struct.pack_into("<b", row, 0x84, -1)
        struct.pack_into("<fI", row, 0x88, .12, 0x3A00)
        struct.pack_into("<3f", row, 0x9C, 1.01, .01, -1.)
        struct.pack_into("<3f", row, 0xA8, 2., -3., 4.)
        return struct.pack("<H", 1) + row

    def test_recovered_effect_fields_and_unknown_bytes_round_trip(self):
        raw = self.sample()
        bank = read_action_bank(raw)
        e = bank["records"][0]["effects"][0]
        self.assertEqual((e["part_selector"], e["particle"], e["alternate_particle"]), (26, 21, 22))
        self.assertEqual((e["parent_node"], e["parent_flags"]), (-1, 0x3A00))
        self.assertAlmostEqual(e["inertia_weight"], 1.01, places=6)
        self.assertAlmostEqual(e["inertia_decay"], .01, places=6)
        self.assertEqual(e["position_offset"], [2., -3., 4.])
        self.assertEqual(write_action_bank(bank), raw)
        bank["records"][0]["effects"][0]["alternate_particle"] = -1
        changed = write_action_bank(bank)
        self.assertEqual(changed[2+0x82], 255)
        self.assertEqual(read_action_bank(changed)["records"][0]["effects"][0]["alternate_particle"], -1)

    def test_editing_effect_values_preserves_unrelated_trigger_and_motion_words(self):
        raw = self.sample()
        bank = read_action_bank(raw)
        bank["records"][0]["effects"][0]["scale"] = .25
        bank["records"][0]["effects"][0]["inertia_decay"] = .02
        changed = write_action_bank(bank)
        allowed = set(range(2+0x88, 2+0x8C)) | set(range(2+0xA0, 2+0xA4))
        self.assertTrue(all(a == b for i, (a, b) in enumerate(zip(raw, changed)) if i not in allowed))
        self.assertEqual(read_action_bank(changed)["records"][0]["effects"][0]["scale"], .25)

    def test_rejects_truncated_records_and_trailing_data(self):
        for raw in [b"", b"\1", b"\1\0", self.sample()[:-1], self.sample()+b"\0"]:
            with self.assertRaises(ValueError):
                read_action_bank(raw)
        self.assertEqual(write_action_bank(read_action_bank(b"\0\0")), b"\0\0")

    def test_signed_recycling_priorities_edit_only_their_two_bytes(self):
        raw = self.sample()
        bank = read_action_bank(raw)
        effect = bank["records"][0]["effects"][0]
        self.assertEqual((effect["recycle_priority"], effect["recycle_secondary"]),
                         struct.unpack_from("<2b", raw, 2+0xb8))
        effect["recycle_priority"], effect["recycle_secondary"] = -7, 12
        changed = write_action_bank(bank)
        self.assertEqual(changed[:2+0xb8], raw[:2+0xb8])
        self.assertEqual(changed[2+0xba:], raw[2+0xba:])
        self.assertEqual(struct.unpack_from("<2b", changed, 2+0xb8), (-7, 12))

    def test_all_eight_effects_and_signed_trigger_fields_are_writable(self):
        raw = bytearray(self.sample())
        struct.pack_into("<I", raw, 2+0x260, 8)
        descriptor = bytes(raw[2+0x80:2+0xbc])
        for i in range(8):
            start = 2+0x80+i*60
            raw[start:start+60] = descriptor
            struct.pack_into("<b", raw, start+1, 21+i)
        struct.pack_into("<3b", raw, 2+4, 0, 4, -1)
        struct.pack_into("<b", raw, 2+0x14, 3)
        struct.pack_into("<I", raw, 2+0x1c, 255)
        struct.pack_into("<h", raw, 2+0x2c, -2)
        struct.pack_into("<b", raw, 2+0x34, 1)
        bank = read_action_bank(raw)
        record = bank["records"][0]
        self.assertEqual([e["particle"] for e in record["effects"]], list(range(21, 29)))
        self.assertEqual(record["trigger"]["motion_ids"], [0, 4, -1])
        self.assertEqual(record["trigger"]["weapon_gates"], [{"mask": 255, "delay": -2}])
        self.assertEqual(write_action_bank(bank), raw)
        record["effects"][7]["particle"] = -1
        changed = write_action_bank(bank)
        offset = 2+0x80+7*60+1
        self.assertEqual(changed[offset], 255)
        self.assertEqual(changed[:offset], raw[:offset])
        self.assertEqual(changed[offset+1:], raw[offset+1:])

    def test_owner_fixups_bind_every_effect_and_only_placeholder_weapon_masks(self):
        raw = bytearray(self.sample())
        raw[2+0xbc:2+0xf8] = raw[2+0x80:2+0xbc]
        struct.pack_into("<I", raw, 2+0x260, 2)
        struct.pack_into("<2I", raw, 2+0x1c, 0xff, 0x80)
        struct.pack_into("<2h", raw, 2+0x2c, 1, 3)
        struct.pack_into("<b", raw, 2+0x34, 2)
        bank = read_action_bank(raw)
        for slot, selector, mask in ((0, 3, 64), (1, 2, 128), (2, 5, 256), (3, 4, 512)):
            bound = bind_weapon_action_bank(bank, slot)
            record = bound["records"][0]
            self.assertEqual([e["part_selector"] for e in record["effects"]], [selector]*2)
            self.assertEqual([g["mask"] for g in record["trigger"]["weapon_gates"]], [mask, 128])
            self.assertEqual(record["trigger"]["weapon_state_slot"], slot)
            reread = read_action_bank(write_action_bank(bound))
            self.assertEqual(reread["records"][0]["effects"], record["effects"])
        self.assertEqual(write_action_bank(bank), raw)
        with self.assertRaises(ValueError):
            bind_weapon_action_bank(bank, 4)

    def test_rejects_list_extents_outside_the_original_record(self):
        for offset, layout, value in ((0x260, "<I", 9), (0x14, "<b", -1),
                                      (0x14, "<b", 17), (0x34, "<b", 5)):
            raw = bytearray(self.sample())
            struct.pack_into(layout, raw, 2+offset, value)
            with self.assertRaises(ValueError):
                read_action_bank(raw)

    def test_weapon_routes_use_the_compiled_selector_index_and_shipped_bank_name(self):
        class Program:
            def read(self, address, size):
                self.request = (address, size)
                return struct.pack("<I", 0x59c7e0)

            def string(self, address):
                self.pointer = address
                return "photonArmHandgun"

        program = Program()
        entries = [{"source": "PHOTONARMHANDGUN.ASB", "file": "actions/source.json"}]
        route = weapon_action_route(program, entries, 20)
        self.assertEqual(program.request, (0x59ce8c, 4))
        self.assertEqual(program.pointer, 0x59c7e0)
        self.assertEqual(route["bank"], "slai/assets/actions/source.json")
        self.assertEqual(route["source_name"], "photonArmHandgun")
        with self.assertRaises(ValueError):
            weapon_action_route(program, [], 20)
        with self.assertRaises(ValueError):
            weapon_action_route(program, entries, 95)

    def test_local_weapon_routes_cover_registered_parts_and_complete_effect_lists(self):
        root = Path(__file__).resolve().parents[1] / "mods"
        table = root / "slai/native_particles.json"
        if not table.exists():
            self.skipTest("Disc-derived mod content is not bundled with source")
        routes = json.loads(table.read_text())["weapon_actions"]
        parts = json.loads((root / "slai/native_parts.json").read_text())["parts"]
        self.assertEqual({(p["category"], p["type"]) for p in parts
                          if p["category"] in ("arm", "shoulder")},
                         {(r["mount"], r["model"]) for r in routes})
        self.assertEqual(len(routes), 95)
        for route in routes:
            bank = json.loads((root / route["bank"]).read_text())
            self.assertEqual(bank["source"], route["source_name"].upper()+".ASB")
            for slot in ((0, 1) if route["mount"] == "arm" else (2, 3)):
                bound = bind_weapon_action_bank(bank, slot)
                for row in bound["records"]:
                    self.assertEqual({e["part_selector"] for e in row["effects"]},
                                     {(3, 2, 5, 4)[slot]})
                # The saved descriptor count must still match the full source list.
                self.assertEqual(len(write_action_bank(bound)), 2+640*len(bank["records"]))

    def test_local_export_preserves_every_original_action_bank_payload(self):
        root = Path(__file__).resolve().parents[1] / "mods/slai/assets"
        manifest = root / "manifest.json"
        if not manifest.exists():
            self.skipTest("Disc-derived mod content is not bundled with source")
        entries = json.loads(manifest.read_text(encoding="utf-8")).get("action_banks")
        if entries is None:
            self.skipTest("Action banks have not been converted locally")
        self.assertEqual(len(entries), 128)
        self.assertEqual(sum(e["records"] for e in entries), 1243)
        self.assertEqual(sum(e["effects"] for e in entries), 2233)
        for entry in entries:
            bank = json.loads((root / entry["file"]).read_text(encoding="utf-8"))
            raw = write_action_bank(bank)
            self.assertEqual(hashlib.sha256(raw).hexdigest(), entry["sha256"], entry["source"])
            self.assertEqual(len(raw), 2+640*entry["records"])


if __name__ == "__main__":
    unittest.main()
