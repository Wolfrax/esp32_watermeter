#!/usr/bin/env python3
"""Scan an ST25DV NFC memory dump from the water meter for BCD-encoded
daily-history records: [4-byte LE uint32 volume, units of 0.0001 m3]
followed by [4-byte BCD date DD MM YY 00].

Usage: analyze_dump.py dumps/mem2.bin [dumps/other.bin ...]
"""
import struct
import sys


def bcd(b):
    hi, lo = b >> 4, b & 0xF
    if hi > 9 or lo > 9:
        return None
    return hi * 10 + lo


def scan(data):
    print(f"length: {len(data)} bytes ({len(data) / 4:.0f} blocks)")
    print(data.hex())
    print()
    for i in range(len(data) - 3):
        d, m, y, pad = data[i], data[i + 1], data[i + 2], data[i + 3]
        bd, bm, by = bcd(d), bcd(m), bcd(y)
        if bd is None or bm is None or by is None:
            continue
        if not (1 <= bd <= 31 and 1 <= bm <= 12 and 20 <= by <= 30 and pad == 0):
            continue
        before = data[max(0, i - 4):i]
        after = data[i + 4:i + 8]
        le_before = struct.unpack("<I", before)[0] if len(before) == 4 else None
        vol = f"{le_before / 10000:.4f} m3" if le_before is not None else "?"
        print(
            f"offset {i:3d} (0x{i:02x}): 20{by:02d}-{bm:02d}-{bd:02d}  "
            f"volume_field={before.hex()} LE={le_before} ({vol})  "
            f"next_field={after.hex()}"
        )


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        print(f"=== {path} ===")
        with open(path, "rb") as f:
            scan(f.read())
        print()
