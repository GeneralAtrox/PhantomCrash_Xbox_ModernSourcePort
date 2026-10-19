"""Authored supply-ship paths and the crate's compiled presentation values."""

import math
import struct
from pathlib import PurePosixPath

from .resources import resource_packages


def read_supply_path(raw, scale):
    if len(raw) < 20:
        raise ValueError('Truncated supply path')
    count, closing, drop, drop_end, leave = struct.unpack_from('<5h', raw)
    if count < 0 or len(raw) != 20 + count * 64:
        raise ValueError('Unsupported supply path size')
    if count and not (0 <= closing <= drop <= drop_end <= leave < count):
        raise ValueError('Invalid supply path boundaries')
    segments = []
    for i in range(count):
        points = []
        for j in range(4):
            x, y, z, w = struct.unpack_from('<4f', raw, 20 + i * 64 + j * 16)
            if not all(math.isfinite(v) for v in (x, y, z, w)) or w != 1:
                raise ValueError('Invalid supply path point')
            points.append([x * scale, -y * scale, -z * scale])
        segments.append(points)
    return {'segments': segments, 'closing': closing, 'drop': drop,
            'drop_end_source': drop_end, 'leave': leave,
            'source_header_float': struct.unpack_from('<f', raw, 12)[0]}


def supply_assets(disc, program, units):
    guards = {0x52252c: 0x24e73b60, 0x522618: 0x26940014,
              0x522620: 0x00022180, 0x522648: 0x24060040,
              0x524438: 0x8f82b21c, 0x5245c8: 0x8f82b220,
              0x524600: 0x8f82b228, 0x525318: 0x3c024270,
              0x525334: 0x3c024170, 0x525338: 0x3c024461,
              0x5253b4: 0x3c0342c0, 0x52542c: 0x3c034280,
              0x525430: 0x3c023f00, 0x525448: 0x3c023d4c,
              0x52544c: 0x3443cccd, 0x5235c8: 0x3c033cf5,
              0x5235cc: 0x3463c28f, 0x523674: 0x0c06cf18}
    for address, word in guards.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported supply presentation at {address:#x}')
    package = resource_packages(disc)['Z/BGOBJ/SUPPLY_SHIP/JET_PATH']
    files = {f.name: f for f in disc.packages()[package]}
    counts = struct.unpack('<18i', program.read(0x5b38d0, 72))
    prefixes = [program.string(v) for v in struct.unpack('<18I', program.read(0x5b3b60, 72))]
    routes = {}
    for prefix, count in zip(prefixes, counts):
        if not count or prefix in routes:
            continue
        rows = []
        for i in range(count):
            name = PurePosixPath(prefix).name.upper() + f'{i:02d}.PTH'
            path = read_supply_path(disc.read(files[name]), units['world_scale'])
            path.update({'source': name, 'package_offset': package})
            if path['segments']:
                rows.append(path)
        routes[prefix] = rows
    return {'routes': routes,
            'carrier_model': 'slai/assets/models/jet01.gltf',
            'carrier_light_model': 'slai/assets/models/jet_light.gltf',
            'light_spin_per_second': struct.unpack('<f', struct.pack('<I', 0x3cf5c28f))[0] * 60,
            'pulse': {'frame_rate': 60, 'step': .05, 'fast_frames': 60,
                      'fast_rate': 15, 'life_numerator': 900,
                      'colour_base': .5, 'colour_steps': 96, 'colour_divisor': 64},
            'provenance': {'path_loader': '0x522520..522690',
                           'path_tables': '0x5b38d0;0x5b3b60',
                           'curve': '0x524350;0x1ab690',
                           'entry_drop_exit': '0x524438;0x5245c8;0x524600',
                           'pulse': '0x525318..525498',
                           'light': '0x5235c0..523678'}}


def stage_supply_routes(assets, source):
    city = PurePosixPath(source).stem.upper().removeprefix('DAY_').removeprefix('NIGHT_')
    city = {'TOKYO': 'SINJYUKU', 'TOKYO2': 'SINJYUKU', 'ENGLAND': 'LONDON'}.get(city, city)
    return next((rows for name, rows in assets['routes'].items()
                 if PurePosixPath(name).name.upper() == city), [])
