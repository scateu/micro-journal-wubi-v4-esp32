#!/usr/bin/env python3
"""
Inject an IME table (Wubi / Pinyin / Shuangpin) into a PREBUILT firmware image,
WITHOUT any build toolchain - only the Python standard library.

The firmware reserves a fixed flash slot for the IME dictionary (see
IME/gen_ime.py and board_build.embed_files in platformio.ini). This tool finds
that slot inside a released firmware.bin and overwrites it with a table you
generated with gen_ime.py, so you can switch the on-device IME scheme just by
re-flashing - no PlatformIO, no compiler. All IME assets live in the IME/ folder.

IMPORTANT: the slot size must match the firmware build. The wubi build (with
phrases) reserves 896 KiB (--slot 917504); pinyin/shuangpin builds reserve
512 KiB. Pass --slot to match whatever the firmware you are patching was built
with (the tool needs a full slot's worth of room after the located magic).

Workflow (python-only user, run from the repo root):
  1. Generate a table for the scheme you want:
       python3 IME/gen_ime.py --scheme pinyin \\
           --src IME/pinyin_simp.dict.yaml --out my_table.bin
  2. Inject it into the released firmware:
       python3 IME/inject_ime.py --firmware firmware_cardputer_adv.bin \\
           --table my_table.bin
     (writes firmware_cardputer_adv.bin in place; keeps a .bak backup)
  3. Flash firmware_cardputer_adv.bin to the device as usual.

How the slot is located
-----------------------
The slot starts with the ASCII magic "IME4". The tool scans the firmware for
every "IME4" occurrence and VALIDATES the IME4 header that follows (scheme 0-2,
codeLen 1-6, a record count whose table fits in the slot) - it does NOT rely on
alignment, because the embedded slot is not necessarily aligned to a file
offset. The first candidate that both validates AND is followed by SLOT_SIZE
bytes of room is the slot. The new table must be <= SLOT_SIZE; it is written
padded exactly like gen_ime.py produces (0xFF fill), so the slot never resizes.
"""
import argparse
import os
import shutil
import struct
import sys

MAGIC = b"IME4"
HEADER_SIZE = 16
INDEX_ENTRIES = 26 * 26 + 1
SLOT_SIZE_DEFAULT = 512 * 1024


def parse_ime4_header(buf, off):
    """Return (scheme, codeLen, count, real_bytes) if a valid IME4 header sits at
    `off`, else None. real_bytes is the size of the actual (unpadded) table
    (header + prefix index + fixed-width records + the trailing string pool)."""
    if off + HEADER_SIZE > len(buf):
        return None
    if buf[off:off + 4] != MAGIC:
        return None
    scheme = buf[off + 4]
    code_len = buf[off + 5]
    count = struct.unpack_from("<I", buf, off + 8)[0]
    pool_bytes = struct.unpack_from("<I", buf, off + 12)[0]
    if scheme > 2 or code_len < 1 or code_len > 6 or count == 0:
        return None
    record_size = code_len + 4  # poolOff[3] + wordLen[1]
    real = HEADER_SIZE + INDEX_ENTRIES * 4 + count * record_size + pool_bytes
    # sanity: a real table is at most a couple MB
    if real > 4 * 1024 * 1024:
        return None
    return scheme, code_len, count, real


def find_slot(fw, slot_size):
    """Find the offset of the embedded IME4 slot in the firmware bytes.
    Returns (offset, header) or (None, None)."""
    # The embedded slot is NOT necessarily aligned to a file offset (the flash
    # section's file position depends on preceding sections), so we locate it by
    # VALIDATING the IME4 header rather than by alignment. A stray "IME4" byte
    # sequence elsewhere won't have a valid header (scheme 0-2, codeLen 1-6, a
    # plausible record count), so it's rejected.
    start = 0
    while True:
        off = fw.find(MAGIC, start)
        if off < 0:
            return None, None
        start = off + 4
        hdr = parse_ime4_header(fw, off)
        if hdr is None:
            continue
        # must have a full slot's worth of room after it
        if off + slot_size > len(fw):
            continue
        return off, hdr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", required=True, help="prebuilt firmware .bin (edited in place)")
    ap.add_argument("--table", required=True, help="IME4 table from gen_ime.py")
    ap.add_argument("--slot", type=int, default=SLOT_SIZE_DEFAULT,
                    help="reserved slot size in bytes (must match the build: "
                         "wubi=917504 (896 KiB), pinyin/shuangpin=524288 (512 KiB))")
    ap.add_argument("--no-backup", action="store_true", help="do not write a .bak")
    args = ap.parse_args()

    for p in (args.firmware, args.table):
        if not os.path.exists(p):
            sys.exit(f"not found: {p}")

    table = open(args.table, "rb").read()
    thdr = parse_ime4_header(table, 0)
    if thdr is None:
        sys.exit("--table is not a valid IME4 table (regenerate with gen_ime.py)")
    tscheme, tcode, tcount, treal = thdr
    if len(table) > args.slot:
        sys.exit(f"table {len(table)} bytes exceeds slot {args.slot}")

    fw = bytearray(open(args.firmware, "rb").read())
    off, fhdr = find_slot(fw, args.slot)
    if off is None:
        sys.exit("could not locate the IME4 slot in the firmware "
                 "(is this a micro-journal build with USE_IME + embedded ime_table.bin?)")

    fscheme, fcode, fcount, freal = fhdr
    names = {0: "wubi", 1: "pinyin", 2: "shuangpin"}
    print(f"found IME slot at 0x{off:X}")
    print(f"  current: {names.get(fscheme, '?')} scheme, {fcount} records")
    print(f"  new    : {names.get(tscheme, '?')} scheme, {tcount} records "
          f"({treal} bytes real, {len(table)} in slot)")

    if not args.no_backup:
        bak = args.firmware + ".bak"
        shutil.copy(args.firmware, bak)
        print(f"backup   : {bak}")

    # Overwrite the whole slot. Pad the new table to the slot size with 0xFF
    # (flash-erase value) so the slot's length is unchanged.
    payload = bytearray(table)
    if len(payload) < args.slot:
        payload += bytes([0xFF]) * (args.slot - len(payload))
    fw[off:off + args.slot] = payload

    with open(args.firmware, "wb") as f:
        f.write(fw)
    print(f"injected : {args.firmware}  (flash it to switch IME to {names.get(tscheme, '?')})")


if __name__ == "__main__":
    sys.exit(main())
