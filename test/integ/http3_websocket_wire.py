"""Bounded RFC 6455 generator/observer independent of the server and aioquic."""
from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Frame:
    opcode: int
    fin: bool
    payload: bytes
    masked: bool = False


def frame(opcode, payload, *, fin=True, masked=True, key=None):
    if opcode not in (0, 1, 2, 8, 9, 10) or len(payload) > 8192:
        raise ValueError('invalid client frame')
    if opcode >= 8 and (not fin or len(payload) > 125):
        raise ValueError('invalid control frame')
    size = len(payload)
    header = bytes([(0x80 if fin else 0) | opcode, (0x80 if masked else 0) | (size if size < 126 else 126)])
    if size >= 126:
        header += size.to_bytes(2, 'big')
    if not masked:
        return header + payload
    key = os.urandom(4) if key is None else key
    if len(key) != 4:
        raise ValueError('mask must contain four bytes')
    return header + key + bytes(byte ^ key[i % 4] for i, byte in enumerate(payload))


class Decoder:
    def __init__(self, max_payload=8192):
        self.buffer = bytearray()
        self.max_payload = max_payload

    def feed(self, data):
        if len(self.buffer) + len(data) > 1048576:
            raise ValueError('wire accumulation bound')
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 2:
            first, second = self.buffer[:2]
            opcode, fin = first & 15, bool(first & 128)
            if first & 112 or second & 128 or opcode not in (0, 1, 2, 8, 9, 10):
                raise ValueError('server masking, RSV or opcode violation')
            size, at = second & 127, 2
            if opcode >= 8 and (not fin or size > 125):
                raise ValueError('malformed control frame')
            if size >= 126:
                width = 2 if size == 126 else 8
                if len(self.buffer) < 2 + width:
                    break
                size = int.from_bytes(self.buffer[2:2 + width], 'big')
                if size < (126 if width == 2 else 65536):
                    raise ValueError('nonminimal length')
                at += width
            if size > self.max_payload:
                raise ValueError('server payload bound')
            if len(self.buffer) < at + size:
                break
            frames.append(Frame(opcode, fin, bytes(self.buffer[at:at + size])))
            del self.buffer[:at + size]
        return frames

    def finish(self):
        if self.buffer:
            raise ValueError('truncated terminal frame')


def close_payload(payload):
    if not payload:
        return None, ''
    if len(payload) < 2 or len(payload) > 125:
        raise ValueError('malformed close payload')
    code = int.from_bytes(payload[:2], 'big')
    if not (1000 <= code < 5000) or code in (1004, 1005, 1006, 1015) or 1016 <= code < 3000:
        raise ValueError('invalid close code')
    return code, payload[2:].decode('utf-8', errors='strict')


def expect(observed, opcode, payload):
    if (observed.opcode, observed.fin, observed.payload, observed.masked) != (opcode, True, payload, False):
        raise AssertionError('WebSocket opcode/FIN/payload/masking mismatch')
