"""Read-only MWo3 overlay programs and their original virtual-address data."""

import struct

from .stats import _elf_offset


class ProgramData:
    def __init__(self, disc):
        at, size = disc.disc_files()["SLES_529.40"]
        self.elf = disc._read(at, size)
        at, size = disc.disc_files()["PC2PRG.STM"]
        archive = disc._read(at, size)
        self.overlays = {}
        for i in range(struct.unpack_from("<I", archive, 4)[0]):
            offset, length = struct.unpack_from("<2I", archive, 8 + i * 8)
            data = archive[offset:offset+length]
            if data[:4] != b"MWo3" or len(data) != length:
                raise ValueError("Unsupported program overlay")
            name = data[32:64].split(b"\0")[0].decode("ascii")
            self.overlays[name] = (struct.unpack_from("<I", data, 8)[0], data)

    def read(self, va, size, overlay="battledvd.bin"):
        base, data = self.overlays[overlay]
        if base <= va and va + size <= base + len(data):
            return data[va-base:va-base+size]
        try:
            at = _elf_offset(self.elf, va, size)
        except ValueError:
            return b""
        return self.elf[at:at+size]

    def string(self, va, overlay="battledvd.bin"):
        data = self.read(va, 128, overlay).split(b"\0")[0]
        return data.decode("ascii") if data and all(32 <= c < 127 for c in data) else ""

    def stage_textures(self, model_names):
        """The battle's stage descriptors: 13 model names, 110 image names,
        then 13 texture-binding rows of 110 signed shorts terminated by -1.
        The model names select the row, not a filename or alphabetical guess.
        """
        base, data = self.overlays["battledvd.bin"]
        result = {}
        for at in range(64, len(data) - 3372, 4):
            pointer = struct.unpack_from("<I", data, at)[0]
            if not (0x100000 <= pointer < base+len(data)):
                continue
            folder = self.string(pointer)
            if "/bg/" not in folder or not folder.startswith("host0:"):
                continue
            names = [self.string(v).upper() for v in struct.unpack_from("<13I", data, at+4)]
            if not names[0] or names[0]+".PO2" not in model_names:
                continue
            pool = [self.string(v) for v in struct.unpack_from("<110I", data, at+72)]
            for row, name in enumerate(names):
                if not name or name+".PO2" not in model_names:
                    continue
                values = struct.unpack_from("<110h", data, at+512+220*row)
                if -1 not in values:
                    continue
                ids = values[:values.index(-1)]
                if any(i < 0 or i >= len(pool) or not pool[i] for i in ids):
                    raise ValueError(f"Invalid compiled texture binding for {name}")
                result[name+".PO2"] = {"textures": [pool[i].split("/")[-1].upper() for i in ids],
                    "folder": folder, "table_address": hex(base+at), "row": row,
                    "source_indices": list(ids)}
        # The common gate loader (0x516200..0x5166c8) binds image 0 to
        # its two door models and image 1 to table/under models. The image
        # pointers are in main-program GP data, while names are in battle.
        gates = [self.string(v) for v in struct.unpack("<4I", self.read(0x59ea90, 16))]
        images = [self.string(v) for v in struct.unpack("<2I", self.read(0x4396b8, 8))]
        if (all("/bg/gate/" in s and s.endswith(".po2") for s in gates)
                and all("/bg/gate/" in s and s.endswith(".pig") for s in images)):
            for row, path in enumerate(gates):
                name = path.split("/")[-1].upper()
                result[name] = {"textures": [images[0 if row < 2 else 1].split("/")[-1][:-4].upper()],
                    "folder": "host0:data/z_zbattle/bg/gate", "table_address": "0x59ea90",
                    "row": row, "loader_address": "0x516200"}
        result.update(self.effect_textures())
        return result

    def effect_textures(self):
        """Explicit model/image loads and the common prop descriptor.

        The shield/light loader stores model/image pairs at context offsets
        0x1200/0x1204 and 0x1224/0x1228. The generic prop loader supplies
        JET_CONT's initial image; the supply counter can replace that image.
        """
        result = {}
        for model, image, loader in ((0x59c290, 0x59c2c0, 0x50743c),
                                      (0x59c2f0, 0x59c320, 0x507484)):
            model_path, image_path = self.string(model), self.string(image)
            if not model_path.endswith(".po2") or not image_path.endswith(".pig"):
                raise ValueError("Unsupported effect resource pair")
            result[model_path.split("/")[-1].upper()] = {
                "textures": [image_path.split("/")[-1][:-4].upper()],
                "loader_address": hex(loader), "source_model": model_path,
                "source_image": image_path}
        model = self.string(struct.unpack("<23I", self.read(0x5b2d60, 92))[-1])
        image = self.string(struct.unpack("<41I", self.read(0x5b3530, 164))[-1])
        if not model.endswith("jet_cont.po2") or not image.endswith("jet01.pig"):
            raise ValueError("Unsupported supply prop descriptor")
        result["JET_CONT.PO2"] = {"textures": ["JET01"],
            "table_address": "0x5b2d60", "row": 22,
            "source_model": model, "source_image": image,
            "runtime_image_overrides": [self.string(0x5b2690 + i*64) for i in range(10)]}
        return result

    def supply_pickup_visuals(self):
        """Crate model and ten kind-indexed image banks, not reward values.

        0x522e54..64 selects bank kind*4 from model bank +4. The update
        at 0x525394..a0 applies the common scale 2.5 to the model.
        """
        model = self.string(0x5b2650)
        images = [self.string(0x5b2690 + i*64) for i in range(10)]
        if (not model.endswith("/jet_cont.po2") or
                not all("/jet_cont/" in image and image.endswith(".pig") for image in images)):
            raise ValueError("Unsupported source supply crate banks")
        if self.read(0x525364, 4) != struct.pack("<I", 0x3c024020):
            raise ValueError("Unsupported source supply crate scale")
        return {"model": "slai/assets/models/JET_CONT.gltf",
                "images": ["slai/assets/textures/" + image.rsplit("/", 1)[-1][:-4].lower() + ".png"
                           for image in images],
                "scale": 2.5,
                "provenance": {"model_path": model, "image_paths": images,
                               "model_loader": "0x520b90", "image_loaders": "0x520bb4..520cf8",
                               "kind_binding": "0x522e54..64", "scale": "0x525394..a0"}}

    def prop_resources(self):
        """Decode the shipped prop-ID switch as data, without executing it.

        Battle 0x5203b0 selects a model and its image variant; 0x520d20
        loads the pointer arrays and per-model image counts used by that switch.
        """
        names = [self.string(v) for v in struct.unpack("<23I", self.read(0x5b2d60, 92))]
        counts = struct.unpack("<23h", self.read(0x5b2910, 46))
        images = [self.string(v) for v in struct.unpack("<41I", self.read(0x5b3530, 164))]
        if (not all("/bgobj/" in n and n.endswith(".po2") for n in names)
                or sum(counts) != len(images) or any(c <= 0 for c in counts)):
            raise ValueError("Unsupported battle prop resource table")
        targets = struct.unpack("<39I", self.read(0x5bb3d0, 156))
        result = {}
        for resource, target in enumerate(targets):
            if not 0x5203e0 <= target < 0x5206d0:
                raise ValueError("Invalid prop switch target")
            registers, fields = {0: 0}, {}
            # Each case consists of constant assignments and two short stores,
            # followed by a branch to the common return (including delay slot).
            for word in struct.unpack("<6I", self.read(target, 24)):
                opcode, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
                if opcode == 9 and rs == 0:
                    registers[rt] = word & 65535
                elif opcode == 41 and rs in (6, 7) and (word & 65535) == 0:
                    if rt not in registers:
                        raise ValueError("Nonconstant prop switch assignment")
                    fields[rs] = registers[rt]
                if len(fields) == 2:
                    break
            if len(fields) != 2:
                raise ValueError("Unresolved prop switch case")
            model, variant = fields[6], fields[7]
            if model >= len(names) or variant >= counts[model]:
                raise ValueError("Prop variant outside its image table")
            image = images[sum(counts[:model]) + variant]
            result[resource] = {"model": names[model].split("/")[-1].upper(),
                "texture": image.split("/")[-1].removesuffix(".pig").upper(),
                "model_index": model, "image_variant": variant,
                "switch_address": hex(target), "source_model": names[model], "source_image": image}
        return result
