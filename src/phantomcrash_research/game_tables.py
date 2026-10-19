"""Data tables read from the European ``default.xbe``.

The addresses below belong to that exact build (SHA-256 in
``docs/EUROPE_BASELINE.md``); :func:`load` refuses any other executable.
See ``docs/OBJECTIVE_01_PLAN.md`` for how the game uses each table.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
import unicodedata

from . import xbe

EUROPE_XBE_SHA256 = "bdbe2c3eb2361395babfe6095d1a2da72f6581dccb34812b8d45e084c6c903c6"

MUSIC_NAMES = 0x254480  # char *[], WAV names in Media\sound
SCENE_MUSIC = 0x256230  # int32[SCENE_COUNT], index into MUSIC_NAMES or -1
SCENE_COUNT = 159
MOVIES = 0x259524  # char *[MOVIE_COUNT], "d:\media\movie\*.sfd"
MOVIE_COUNT = 11
SPRITES = 0x276680  # {SpriteDefinition *, texture slot}[SPRITE_COUNT]
SPRITE_COUNT = 0xEB3  # bound checked by the draw call at 0x0bbb97
RESOURCES = 0x285E40  # 24-byte records, indexed by resource ID
RESOURCE_COUNT = 1927
BINLINK_PACKS = 0x1C3E98  # {disc path, cache path}[], packs on Media\binlink
FONT_CHARACTERS = 0x27F114  # char *: Shift-JIS characters in glyph order (0x0bde54)
FONT_METRICS = 0x1C3FB0  # {int8 left, uint8 width}[FONT_METRICS_COUNT] (0x0be4b0)
FONT_METRICS_COUNT = 0x184  # later glyphs fill their 18-texel cell
MUSIC_CREDITS = 0x1EFD38  # 0x1ac-byte records: int32 track, title @4, artist @0x69
MUSIC_CREDIT_COUNT = 0x51  # the lookup at 0x02a407 stops here
VOLUME_MILLIBELS = 0x410400  # int16[256]: DirectSound volume for a 0-255 level (0x0dd9d0)

RESOURCE_MODEL = 0
RESOURCE_TEXTURE_PACK = 3


class GameTablesError(ValueError):
    """The executable is not the build these addresses describe."""


@dataclass(frozen=True)
class Resource:
    """One loadable resource: a file on the disc or a slice of a binlink pack."""

    id: int
    path: str | None
    size: int
    type: int
    first_slot: int | None  # texture packs: first texture slot they fill
    slot_count: int | None
    binlink_pack: int | None
    binlink_offset: int | None
    # Models: the texture pack their batches index ({first slot, meshes, pack}
    # record read by the loader at 0x0af17d), or None.
    texture_resource: int | None = None
    # Models: the first model slot their meshes fill, one mesh a slot, and
    # how many (the same record).
    first_mesh_slot: int | None = None
    mesh_count: int | None = None


@dataclass(frozen=True)
class Sprite:
    """A rectangle of a texture slot, in texels of a ``texture_width`` x ``texture_height`` texture."""

    id: int
    slot: int
    left: float
    top: float
    right: float
    bottom: float
    texture_width: float
    texture_height: float


@dataclass(frozen=True)
class MusicCredit:
    """Title and artist the option screen scrolls for a track (0x02a85a),
    the sprites of its CD jacket, large and small (+ 0x198, 0x02bc93), and
    of the artist's name and web address Sonic Amp's preview shows (+ 0x1a4
    and + 0x1a8, 0x029fdc)."""

    track: int
    title: str
    artist: str
    jacket: int = -1
    jacket_small: int = -1
    artist_name: int = -1
    artist_link: int = -1
    artist_profile: int = 0  # text script at +0x1a0, shared by an artist's tracks


class _Image:
    def __init__(self, data: bytes):
        self.data = data
        self.sections = xbe.parse(data).sections

    def offset(self, address: int, size: int = 4) -> int:
        for section in self.sections:
            start = section.virtual_address
            if start <= address and address + size <= start + section.raw_size:
                return section.raw_address + address - start
        raise GameTablesError(f"Address 0x{address:08x} is not in the file image")

    def u32(self, address: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset(address))[0]

    def string(self, address: int) -> str | None:
        """The printable C string at ``address``; None for null, unmapped or binary data."""
        try:
            start = self.offset(address, 1)
        except GameTablesError:
            return None
        end = self.data.find(b"\0", start)
        text = self.data[start:end]
        if not text or any(byte < 0x20 or byte > 0x7E for byte in text):
            return None
        return text.decode("latin-1")

    def raw_string(self, address: int) -> bytes:
        start = self.offset(address, 1)
        return self.data[start : self.data.find(b"\0", start)]


def _media_relative(path: str) -> str:
    """``d:\\media\\movie\\x.sfd`` -> ``movie/x.sfd`` (relative to ``Media``)."""
    parts = path.replace("\\", "/").split("/")
    lowered = [part.lower() for part in parts]
    if "media" in lowered:
        parts = parts[lowered.index("media") + 1 :]
    return "/".join(parts)


@dataclass
class GameTables:
    music: list[str]
    scene_music: list[int]
    movies: list[str]
    binlink_packs: list[str]
    resources: list[Resource]
    sprites: list[Sprite]
    font_characters: str  # glyph N of the system font draws character N
    font_metrics: list[tuple[int, int]]  # (left, width) in texels within the cell
    music_credits: list[MusicCredit]
    volume_millibels: list[int]  # BGM and SE level 0-255 -> hundredths of a decibel

    def texture_for_slot(self, slot: int) -> tuple[Resource, int] | None:
        """The disc texture pack (and texture index in it) that fills ``slot``.

        Binlink resources that share a slot are swapped in at run time, so only
        packs with their own file on the disc are considered.
        """
        for resource in self.resources:
            if resource.path is None or resource.first_slot is None:
                continue
            if resource.first_slot <= slot < resource.first_slot + resource.slot_count:
                return resource, slot - resource.first_slot
        return None


def sprite_binlink_packs(tables: GameTables) -> list[Resource]:
    """The binlink texture packs that fill a slot some sprite draws from
    (resource 1658: the music jackets and artist portraits, 0x02bc93)."""
    used = {sprite.slot for sprite in tables.sprites}
    return [
        resource
        for resource in tables.resources
        if resource.binlink_pack is not None
        and resource.first_slot is not None
        and any(resource.first_slot + index in used for index in range(resource.slot_count))
    ]


def _resources(image: _Image) -> list[Resource]:
    resources = []
    for index in range(RESOURCE_COUNT):
        base = RESOURCES + index * 24
        path_address, size, kind, slots, pack, pack_offset = struct.unpack_from(
            "<6I", image.data, image.offset(base, 24)
        )
        path = image.string(path_address)
        first_slot = slot_count = texture_resource = first_mesh_slot = mesh_count = None
        if kind == RESOURCE_TEXTURE_PACK and slots:
            first_slot, slot_count = struct.unpack_from("<2I", image.data, image.offset(slots, 8))
        if kind == RESOURCE_MODEL and slots:
            first_mesh_slot, mesh_count, pack_id = struct.unpack_from("<3I", image.data, image.offset(slots, 12))
            texture_resource = None if pack_id == 0xFFFFFFFF else pack_id
        in_pack = path is None and pack != 0xFFFFFFFF
        resources.append(
            Resource(
                id=index,
                path=_media_relative(path) if path else None,
                size=size,
                type=kind,
                first_slot=first_slot,
                slot_count=slot_count,
                binlink_pack=pack if in_pack else None,
                binlink_offset=pack_offset if in_pack else None,
                texture_resource=texture_resource,
                first_mesh_slot=first_mesh_slot,
                mesh_count=mesh_count,
            )
        )
    return resources


def _sprites(image: _Image) -> list[Sprite]:
    sprites = []
    for index in range(SPRITE_COUNT):
        definition, slot = struct.unpack_from("<2I", image.data, image.offset(SPRITES + index * 8, 8))
        values = struct.unpack_from("<6f", image.data, image.offset(definition, 24))
        sprites.append(Sprite(index, slot, *values))
    return sprites


def _font_characters(image: _Image) -> str:
    raw = image.raw_string(image.u32(FONT_CHARACTERS))
    # One pair (glyph 829, 0x83bd) is not valid Shift-JIS; U+FFFD keeps the
    # later glyphs at their index.
    pairs = (raw[index : index + 2] for index in range(0, len(raw) - 1, 2))
    return "".join(pair.decode("cp932", errors="replace")[:1] for pair in pairs)


def _font_metrics(image: _Image) -> list[tuple[int, int]]:
    count = FONT_METRICS_COUNT * 2
    values = struct.unpack_from(f"<{count}b", image.data, image.offset(FONT_METRICS, count))
    return [(values[index], values[index + 1] & 0xFF) for index in range(0, count, 2)]


def _plain(raw: bytes) -> str:
    """Full-width Shift-JIS text as plain Unicode (NFKC), without its padding."""
    return unicodedata.normalize("NFKC", raw.decode("cp932")).strip()


def _music_credits(image: _Image) -> list[MusicCredit]:
    credits = []
    for index in range(MUSIC_CREDIT_COUNT):
        base = MUSIC_CREDITS + index * 0x1AC
        track = struct.unpack_from("<i", image.data, image.offset(base))[0]
        if track >= 0:
            title, artist = image.raw_string(base + 4), image.raw_string(base + 0x69)
            large, small, profile, name, link = struct.unpack_from("<5i", image.data, image.offset(base + 0x198, 20))
            credits.append(MusicCredit(track, _plain(title), _plain(artist), large, small, name, link, profile))
    return credits


def _string_table(image: _Image, address: int, stride: int = 4, limit: int = 4096) -> list[str]:
    names = []
    for index in range(limit):
        name = image.string(image.u32(address + index * stride))
        if name is None:
            break
        names.append(name)
    return names


def load(data: bytes) -> GameTables:
    if hashlib.sha256(data).hexdigest() != EUROPE_XBE_SHA256:
        raise GameTablesError("Not the European default.xbe these table addresses describe")
    image = _Image(data)
    scene_music = list(
        struct.unpack_from(f"<{SCENE_COUNT}i", data, image.offset(SCENE_MUSIC, SCENE_COUNT * 4))
    )
    movies = [_media_relative(image.string(image.u32(MOVIES + 4 * i))) for i in range(MOVIE_COUNT)]
    packs = [
        _media_relative(path)
        for path in _string_table(image, BINLINK_PACKS, stride=8)
        if path.lower().startswith("d:\\")
    ]
    return GameTables(
        music=_string_table(image, MUSIC_NAMES),
        scene_music=scene_music,
        movies=movies,
        binlink_packs=packs,
        resources=_resources(image),
        sprites=_sprites(image),
        font_characters=_font_characters(image),
        font_metrics=_font_metrics(image),
        music_credits=_music_credits(image),
        volume_millibels=list(
            struct.unpack_from("<256h", data, image.offset(VOLUME_MILLIBELS, 512))
        ),
    )
