"""Convert Sony PS2 MIDI event encoding to standard MIDI track bytes.

The note-off and delta compression rules are corroborated by VGMTrans's
SonyPS2Seq.cpp. Channel/controller values are retained without importing
S.L.A.I.'s runtime sound routing or game logic.
"""


def _vlq(value: int) -> bytes:
    result = [value & 127]
    value >>= 7
    while value:
        result.insert(0, (value & 127) | 128)
        value >>= 7
    return bytes(result)


def convert_events(data: bytes) -> bytes:
    cursor, running, skip_delta = 0, None, False
    output = bytearray()

    def byte():
        nonlocal cursor
        if cursor >= len(data):
            raise ValueError("Truncated Sony MIDI event")
        value = data[cursor]
        cursor += 1
        return value

    def vlq():
        value = 0
        for _ in range(4):
            b = byte()
            value = (value << 7) | (b & 127)
            if b < 128:
                return value
        raise ValueError("Invalid Sony MIDI delta")

    def final_data():
        nonlocal skip_delta
        value = byte()
        skip_delta = bool(value & 128)
        return value & 127

    while cursor < len(data):
        delta = 0 if skip_delta else vlq()
        skip_delta = False
        status = byte()
        if status < 128:
            if running is None:
                raise ValueError("Sony MIDI running status has no channel event")
            cursor -= 1
            status = running
        elif status != 255:
            running = status
        output += _vlq(delta)
        output.append(status)
        kind = status >> 4
        if kind == 8:
            output += bytes([final_data(), 0])
        elif kind in (9, 11, 14):
            first = byte()
            if first >= 128:
                raise ValueError("Invalid first Sony MIDI data byte")
            output += bytes([first, final_data()])
        elif kind == 12:
            output.append(final_data())
        elif status == 255:
            meta = byte()
            output.append(meta)
            if meta == 47:
                output.append(0)
                return bytes(output)
            if meta != 81 or byte() != 3:
                raise ValueError(f"Unsupported Sony MIDI meta event {meta:#x}")
            output.append(3)
            output += bytes([byte(), byte(), byte()])
        else:
            raise ValueError(f"Unsupported Sony MIDI event {status:#x}")
    raise ValueError("Sony MIDI sequence is missing end-of-track")
