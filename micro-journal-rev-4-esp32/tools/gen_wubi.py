#!/usr/bin/env python3
"""
Generate the on-device Wubi 86 dictionary (data/wubi86.bin) for the micro-journal
Chinese IME.

Source table: rime-wubi `wubi86.dict.yaml`
  https://github.com/rime/rime-wubi  (lines: <hanzi>\\t<code>\\t<weight>[\\t<stem>])

The full table has ~6.5k single hanzi. We keep the most frequent N (default 3500,
the "common" set) by weight, but retain every code that maps to a kept hanzi so
short codes still work.

Binary format (little-endian), consumed by src/service/IME/IME.cpp:

  magic   : 4 bytes  "WUB1"
  count   : uint32   number of records
  records : count * 8 bytes, sorted ascending by code:
              code  : 4 bytes  ASCII a-z, NUL-padded (never NUL-prefixed)
              hanzi : 3 bytes  UTF-8 (BMP CJK is always 3 bytes)
              flag  : 1 byte   reserved (0)

Records are sorted by (code, descending weight) so the device can binary-search a
code prefix and read candidates already ordered best-first.

Usage:
  python3 tools/gen_wubi.py [--src wubi86.dict.yaml] [--top 3500] [--out data/wubi86.bin]
  (downloads the source automatically if --src is omitted and not cached)
"""
import argparse
import os
import struct
import sys
import urllib.request

SRC_URL = "https://raw.githubusercontent.com/rime/rime-wubi/master/wubi86.dict.yaml"
MAGIC = b"WUB1"


def load_rows(path):
    rows = []
    started = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not started:
                # YAML front matter ends with a line that is just "..."
                if line.strip() == "...":
                    started = True
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            text, code = parts[0], parts[1]
            weight = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 0
            rows.append((text, code, weight))
    return rows


def is_single_hanzi(s):
    return len(s) == 1 and 0x4E00 <= ord(s) <= 0x9FFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=None, help="path to wubi86.dict.yaml")
    ap.add_argument("--top", type=int, default=3500,
                    help="keep the N most frequent hanzi (default 3500)")
    ap.add_argument("--out", default=os.path.join("data", "wubi86.bin"))
    ap.add_argument("--cache", default=os.path.join("tools", "wubi86.dict.yaml"))
    args = ap.parse_args()

    src = args.src
    if src is None:
        src = args.cache
        if not os.path.exists(src):
            print(f"downloading {SRC_URL}")
            os.makedirs(os.path.dirname(src), exist_ok=True)
            urllib.request.urlretrieve(SRC_URL, src)

    rows = load_rows(src)

    # single hanzi, alphabetic a-z codes only
    single = [(t, c.lower(), w) for (t, c, w) in rows
              if is_single_hanzi(t) and c.isascii() and c.isalpha() and len(c) <= 4]

    # rank hanzi by their best (max) weight, keep the top N
    best = {}
    for t, c, w in single:
        if t not in best or w > best[t]:
            best[t] = w
    kept = set(sorted(best, key=lambda h: best[h], reverse=True)[:args.top])

    entries = [(c, t, w) for (t, c, w) in single if t in kept]

    # sort by code asc, then weight desc -> candidates come out best-first
    entries.sort(key=lambda e: (e[0], -e[2]))

    out = bytearray()
    out += MAGIC
    out += struct.pack("<I", len(entries))
    for code, hanzi, _w in entries:
        cb = code.encode("ascii")
        if len(cb) > 4:
            continue
        hb = hanzi.encode("utf-8")
        if len(hb) != 3:
            # skip anything outside the BMP 3-byte range to keep records fixed-width
            continue
        out += cb + b"\x00" * (4 - len(cb))  # code[4]
        out += hb                              # hanzi[3]
        out += b"\x00"                         # flag

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    print(f"hanzi kept   : {len(kept)}")
    print(f"records      : {len(entries)}")
    print(f"output       : {args.out}  ({len(out)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
