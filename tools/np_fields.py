"""Compares two peers' per-frame field logs (fields.d4f, DOA4_NP_TEST_FIELDS, src/net/np_core.h) by value, on every frame both wrote.
A digest holds hashes; this answers "what differed, and when".

  py -3 -I tools/np_fields.py RUN_A RUN_B [--field NAME:ADDR:LEN ...] [--from F]
RUN_A/RUN_B are session folders (holding fields.d4f). Without --field it compares the fighters' fields (FIGHTER_FIELDS), which a run logs
with DOA4_NP_TEST_FIELDS=83033B40:138.
Prints the frames compared and, per field, "equal on all N frames" or its first differing frame, both values and the number of
frames it differs on. Exit status: 0 = every field equal on at least one frame; 1 = a field differs; 2 = nothing to compare (no file,
no common frame, or a field the log does not hold), so an empty comparison can never pass.
"""
import argparse
import os
import struct
import sys

sys.dont_write_bytecode = True
# The per-fighter table 0x83033B40, stride 156, one record per side (sub_823CCF10: r29 = 0x83033B40 + side * 156, the HUD's source):
# +0 active in a match, +1 character, +2 model (u8 each), +4 position x y z w (floats), +0x24 life (u16, 240 full). The first 0x78
# bytes never differed between two peers in any measured pair; +0x78.. holds bone positions of the model being shown, which do.
TABLE = 0x83033B40
SIDE = 156
FIGHTER_FIELDS = [f for side in (0, 1) for f in (
    (f"side{side} position", TABLE + side * SIDE + 4, 12),
    (f"side{side} life", TABLE + side * SIDE + 0x24, 2),
    (f"side{side} state", TABLE + side * SIDE, 0x78),
)]


def read_fields(path):
    """([(address, length)], {frame: row bytes}); a short last row (a killed run) is dropped."""
    with open(path, "rb") as f:
        head = f.read(16)
        if head[:8] != b"D4NPFLD1":
            raise SystemExit(f"{path}: not a field log")
        _, n = struct.unpack_from("<II", head, 8)
        ranges = [struct.unpack("<II", f.read(8)) for _ in range(n)]
        size = sum(r[1] for r in ranges)
        rows = {}
        while True:
            h = f.read(8)
            row = f.read(size)
            if len(h) < 8 or len(row) < size:
                return ranges, rows
            rows[struct.unpack_from("<I", h)[0]] = row


def locate(ranges, address, length):
    """The field's offset in a row, or None when no logged range holds all of it."""
    at = 0
    for a, n in ranges:
        if a <= address and address + length <= a + n:
            return at + address - a
        at += n
    return None


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--field", action="append", default=[], help="NAME:ADDR:LEN, hex")
    ap.add_argument("--from", dest="start", type=int, default=0)
    args = ap.parse_args(argv)
    fields = []
    for text in args.field:
        name, addr, length = text.rsplit(":", 2)
        fields.append((name, int(addr, 16), int(length, 16)))
    fields = fields or FIGHTER_FIELDS
    logs = []
    for d in (args.a, args.b):
        path = os.path.join(d, "fields.d4f")
        if not os.path.exists(path):
            print(f"no field log: {path}")
            return 2
        logs.append(read_fields(path))
    (ra, xa), (rb, xb) = logs
    if ra != rb:
        print(f"the runs logged different ranges: {ra} / {rb}")
        return 2
    common = sorted(f for f in xa if f in xb and f >= args.start)
    print(f"fields: {len(common)} common frames" + (f" ({common[0]}..{common[-1]})" if common else "") + f"; run A {len(xa)}, run B {len(xb)}")
    if not common:
        return 2
    result = 0
    for name, address, length in fields:
        at = locate(ra, address, length)
        if at is None:
            print(f"  {name}: {address:08X}+{length:X} is not in the log")
            result = 2
            continue
        differ = [f for f in common if xa[f][at:at + length] != xb[f][at:at + length]]
        if not differ:
            print(f"  {name} {address:08X}+{length:X}: equal on all {len(common)} frames")
            continue
        f = differ[0]
        print(f"  {name} {address:08X}+{length:X}: first differs at frame {f} (A {xa[f][at:at + length].hex()}, B {xb[f][at:at + length].hex()}); "
              f"{len(differ)} frames differ")
        result = result or 1
    return result


if __name__ == "__main__":
    sys.exit(main())
