"""Lossless PTKUN212 asset records, retaining undecoded parameter words.

All shipped files have a 1024-byte header, an emitter count and 1024-byte
emitter records, then a generator count and 1024-byte generator records.
Each record starts with a 256-byte name. Parameter semantics are pending;
serialized engine addresses are data and must never be executed by the port.
"""

import struct
from .particle_fields import generator_fields

RECORD_SIZE = 1024
NAME_SIZE = 256


def read_particle(data: bytes) -> dict:
    if data[:8] != b"PTKUN212" or len(data) < RECORD_SIZE + 8:
        raise ValueError("Unsupported particle asset")
    header = list(struct.unpack_from("<256I", data))
    cursor = RECORD_SIZE
    groups = {}
    for group in ("emitters", "generators"):
        count = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        if cursor + count * RECORD_SIZE > len(data):
            raise ValueError("Particle records exceed file")
        records = []
        for i in range(count):
            record = data[cursor:cursor + RECORD_SIZE]
            name = record[:NAME_SIZE].split(b"\0", 1)[0].decode("cp932", errors="replace")
            records.append({"name": name, "name_bytes": list(record[:NAME_SIZE]),
                            "parameter_words": list(struct.unpack_from("<192I", record, NAME_SIZE))})
            cursor += RECORD_SIZE
        groups[group] = records
    if cursor != len(data):
        raise ValueError("Trailing particle data")
    for generator in groups["generators"]:
        generator["decoded_fields"] = generator_fields(generator["parameter_words"])
    return {"format": "PTKUN212", "header_words": header, **groups,
            "parameter_semantics": "partial; verified fields retain source units",
            "native_rendering": "Native billboard/world quads with source birth bases and texture strips; parent attachment/inertia/orientation and full GS behavior remain partial"}


def write_particle(document: dict) -> bytes:
    result = bytearray(struct.pack("<256I", *document["header_words"]))
    for group in ("emitters", "generators"):
        result += struct.pack("<I", len(document[group]))
        for record in document[group]:
            name = bytes(record["name_bytes"])
            if len(name) != NAME_SIZE:
                raise ValueError("Particle name buffer must retain 256 bytes")
            current = name.split(b"\0", 1)[0].decode("cp932", errors="replace")
            if record["name"] != current:
                encoded = record["name"].encode("cp932")
                if len(encoded) >= NAME_SIZE:
                    raise ValueError("Particle name is too long")
                name = (encoded + b"\0").ljust(NAME_SIZE, b"\0")
            result += name + struct.pack("<192I", *record["parameter_words"])
    return bytes(result)
