"""Story events (talk scenes) read from the European ``default.xbe``.

Each story event is a scene (48-158) that plays one command script from the
``EVENTSEG`` section over a talk background, with up to six talking
characters. The event table says when an event plays: on the first move from
one scene to another (``0x0d1861``), in place of the scene it leads to.

Scripts are arrays of 13-int commands (``0x34`` bytes) run by ``0x0c5a1a``;
every frame the interpreter re-runs the commands since the last completed
wait, so sprites, characters and text are drawn again each frame. The
opcodes used by the shipped events:

- 1 text: a pair of records, the ruby line then the text (``0x0c5b9d``);
- 2 wait a number of frames (``0x0c604b``);
- 3 sprite (``0x0c620c``);
- 4 talking character in a pose, with fades, blinks and mouth (``0x0c62ff``);
- 5 hold: wait a number of frames, keeping the page drawn; holding the
  fast-forward buttons (mask ``0xc0c0``) cuts it short, and once passed it
  is not waited for again (``0x0c6a0f``; only Pepper's in-battle lessons
  use it);
- 10 wait for confirm (``0x0c6bc4``);
- 13 end.

See ``docs/NEW_GAME.md`` for the details the port reproduces.
"""

from __future__ import annotations

import functools
import struct

from .game_tables import GameTables, _Image

EVENT_TABLE = 0x1D0E00  # 0x24-byte records, looked up by scene id (0x0d113b)
EVENT_COUNT = 0x6E
EVENT_RECORD = 0x24
SCENE_FACTORIES = 0x256738  # void (*)(task)[159], the scene table
TALK_CHARACTERS = 0x1C42E8  # {int resource; int pose[9]}[TALK_CHARACTER_COUNT] (0x0c79c0)
TALK_CHARACTER_COUNT = 0x1C
TALK_POSES = 0x1C4748  # 0x1c-byte records (0x0c79e3)
TALK_POSE_COUNT = 0xB4
TALK_BACKGROUNDS = 0x1D0DD0  # {int back sprite; int front sprite}[6] (0x0d0fa2)
TALK_BACKGROUND_COUNT = 6
TALK_FRONT_Y = 296.0  # 0x43940000, where the front strip is drawn
TALK_FRONT_LAYER = 0x101
CHARACTER_SLOT = 534  # talk characters' textures share slots 534-545 (0x0d106a)
CHARACTER_SLOTS = 12
CREATE_EVENT_TASK = 0x0D19EA  # (flags, task, start, end)
RUN_SCRIPT = 0x0D0F07  # (task, script, then)
EVENTSEG = "EVENTSEG"
COMMAND_INTS = 13
TEXT_BIT_NO_SKIP = 0x80000000
MUSIC_KEEP = 0xCA  # the event table's "leave the music playing" value (0x0d1615)
TEXT_END = 0xFFFF

# Text codes of the text drawer (0x0bd720), as u16 values.
TEXT_NEWLINE = 0xFFFE
TEXT_PLAYER_NAME = 0xFFFB  # the player's name, save + 0x280c (0x06d78d)
TEXT_RESET_COLOUR = 0xFFF7
TEXT_COLOURS = {
    0xFFEE: "#256584",
    0xFFEF: "#eeee66",
    0xFFF0: "#66ee33",
    0xFFF1: "#8e4500",
    0xFFF2: "#308080",
    0xFFF3: "#808080",
    0xFFF4: "#ee88ee",
    0xFFF5: "#ee9500",
    0xFFF6: "#66eeee",
}


def _ints(image: _Image, address: int, count: int) -> list[int]:
    """Ints at ``address``; a section's memory past its file data reads as zeros."""
    for section in image.sections:
        start = section.virtual_address
        if start <= address and address + 4 * count <= start + section.virtual_size:
            within = address - start
            available = max(0, min(4 * count, section.raw_size - within))
            offset = section.raw_address + within
            chunk = image.data[offset : offset + available] + bytes(4 * count - available)
            return list(struct.unpack(f"<{count}i", chunk))
    raise ValueError(f"Address 0x{address:08x} is not in the executable's memory")


def _section(image: _Image, name: str):
    return next(section for section in image.sections if section.name == name)


def _colour(value: int) -> str:
    return f"#{value & 0xFFFFFF:06x}"


class _Code:
    """Byte patterns in the event scenes' small, compiler-generated functions."""

    WINDOW = 0x60  # bytes searched from a function's start

    def __init__(self, image: _Image):
        self.image = image

    def _bytes(self, address: int) -> tuple[bytes, int]:
        start = self.image.offset(address, 1)
        return self.image.data[start : start + self.WINDOW], address

    def calls(self, address: int) -> list[tuple[int, int]]:
        """(offset, target) of each ``call rel32`` near the start of a function."""
        code, base = self._bytes(address)
        found = []
        for index in range(len(code) - 4):
            if code[index] == 0xE8:
                rel = struct.unpack_from("<i", code, index + 1)[0]
                found.append((index, base + index + 5 + rel))
        return found

    def pushes(self, address: int, before: int) -> list[int]:
        """Immediates of ``push imm32`` before offset ``before``."""
        code, _ = self._bytes(address)
        found = []
        index = 0
        while index + 5 <= before:
            if code[index] == 0x68:
                found.append(struct.unpack_from("<I", code, index + 1)[0])
                index += 5  # an immediate can hold 0x68 bytes too
            else:
                index += 1
        return found

    def next_state(self, address: int) -> int | None:
        """The ``mov dword ptr [reg + 8], imm32`` that sets a task's next function."""
        code, _ = self._bytes(address)
        for index in range(len(code) - 6):
            if code[index] == 0xC7 and 0x40 <= code[index + 1] <= 0x47 and code[index + 2] == 8:
                return struct.unpack_from("<I", code, index + 3)[0]
        return None


def _script_address(image: _Image, scene: int, segment) -> int:
    """The script an event scene runs: factory -> event creator -> start -> script."""
    code = _Code(image)
    factory = image.u32(SCENE_FACTORIES + 4 * scene)
    for _, creator in code.calls(factory):
        created = [offset for offset, target in code.calls(creator) if target == CREATE_EVENT_TASK]
        if not created:
            continue
        start = code.pushes(creator, created[0])[-1]
        runner = code.next_state(start)
        for offset, target in code.calls(runner):
            if target == RUN_SCRIPT:
                script = code.pushes(runner, offset)[-1]
                low = segment.virtual_address
                if low <= script < low + segment.raw_size:
                    return script
    raise ValueError(f"No event script found for scene {scene}")


@functools.cache
def _first_glyphs(characters: str) -> dict[str, int]:
    """Each character's first glyph; the port maps text to glyphs the same way."""
    first: dict[str, int] = {}
    for index, character in enumerate(characters):
        first.setdefault(character, index)
    return first


def _text(image: _Image, tables: GameTables, address: int) -> str:
    """A u16 glyph string as Unicode with markup for the codes."""
    characters = tables.font_characters
    first = _first_glyphs(characters)
    out = []
    offset = image.offset(address, 2)
    while True:
        (glyph,) = struct.unpack_from("<H", image.data, offset)
        offset += 2
        if glyph == TEXT_END:
            return "".join(out)
        if glyph == TEXT_NEWLINE:
            out.append("\n")
        elif glyph == TEXT_PLAYER_NAME:
            out.append("{name}")
        elif glyph == TEXT_RESET_COLOUR:
            out.append("{/}")
        elif glyph in TEXT_COLOURS:
            out.append("{" + TEXT_COLOURS[glyph] + "}")
        elif glyph >= 0xFFE0:
            out.append(f"{{code:{glyph:04x}}}")
        elif glyph < len(characters) and first[characters[glyph]] == glyph:
            character = characters[glyph]
            out.append("{{" if character == "{" else character)
        else:
            out.append(f"{{glyph:{glyph}}}")


def sprite_json(tables: GameTables, sprite_id: int) -> dict:
    """A fixed sprite as an image and a rectangle, for the port's data files."""
    sprite = tables.sprites[sprite_id]
    found = tables.texture_for_slot(sprite.slot)
    image = None
    if found and not CHARACTER_SLOT <= sprite.slot < CHARACTER_SLOT + CHARACTER_SLOTS:
        resource, index = found
        image = f"textures/{resource.path.rsplit('.', 1)[0]}/{index}.png"
    return {
        "sprite": sprite_id,
        "image": image,
        "rect": [sprite.left, sprite.top, sprite.right, sprite.bottom],
    }


def _command(image: _Image, tables: GameTables, values: list[int], ruby: list[int] | None):
    op = values[0]
    if op == 1:
        flags = values[3] & 0xFFFFFFFF
        command = {
            "op": "text",
            "text": _text(image, tables, values[1] & 0xFFFFFFFF),
            "position": values[4:6],
            "colour": _colour(values[6]),
            "frames_per_character": values[2],
            "size": values[7:9],
            "layer": TALK_FRONT_LAYER + values[9],
        }
        if flags & 0x20:
            command["animation"] = "fade"  # the draw flag 0x20, the page-end marker
        if flags & TEXT_BIT_NO_SKIP:
            command["skippable"] = False
        if ruby is not None:
            ruby_text = _text(image, tables, ruby[1] & 0xFFFFFFFF)
            if ruby_text.strip("　 "):
                command["ruby"] = ruby_text
        return command
    if op == 2:
        return {"op": "wait", "frames": values[3]}
    if op == 3:
        # A negative id is a sprite chosen at run time (0x0c6ec5).
        found = sprite_json(tables, values[1]) if values[1] >= 0 else {"sprite": values[1]}
        command = {"op": "sprite", **found}
        command.update(position=values[2:4], layer=values[4], colour=_colour(values[5]))
        alpha = (values[5] >> 24) & 0xFF
        if alpha not in (0, 0xFF):
            command["alpha"] = round(alpha / 255, 4)  # Pepper's lesson window: 0x80
        return command
    if op == 4:
        command = {
            "op": "character",
            "character": values[1],
            "pose": values[2],
            "position": values[3:5],
            "layer": values[5],
            "colour": _colour(values[6]),
        }
        if values[7] & 1:
            command["fade"] = "in"
        elif values[7] & 2:
            command["fade"] = "out"
        if values[7] & 4:
            command["mouth"] = False
        return command
    if op == 5:
        return {"op": "hold", "frames": values[1]}
    if op == 10:
        return {"op": "wait_for_confirm"}
    raise ValueError(f"Unknown event opcode {op}")


def commands(image: _Image, tables: GameTables, address: int) -> list[dict]:
    """Decode a script up to its end command; text records are merged in pairs."""
    raw = []
    while True:
        values = _ints(image, address, COMMAND_INTS)
        if values[0] == 13:
            break
        raw.append(values)
        address += COMMAND_INTS * 4
    commands = []
    index = 0
    while index < len(raw):
        values = raw[index]
        # 0x0c5b9d draws every other text record: the ruby, then the text.
        if values[0] == 1 and index + 1 < len(raw) and raw[index + 1][0] == 1:
            commands.append(_command(image, tables, raw[index + 1], values))
            index += 2
            continue
        commands.append(_command(image, tables, values, None))
        index += 1
    return commands


def _pose(tables: GameTables, image: _Image, pose_id: int) -> dict:
    address = TALK_POSES + pose_id * 0x1C
    body, *faces = _ints(image, address, 5)
    offsets = image.data[image.offset(address + 20, 8) : image.offset(address + 20, 8) + 8]

    def part(sprite_id: int, offset: int | None = None):
        if sprite_id < 0:
            return None
        sprite = tables.sprites[sprite_id]
        entry = {
            "page": sprite.slot - CHARACTER_SLOT,
            "rect": [sprite.left, sprite.top, sprite.right, sprite.bottom],
        }
        if offset is not None:
            entry["offset"] = [offsets[offset], offsets[offset + 1]]
        return entry

    return {
        "body": part(body),
        "mouth": [part(faces[index], 2 * index) for index in range(3)],
        "blink": part(faces[3], 6),
    }


def talk_tables(data: bytes, tables: GameTables) -> dict:
    """Backgrounds and characters of the talk scenes."""
    image = _Image(data)
    backgrounds = []
    for index in range(TALK_BACKGROUND_COUNT):
        back, front = _ints(image, TALK_BACKGROUNDS + 8 * index, 2)
        backgrounds.append(
            {
                "back": {**sprite_json(tables, back), "position": [0, 0], "layer": 0},
                "front": {
                    **sprite_json(tables, front),
                    "position": [0, TALK_FRONT_Y],
                    "layer": TALK_FRONT_LAYER,
                },
            }
        )
    characters = []
    for index in range(TALK_CHARACTER_COUNT):
        resource, *poses = _ints(image, TALK_CHARACTERS + 0x28 * index, 10)
        path = tables.resources[resource].path.rsplit(".", 1)[0]
        characters.append(
            {
                "id": index,
                "images": [
                    f"textures/{path}/{page}.png"
                    for page in range(tables.resources[resource].slot_count)
                ],
                # A missing pose shows the first one (0x0c7a77).
                "poses": [_pose(tables, image, pose) if pose >= 0 else None for pose in poses],
            }
        )
    return {"backgrounds": backgrounds, "characters": characters}


def events(data: bytes, tables: GameTables) -> list[dict]:
    """Every story event with its trigger, background, cast, music and script."""
    image = _Image(data)
    segment = _section(image, EVENTSEG)
    records = []
    scenes = [_ints(image, EVENT_TABLE + index * EVENT_RECORD, 1)[0] for index in range(EVENT_COUNT)]
    for index in range(EVENT_COUNT):
        base = EVENT_TABLE + index * EVENT_RECORD
        scene, from_scene, to_scene = _ints(image, base, 3)
        raw = image.data[image.offset(base, EVENT_RECORD) : image.offset(base, EVENT_RECORD) + EVENT_RECORD]
        music = struct.unpack_from("<h", raw, 0x1E)[0]
        volume = struct.unpack_from("<f", raw, 0x20)[0]
        cast = [value for value in struct.unpack_from("<6b", raw, 0x17)]
        cast = cast[: next((k for k, value in enumerate(cast) if value < 0), len(cast))]
        script = _script_address(image, scene, segment)
        # Events whose table index is in [first, last] must have played (0 = none).
        first, last = raw[0x0C], raw[0x0D]
        required = range(min(first, last), max(first, last) + 1) if first and last else range(0)
        record = {
            "scene": scene,
            "from_scene": from_scene,
            "to_scene": to_scene,
            "enabled": raw[0x14] != 0,
            "requires_seen": [scenes[index] for index in required],
            # Checks 0x0d1861 also makes (a condition function, the day, the
            # garage...); not decoded yet.
            "condition_bytes": list(raw[0x0E:0x14]),
            "background": struct.unpack_from("<b", raw, 0x16)[0],
            "cast": cast,
            "music": None if music < 0 else ("keep" if music == MUSIC_KEEP else tables.music[music]),
            "music_volume": round(volume, 3),
            "script": f"0x{script:x}",
            "commands": commands(image, tables, script),
        }
        records.append(record)
    return records
