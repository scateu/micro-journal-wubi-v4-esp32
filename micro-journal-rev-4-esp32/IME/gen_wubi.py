#!/usr/bin/env python3
"""
Generate the on-device Wubi 86 dictionary (data/wubi86.bin) for the micro-journal
Chinese IME.

Source table: ywvim `wubi.ywvim` (五笔86), a complete full-code single-char +
phrase table. Only the single-character candidates are used (the editor renders
one hanzi per commit).

  https://github.com/scateu/ywvim  (plugin/wubi.ywvim)

File format (relevant part):

    [Main]
    <code> <cand1> <cand2> ...     # space separated, frequency order

where <code> is 1-4 letters and each candidate may be a single hanzi or a phrase.
This table DOES carry the standard full codes for base characters that the older
rime table lacked, e.g. `ssss 木`, `yygy 文`.

Binary format (little-endian), consumed by src/service/IME/IME.cpp:

  magic   : 4 bytes  "WUB2"
  count   : uint32   number of records
  index   : 677 * uint32   first-two-letter prefix index (see below)
  records : count * 8 bytes, sorted ascending by code:
              code  : 4 bytes  ASCII a-z, NUL-padded (never NUL-prefixed)
              hanzi : 3 bytes  UTF-8 (BMP CJK is always 3 bytes)
              flag  : 1 byte   reserved (0)

Within one code the records keep the source table's candidate order (best first),
so the device can binary-search a code prefix and read candidates already ranked.

Prefix index (the speed-up): a flat lower-bound table over the first TWO code
letters. Entry k (k = (c0-'a')*26 + (c1-'a')) holds the record index of the first
record whose code is >= that two-letter prefix; entry 676 is a sentinel == count.
It is monotonic non-decreasing, so for any bucket k the records for that prefix are
[index[k], index[k+1]). This lets the device jump straight to a ~hundreds-record
window and binary-search inside it, instead of seeking through all ~33k records.
A one-letter query `a` uses [index[(a-'a')*26], index[(a-'a'+1)*26]).

The older "WUB1" format had no index (magic + count + records); the device still
reads it by falling back to a full-range search.

Usage:
  python3 tools/gen_wubi.py [--src /tmp/wubi.ywvim] [--top N] [--out data/wubi86.bin]

--top keeps only the N most common hanzi (ranked by shortest code, then earliest
candidate position). Default: keep every single hanzi (~21k chars, ~265 KB) so no
character is ever missing.
"""
import argparse
import os
import struct
import sys

MAGIC = b"WUB2"
INDEX_ENTRIES = 26 * 26 + 1  # 677: one lower-bound per two-letter prefix + sentinel


def build_prefix_index(records, count):
    """Return a list of INDEX_ENTRIES record indices (lower bounds).

    index[k] = first record index whose code sorts >= the two-letter prefix that k
    encodes, where k = (c0-'a')*26 + (c1-'a'). Monotonic; index[676] == count.
    Records are already sorted ascending by code.
    """
    index = [count] * INDEX_ENTRIES
    # Walk buckets in reverse and fill each with the start of the next-or-equal
    # bucket, so gaps (prefixes with no records) inherit the following start and
    # yield an empty [start, start) window.
    # First, mark the true start of every occupied bucket.
    for i, (code, _char, _rank) in enumerate(records):
        if len(code) < 2:
            # one-letter code: it belongs to bucket (c0, 'a') as the earliest.
            k = (ord(code[0]) - ord("a")) * 26
        else:
            k = (ord(code[0]) - ord("a")) * 26 + (ord(code[1]) - ord("a"))
        # records are ascending, so the first record that lands in bucket k is
        # its true start; keep only that first hit.
        if index[k] == count:
            index[k] = i
    # Propagate right-to-left so empty buckets point at the next real start,
    # making the whole array a monotonic lower-bound table.
    nxt = count
    for k in range(INDEX_ENTRIES - 1, -1, -1):
        if index[k] == count:
            index[k] = nxt
        else:
            nxt = index[k]
    return index


def is_single_hanzi(s):
    return len(s) == 1 and 0x4E00 <= ord(s) <= 0x9FFF


def load_main(path):
    """Yield (code, char, rank) for every single-hanzi candidate in [Main].

    rank is the candidate's 0-based position under its code (frequency order).
    """
    in_main = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("["):
                in_main = line.strip() == "[Main]"
                continue
            if not in_main or not line or line.startswith("#"):
                continue
            parts = line.split(" ")
            code = parts[0].lower()
            if not (code.isascii() and code.isalpha() and 1 <= len(code) <= 4):
                continue
            rank = 0
            for cand in parts[1:]:
                if not cand:
                    continue
                if is_single_hanzi(cand):
                    yield code, cand, rank
                rank += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="/tmp/wubi.ywvim",
                    help="path to the ywvim wubi86 table")
    ap.add_argument("--top", type=int, default=0,
                    help="keep only the N most common hanzi (0 = keep all)")
    ap.add_argument("--out", default=os.path.join("data", "wubi86.bin"))
    args = ap.parse_args()

    if not os.path.exists(args.src):
        sys.exit(f"source table not found: {args.src}")

    # collect records, de-duplicating (code, char)
    seen = set()
    records = []  # (code, char, rank)
    for code, char, rank in load_main(args.src):
        key = (code, char)
        if key in seen:
            continue
        seen.add(key)
        records.append((code, char, rank))

    # optional frequency filter: rank each char by its best (shortest code,
    # then earliest candidate position) and keep the top N.
    if args.top > 0:
        best = {}
        for code, char, rank in records:
            score = (len(code), rank)  # smaller = more common
            if char not in best or score < best[char]:
                best[char] = score
        kept = set(sorted(best, key=lambda c: best[c])[:args.top])
        records = [r for r in records if r[1] in kept]

    # sort by code asc; keep candidate order (rank) within a code
    records.sort(key=lambda r: (r[0], r[2]))

    index = build_prefix_index(records, len(records))

    out = bytearray()
    out += MAGIC
    out += struct.pack("<I", len(records))
    out += struct.pack("<%dI" % INDEX_ENTRIES, *index)
    for code, char, _rank in records:
        cb = code.encode("ascii")
        hb = char.encode("utf-8")
        if len(cb) > 4 or len(hb) != 3:
            continue
        out += cb + b"\x00" * (4 - len(cb))  # code[4]
        out += hb                              # hanzi[3]
        out += b"\x00"                         # flag

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    print(f"unique hanzi : {len(set(r[1] for r in records))}")
    print(f"records      : {len(records)}")
    print(f"index entries: {INDEX_ENTRIES} ({INDEX_ENTRIES * 4} bytes)")
    print(f"output       : {args.out}  ({len(out)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
