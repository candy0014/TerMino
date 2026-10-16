"""Negotiated, bounded WebSocket event batches; wire-compatible with event_stream.h."""
import asyncio
from collections import deque
import struct
import math

PROTOCOL = "termino.events.v1"
MAX_PACKET = 65507
MAX_FRAME = 65536
MAX_EVENTS = 64


def select_subprotocol(connection, offered):
    return PROTOCOL if PROTOCOL in offered else None


def encode(events):
    if not 1 <= len(events) <= MAX_EVENTS:
        raise ValueError("Invalid event count")
    if any(len(event) > MAX_PACKET for event in events):
        raise ValueError("Event too large")
    wire = b"TES1" + struct.pack("!H", len(events))
    wire += b"".join(struct.pack("!I", len(event)) + event for event in events)
    if len(wire) > MAX_FRAME:
        raise ValueError("Event batch too large")
    return wire


def decode(wire):
    if not 6 <= len(wire) <= MAX_FRAME or wire[:4] != b"TES1":
        raise ValueError("Invalid event envelope")
    count = int.from_bytes(wire[4:6], "big")
    if not 1 <= count <= MAX_EVENTS:
        raise ValueError("Invalid event count")
    events, offset = [], 6
    for _ in range(count):
        if offset + 4 > len(wire):
            raise ValueError("Truncated event length")
        size = int.from_bytes(wire[offset:offset + 4], "big")
        offset += 4
        if size > MAX_PACKET or offset + size > len(wire):
            raise ValueError("Truncated or oversized event")
        events.append(wire[offset:offset + size])
        offset += size
    if offset != len(wire):
        raise ValueError("Trailing event data")
    return events


def visual(wire):
    owner = b""
    fields = 2 if wire.startswith(b"!match;") else 3 if wire.startswith(b"!player;") else 0
    if fields:
        parts = wire.split(b";", fields)
        if len(parts) != fields + 1:
            return None, 0
        owner, wire = b";".join(parts[:-1]) + b";", parts[-1]
    try:
        if wire.startswith(b"!motion-v1;") and 25 <= len(wire) <= 128:
            offset = len(b"!motion-v1;") + 4

            def varint():
                nonlocal offset
                value = 0
                for i in range(10):
                    byte = wire[offset]
                    offset += 1
                    if i == 9 and byte & 254:
                        raise ValueError("Varint overflow")
                    value |= (byte & 127) << (7 * i)
                    if not byte & 128:
                        if i and not byte:
                            raise ValueError("Overlong varint")
                        return value
                raise ValueError("Invalid varint")

            sequence, frame, revision = varint(), varint(), varint()
            fingerprint = wire[offset:offset + 8]
            offset += 8
            piece = varint()
            kind = varint()
            signed = []
            for _ in range(5):
                value = varint()
                if value > 0xffffffff:
                    raise ValueError("Signed varint overflow")
                signed.append(-(value >> 1) - 1 if value & 1 else value >> 1)
            rotation = varint()
            rows, columns, turns, row, column = signed
            valid = (kind <= 1 and rotation <= 3 and -400 <= rows <= 400 and -210 <= columns <= 210
                     and -3 <= turns <= 3 and -210 <= row <= 210 and -105 <= column <= 210
                     and ((kind == 0 and turns == 0) or (kind == 1 and turns != 0)))
            if offset < len(wire):
                if len(wire) - offset != 17 or wire[offset] != 1:
                    return None, 0
                valid = valid and all(math.isfinite(v) and 0 <= v <= 1e18 for v in struct.unpack("!dd", wire[offset + 1:]))
            if valid and sequence and revision and frame <= 0xffffffff and len(fingerprint) == 8:
                return (owner, b"motion", wire[11:15], revision, fingerprint, piece), sequence
        if wire.startswith(b"!mini-v1;") and len(wire) <= 33000:
            fields = wire.split(b";", 7)
            sequence, width, height, columns, rows, flags = map(int, fields[1:7])
            body = fields[7]
            if not (0 < sequence <= 0xffffffffffffffff and 1 <= width <= 100 and 1 <= height <= 100
                    and 1 <= columns <= width * 2 and 1 <= rows <= 160 and 0 <= flags <= 3):
                return None, 0
            if flags & 2:
                if len(body) < 16 or not all(math.isfinite(v) and 0 <= v <= 1e18 for v in struct.unpack("!dd", body[:16])):
                    return None, 0
                body = body[16:]
            if flags & 1:
                valid = (len(body) % 2 == 0 and all(body[i] and body[i + 1] <= 11 for i in range(0, len(body), 2))
                         and sum(body[::2]) == columns * rows)
            else:
                valid = len(body) == columns * rows and all(cell <= 11 for cell in body)
            if valid:
                return (owner, b"mini"), sequence
    except (IndexError, ValueError, struct.error):
        pass
    return None, 0


class EventQueue:
    def __init__(self):
        self.events = deque()
        self.bytes = 0
        self.available = asyncio.Event()

    def push(self, data, now, stream):
        if len(data) > MAX_PACKET:
            return False
        key, sequence = visual(data) if stream else (None, 0)
        if key is not None and key[1] == b"mini":
            old = [event for event in self.events if event[1] == key]
            if any(sequence < event[2] for event in old):
                return True
            size = self.bytes - sum(len(event[0]) for event in old) + len(data)
            if size > 2 * 1024 * 1024 or len(self.events) - len(old) >= 1024:
                return False
            self.events = deque(event for event in self.events if event[1] != key)
            self.bytes = size
            self.events.append([data, key, sequence, now])
            self.available.set()
            return True
        if key is not None:
            for event in reversed(self.events):
                if event[1] is None:
                    break
                if event[1] != key:
                    continue
                if sequence < event[2]:
                    return True
                size = self.bytes - len(event[0]) + len(data)
                if size > 2 * 1024 * 1024:
                    return False
                self.bytes = size
                event[0], event[2] = data, sequence
                self.available.set()
                return True
        if len(self.events) >= 1024 or self.bytes + len(data) > 2 * 1024 * 1024:
            return False
        self.bytes += len(data)
        self.events.append([data, key, sequence, now])
        self.available.set()
        return True

    async def take(self, stream):
        # Coalesce only messages already queued by the event loop/backpressure.
        # An isolated visual update must not wait for another batching window.
        while not self.events:
            self.available.clear()
            await self.available.wait()
        events, size = [], 6
        while self.events and len(events) < (MAX_EVENTS if stream else 1):
            if stream and size + 4 + len(self.events[0][0]) > MAX_FRAME:
                break
            data = self.events.popleft()[0]
            self.bytes -= len(data)
            size += 4 + len(data)
            events.append(data)
        return events
