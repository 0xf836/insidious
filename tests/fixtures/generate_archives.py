#!/usr/bin/env python3
"""Generate original MIT-licensed stored RAR4/RAR5 fixtures using only stdlib.

RAR5 layout: https://www.rarlab.com/technote.htm
RAR4 layout corresponds to the public UnRAR header structures.
No RAR compressor or third-party fixture content is included.
"""
from pathlib import Path
import struct
import zlib


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def png(color):
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', crc(name + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 3, 2, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + bytes(color) * 3) * 2)) + chunk(b'IEND', b''))


def vint(value):
    data = bytearray()
    while value >= 128:
        data.append((value & 127) | 128)
        value >>= 7
    return bytes(data) + bytes([value])


def rar5_header(payload):
    data = vint(len(payload)) + payload
    return struct.pack('<I', crc(data)) + data


def rar4_header(kind, flags, payload):
    data = struct.pack('<BHH', kind, flags, len(payload) + 7) + payload
    return struct.pack('<H', crc(data) & 0xffff) + data


def main():
    files = [(b'nested/green.png', png((0, 255, 0))),
             (b'nested/red.png', png((255, 0, 0))), (b'notes.txt', b'original test fixture\n')]
    rar4 = b'Rar!\x1a\x07\x00' + rar4_header(0x73, 0, bytes(6))
    rar5 = b'Rar!\x1a\x07\x01\x00' + rar5_header(b'\x01\x00\x00')
    for name, data in files:
        fields = struct.pack('<IIBIIBBHI', len(data), len(data), 3, crc(data), 0, 20, 0x30, len(name), 0o100644)
        rar4 += rar4_header(0x74, 0x8000, fields + name) + data
        payload = (vint(2) + vint(2) + vint(len(data)) + vint(4) + vint(len(data))
                   + vint(0o100644) + struct.pack('<I', crc(data)) + vint(0) + vint(1)
                   + vint(len(name)) + name)
        rar5 += rar5_header(payload) + data
    rar4 += rar4_header(0x7b, 0, b'')
    rar5 += rar5_header(b'\x05\x00\x00')
    root = Path(__file__).parent
    (root / 'images-rar4.rar').write_bytes(rar4)
    (root / 'images-rar5.rar').write_bytes(rar5)


if __name__ == '__main__':
    main()
