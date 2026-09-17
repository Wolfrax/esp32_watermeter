#!/usr/bin/env python3
"""Scan an ST25DV NFC memory dump from the water meter for BCD-encoded
daily-history records: [4-byte LE uint32 volume, units of 0.001 m3 (1 L)]
followed by [4-byte BCD date DD MM YY 00]. (Units corrected 2026-09-17
— cross-checked against a physical LCD photo; the original 0.0001 m3
assumption was off by 10x, see watermeter.c.)

Usage:
  analyze_dump.py dumps/mem2.bin [dumps/other.bin ...]
  analyze_dump.py --diff dumps/day1.bin dumps/day2.bin [dumps/day3.bin ...]

--diff compares consecutive dumps byte-by-byte (in the order given, so
pass them chronologically) to find what changes day-to-day beyond the
known volume/date record at WATERMETER_VOLUME_BLOCK/WATERMETER_DATE_BLOCK
(see globals.h) — the open question from findings.md's "Not yet decoded"
section. A byte range that never differs across dumps spanning several
days is probably static config/serial data, not live measurement data.
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
        vol = f"{le_before / 1000:.3f} m3" if le_before is not None else "?"
        print(
            f"offset {i:3d} (0x{i:02x}): 20{by:02d}-{bm:02d}-{bd:02d}  "
            f"volume_field={before.hex()} LE={le_before} ({vol})  "
            f"next_field={after.hex()}"
        )


def _group_runs(offsets):
    runs = []
    start = prev = offsets[0]
    for j in offsets[1:]:
        if j == prev + 1:
            prev = j
            continue
        runs.append((start, prev))
        start = prev = j
    runs.append((start, prev))
    return runs


def _interpret(old, new):
    """Best-effort guess at what a same-length differing hunk represents."""
    guesses = []
    if len(old) == 4:
        lo, ln = struct.unpack("<I", old)[0], struct.unpack("<I", new)[0]
        guesses.append(
            f"LE uint32: {lo} -> {ln} (delta {ln - lo:+d}; if x0.001 m3: "
            f"{lo / 1000:.3f} -> {ln / 1000:.3f})"
        )
        od, om, oy, opad = old
        nd, nm, ny, npad = new
        bod, bom, boy = bcd(od), bcd(om), bcd(oy)
        bnd, bnm, bny = bcd(nd), bcd(nm), bcd(ny)
        if (
            opad == 0
            and npad == 0
            and None not in (bod, bom, boy)
            and None not in (bnd, bnm, bny)
        ):
            guesses.append(
                f"BCD date: 20{boy:02d}-{bom:02d}-{bod:02d} -> "
                f"20{bny:02d}-{bnm:02d}-{bnd:02d}"
            )
    return guesses


def diff(paths):
    if len(paths) < 2:
        sys.exit("--diff needs at least 2 dump files")
    datas = []
    for p in paths:
        with open(p, "rb") as f:
            datas.append(f.read())
    n = min(len(d) for d in datas)

    for i in range(len(datas) - 1):
        a, b = datas[i], datas[i + 1]
        print(f"=== {paths[i]} -> {paths[i + 1]} ===")
        offsets = [j for j in range(n) if a[j] != b[j]]
        if not offsets:
            print(f"  identical across first {n} bytes")
            print()
            continue
        for start, end in _group_runs(offsets):
            length = end - start + 1
            old, new = a[start:end + 1], b[start:end + 1]
            print(f"  offset {start:3d}-{end:3d} (0x{start:02x}-0x{end:02x}, {length}B): "
                  f"{old.hex()} -> {new.hex()}")
            for guess in _interpret(old, new):
                print(f"      {guess}")
        print(f"  ({len(offsets)} byte(s) differ out of {n})")
        print()


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if args[0] == "--diff":
        diff(args[1:])
    else:
        for path in args:
            print(f"=== {path} ===")
            with open(path, "rb") as f:
                scan(f.read())
            print()
