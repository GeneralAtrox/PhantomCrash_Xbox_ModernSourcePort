"""PMT quaternion channels and original per-control cursor semantics."""

import struct
import numpy as np


SAMPLER_WORDS = {
    0x19eeb4: 0x0c067bd0,  # evaluate, then primary/secondary clock advances
    0x19eee4: 0x0c067d48,
    0x19ef14: 0x0c067e40,
    0x19f64c: 0x24840001,
    0x19f650: 0x2463ffff,
    0x19f654: 0x0083182a,  # next key must precede terminal endpoint
    0x19fa44: 0x24840001,
    0x19fa48: 0x2463ffff,
    0x19fa4c: 0x0083182a,
    0x19fac8: 0xe6940000,  # nonloop restores pre-update elapsed
    0x19fda4: 0x0c068264,  # compressed quaternion slerp
    0x19fe20: 0x0c068264,  # uncompressed quaternion slerp
    0x1a0654: 0x460a1881,  # 2xy - 2zw: source matrix direction
    0x1a0658: 0xe4a20004,
    0x1a066c: 0xe4a30010,
}


def decode_motion_sampling(program):
    for address, word in SAMPLER_WORDS.items():
        if program.read(address, 4) != struct.pack('<I', word):
            raise ValueError(f'Unsupported source motion sampler at {address:#x}')
    return {
        'key_sampler': '0x19fd30..19fe98',
        'quaternion_interpolation': '0x1a0990..1a0b94: normalized short-arc slerp',
        'quaternion_matrix': '0x1a05f0..1a06a8: source WXYZ, transposed row convention',
        'primary_cursor': '0x19f520..19f8d8',
        'secondary_cursor': '0x19f900..19fc98',
        'control_update': '0x19ee50..19ef34: evaluate before clock advance',
        'terminal_key': 'endpoint only; final key duration is not traversed',
        'nonloop_end': 'restore elapsed saved before the update; retain current segment',
    }


def native_pmt_track(track, node_matrix, *, baked_bind_scale=True):
    """Retain PMT keys; convert only the source-to-native coordinate basis.

    Source scalar matrix conversion has the opposite quaternion direction
    from a row-vector XYZW matrix. Conjugating that matrix by [1,-1,-1]
    gives native XYZW [-x,y,z,w], matching the body glTF loader.
    """
    from .native_geometry import native_matrix
    flags = track['flags']
    if flags & 2 and not flags & 1:
        raise ValueError('Euler PMT key playback is not implemented')
    bind = track['bind_matrix'] if flags & 0x8000 else node_matrix
    matrix = native_matrix(bind)
    scales = np.linalg.norm(matrix[:3, :3], axis=1)
    if np.any(scales < 1e-6):
        raise ValueError('Singular PMT bind basis')
    # Leg geometry carries the authored leaf scale. The channel control must
    # not apply that same baked scale a second time.
    basis = matrix[:3, :3] / scales[:, None]
    if not np.allclose(basis @ basis.T, np.eye(3), atol=1e-5):
        raise ValueError('Sheared PMT bind basis')
    if baked_bind_scale:
        matrix[:3, :3] = basis
    keys = []
    for source in track['keys']:
        key = {'duration': source['duration']}
        if 'quaternion_wxyz' in source:
            w, x, y, z = source['quaternion_wxyz']
            q = np.array([-x, y, z, w], dtype=float)
            if np.linalg.norm(q) < 1e-6:
                raise ValueError('Zero PMT quaternion')
            key['rotation'] = (q / np.linalg.norm(q)).tolist()
        if 'translation' in source:
            key['position'] = ((np.array(source['translation']) + np.array(bind[12:15])) *
                               [.1, -.1, -.1]).tolist()
        if 'scale' in source:
            key['scale'] = source['scale']
        keys.append(key)
    return {'flags': flags, 'bind': matrix.ravel().tolist(), 'keys': keys}


def motion_period(source):
    return max((sum(k['duration'] for k in track['keys'][:-1])
                for track in source['tracks']), default=0)
