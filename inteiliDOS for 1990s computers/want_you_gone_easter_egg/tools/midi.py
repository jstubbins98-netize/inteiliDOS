#!/usr/bin/env python3
"""Convert SMF melody lanes to timed, monophonic PC-speaker data.

Only this build-time tool uses fractional/float arithmetic. The ELF uses
integer frequencies and milliseconds, with no MIDI parser or runtime library.
"""
import bisect
from fractions import Fraction
from pathlib import Path
import struct


def vlq(data, pos):
    value = 0
    for _ in range(4):
        if pos >= len(data):
            raise ValueError("Truncated MIDI variable-length quantity")
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 127)
        if byte < 128:
            return value, pos
    raise ValueError("Invalid MIDI variable-length quantity")


def parse_midi(path):
    data = Path(path).read_bytes()
    if len(data) < 14 or data[:4] != b"MThd":
        raise ValueError("Not a Standard MIDI File")
    length = struct.unpack_from(">I", data, 4)[0]
    fmt, count, division = struct.unpack_from(">HHH", data, 8)
    if fmt not in (0, 1) or division == 0 or division & 0x8000:
        raise ValueError("Only format 0/1 MIDI with PPQN timing is supported")
    pos = 8 + length
    tempos, lanes, names, end_tick = [(0, 500000)], {}, {}, 0
    for track in range(count):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("Missing track chunk")
        size = struct.unpack_from(">I", data, pos + 4)[0]
        chunk = data[pos + 8:pos + 8 + size]
        if len(chunk) != size:
            raise ValueError("Truncated MIDI track")
        pos += size + 8
        cursor, tick, running = 0, 0, None
        while cursor < len(chunk):
            delta, cursor = vlq(chunk, cursor)
            tick += delta
            status = chunk[cursor]
            if status >= 128:
                cursor += 1
                if status < 0xF0:
                    running = status
            elif running is not None:
                status = running
            else:
                raise ValueError("Missing running status")
            if status == 0xFF:
                kind = chunk[cursor]
                cursor += 1
                size, cursor = vlq(chunk, cursor)
                payload = chunk[cursor:cursor + size]
                if len(payload) != size:
                    raise ValueError("Truncated meta event")
                cursor += size
                if kind == 0x51 and size == 3:
                    tempos.append((tick, int.from_bytes(payload, "big")))
                elif kind == 3:
                    names[track] = payload.decode("latin1", errors="replace")
                elif kind == 0x2F:
                    break
            elif status in (0xF0, 0xF7):
                running = None
                size, cursor = vlq(chunk, cursor)
                cursor += size
                if cursor > len(chunk):
                    raise ValueError("Truncated SysEx")
            elif status < 0xF0:
                kind, channel = status >> 4, status & 15
                size = 1 if kind in (12, 13) else 2
                payload = chunk[cursor:cursor + size]
                if len(payload) != size:
                    raise ValueError("Truncated channel event")
                cursor += size
                if channel != 9 and kind in (8, 9):
                    note, velocity = payload
                    on = kind == 9 and velocity != 0
                    lanes.setdefault((track, channel), []).append((tick, note, on))
            else:
                raise ValueError(f"Unsupported status {status:#x}")
        end_tick = max(end_tick, tick)
    return division, sorted(tempos), lanes, names, end_tick


def convert(path):
    division, tempos, lanes, names, end_tick = parse_midi(path)
    counts = {lane: sum(on for _, _, on in events)
              for lane, events in lanes.items()}
    maximum = max(counts.values(), default=0)
    if not maximum:
        raise ValueError("No pitched notes found")
    candidates = [lane for lane in lanes if counts[lane] >= max(1, maximum // 4)]

    def score(lane):
        pitches = [note for _, note, on in lanes[lane] if on]
        name = names.get(lane[0], "").lower()
        return (1000 if "melody" in name else 0) + sum(pitches) / len(pitches)

    lane = max(candidates, key=score)
    # Integrate tempo changes exactly. A later event at the same tick wins.
    tempo_map = dict(tempos)
    starts = sorted(tempo_map)
    cumulative = [Fraction(0)]
    for previous, current in zip(starts, starts[1:]):
        cumulative.append(cumulative[-1] +
                          Fraction((current - previous) * tempo_map[previous],
                                   division * 1000))

    def milliseconds(tick):
        idx = bisect.bisect_right(starts, tick) - 1
        return int(cumulative[idx] +
                   Fraction((tick - starts[idx]) * tempo_map[starts[idx]],
                            division * 1000))

    events = lanes[lane]
    active, last, segments = {}, 0, []
    grouped = {}
    for tick, note, on in events:
        grouped.setdefault(tick, []).append((note, on))
    grouped.setdefault(end_tick, [])
    for tick, changes in sorted(grouped.items()):
        now = milliseconds(tick)
        pitch = max(active, default=None)
        hz = 0 if pitch is None else round(440 * 2 ** ((pitch - 69) / 12))
        duration = now - last
        if duration > 0:
            if hz == 0 and segments and segments[-1][0] == 0:
                segments[-1] = (hz, segments[-1][1] + duration)
            else:
                segments.append((hz, duration))
        for note, on in changes:
            if on:
                active[note] = active.get(note, 0) + 1
            elif note in active:
                active[note] -= 1
                if active[note] == 0:
                    del active[note]
        last = now
    return segments, lane, names.get(lane[0], ""), milliseconds(end_tick)


