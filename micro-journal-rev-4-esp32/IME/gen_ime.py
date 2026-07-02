#!/usr/bin/env python3
"""
Generate an on-device IME table (Wubi / Pinyin / Shuangpin) for the micro-journal
Chinese IME, in the unified "IME3" format, padded into a fixed-size flash slot.

Only the Python standard library is used - no PlatformIO / build toolchain needed
to (re)generate a table. Pair with IME/inject_ime.py to swap the table into a
prebuilt firmware.bin without rebuilding. All IME assets live in the IME/ folder.

Schemes
-------
  wubi       Wubi 86. Source: ywvim `IME/wubi.ywvim` [Main] section (space-sep
             `<code> <cand1> ...`, frequency order). Codes 1-4 letters.
  pinyin     Full Hanyu Pinyin. Source: rime `IME/pinyin_simp.dict.yaml`
             (`<char>\\t<syllable>\\t<weight>`). Syllables 1-6 letters, ranked
             by descending weight so common chars come first.
  shuangpin  Xiaohe (小鹤) double-pinyin. Derived from the same pinyin source:
             each full syllable is mapped to its 2-letter Xiaohe code, keeping
             the char + weight ranking. Codes are always 2 letters.

IME3 binary format (little-endian), consumed by src/service/IME/IME.cpp
------------------------------------------------------------------------
  magic     : 4 bytes   "IME3"
  scheme    : 1 byte    0=wubi 1=pinyin 2=shuangpin  (drives the [五]/[拼]/[双]
                        indicator and the max input length on-device)
  codeLen   : 1 byte    fixed code width in bytes (6 for all schemes here)
  reserved  : 2 bytes   0
  count     : uint32    number of records
  index     : 677 * uint32   first-two-letter prefix lower-bound index
  records   : count * (codeLen + 4) bytes, sorted ascending by code:
                code  : codeLen bytes  ASCII a-z, NUL-padded
                hanzi : 3 bytes        UTF-8 (BMP CJK is always 3 bytes)
                flag  : 1 byte         reserved (0)

Fixed slot
----------
The whole IME3 blob is padded to a fixed SLOT_SIZE (default 512 KiB) so a table
can be hot-swapped into a prebuilt firmware.bin (the reserved region never moves
or changes size). The padding after the real blob is 0xFF (flash-erase value).
The firmware ignores the padding (it reads `count` from the header).

Usage (run from the repo root)
------------------------------
  python3 IME/gen_ime.py --scheme wubi      --src IME/wubi.ywvim            --out IME/ime_table.bin
  python3 IME/gen_ime.py --scheme pinyin    --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
  python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin

  --top N   keep only the N most common hanzi (0 = keep all).
  --slot N  reserved slot size in bytes (default 524288 = 512 KiB).
"""
import argparse
import os
import struct
import sys

MAGIC = b"IME3"
INDEX_ENTRIES = 26 * 26 + 1  # 677: one lower-bound per two-letter prefix + sentinel
CODE_LEN = 6                  # fixed code width (fits pinyin zhuang/chuang/shuang)
HEADER_SIZE = 12             # magic[4] + scheme[1] + codeLen[1] + reserved[2] + count[4]
SLOT_SIZE_DEFAULT = 512 * 1024
PAD_BYTE = 0xFF

SCHEME_WUBI = 0
SCHEME_PINYIN = 1
SCHEME_SHUANGPIN = 2
SCHEME_IDS = {"wubi": SCHEME_WUBI, "pinyin": SCHEME_PINYIN, "shuangpin": SCHEME_SHUANGPIN}


# ---------------------------------------------------------------------------
# Xiaohe (小鹤) double-pinyin mapping
# ---------------------------------------------------------------------------
# Initials: zh/ch/sh -> v/i/u; all single-letter initials map to themselves.
XIAOHE_INITIAL = {
    "zh": "v", "ch": "i", "sh": "u",
    "b": "b", "p": "p", "m": "m", "f": "f", "d": "d", "t": "t", "n": "n",
    "l": "l", "g": "g", "k": "k", "h": "h", "j": "j", "q": "q", "x": "x",
    "r": "r", "z": "z", "c": "c", "s": "s", "y": "y", "w": "w",
}
# Finals -> key (canonical Xiaohe layout). Single-vowel finals map to their own
# letter (a/o/e/i/u); "v" is for ü.
XIAOHE_FINAL = {
    "a": "a", "o": "o", "e": "e", "i": "i", "u": "u", "v": "v",
    "ai": "d", "an": "j", "ang": "h", "ao": "c",
    "ei": "w", "en": "f", "eng": "g", "er": "r",
    "ia": "x", "ian": "m", "iang": "l", "iao": "n", "ie": "p",
    "in": "b", "ing": "k", "iong": "s", "iu": "q",
    "ong": "s", "ou": "z",
    "ua": "x", "uai": "k", "uan": "r", "uang": "l", "ue": "t",
    "ui": "v", "un": "y", "uo": "o",
    "ve": "t", "ng": "g",
}
# Zero-initial syllables (start with a vowel): first letter is the initial key,
# then the final key. Full standard set for the 2-letter code.
XIAOHE_ZERO = {
    "a": "aa", "o": "oo", "e": "ee",
    "ai": "ai", "an": "an", "ang": "ah", "ao": "ao",
    "ei": "ei", "en": "en", "eng": "eg", "er": "er",
    "ou": "ou",
}


def split_pinyin(syl):
    """Split a full pinyin syllable into (initial, final). initial may be ''."""
    for ini in ("zh", "ch", "sh"):
        if syl.startswith(ini):
            return ini, syl[len(ini):]
    if syl and syl[0] in "bpmfdtnlgkhjqxrzcsyw":
        return syl[0], syl[1:]
    return "", syl  # zero-initial (starts with a vowel)


def to_xiaohe(syl):
    """Map a full pinyin syllable to its 2-letter Xiaohe code, or None if the
    syllable is outside the standard scheme (rare interjections m/n/ng/hm...)."""
    if syl in XIAOHE_ZERO:
        return XIAOHE_ZERO[syl]

    ini, fin = split_pinyin(syl)
    if ini == "":
        return None  # a vowel-initial syllable not in the zero table

    ikey = XIAOHE_INITIAL.get(ini)
    if ikey is None:
        return None

    if len(fin) == 1 and fin in "aoeiuv":
        return ikey + fin
    fkey = XIAOHE_FINAL.get(fin)
    if fkey is None:
        return None
    return ikey + fkey


# ---------------------------------------------------------------------------
# prefix index
# ---------------------------------------------------------------------------
def build_prefix_index(records, count):
    """index[k] = first record index whose code sorts >= the two-letter prefix
    k encodes (k=(c0-'a')*26+(c1-'a')). Monotonic; index[676]==count."""
    index = [count] * INDEX_ENTRIES
    for i, (code, _char) in enumerate(records):
        if len(code) < 2:
            k = (ord(code[0]) - 97) * 26
        else:
            k = (ord(code[0]) - 97) * 26 + (ord(code[1]) - 97)
        if index[k] == count:
            index[k] = i
    nxt = count
    for k in range(INDEX_ENTRIES - 1, -1, -1):
        if index[k] == count:
            index[k] = nxt
        else:
            nxt = index[k]
    return index


def is_single_hanzi(s):
    return len(s) == 1 and 0x4E00 <= ord(s) <= 0x9FFF


# ---------------------------------------------------------------------------
# sources
# ---------------------------------------------------------------------------
def load_wubi(path):
    """Yield (code, char, score) from the ywvim [Main] section. Lower score =
    more common (shortest code, then earliest candidate position)."""
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
                    yield code, cand, (len(code), rank)
                rank += 1


def load_pinyin(path, shuangpin=False):
    """Yield (code, char, score) from rime pinyin_simp.dict.yaml. Higher weight =
    more common, so score = -weight (lower score = more common)."""
    started = False
    dropped = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line == "...":
                started = True
                continue
            if not started or not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            char, syl = parts[0], parts[1]
            weight = int(parts[2]) if len(parts) >= 3 and parts[2].isdigit() else 0
            # single hanzi + single syllable only
            if len(char) != 1 or " " in syl or not is_single_hanzi(char):
                continue
            if not (syl.isascii() and syl.isalpha()):
                continue
            code = syl
            if shuangpin:
                code = to_xiaohe(syl)
                if code is None:
                    dropped += 1
                    continue
            yield code, char, -weight
    if dropped:
        print(f"warning: {dropped} syllables had no Xiaohe mapping (skipped)",
              file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scheme", required=True, choices=list(SCHEME_IDS))
    ap.add_argument("--src", required=True, help="source table for the scheme")
    ap.add_argument("--out", default=os.path.join("IME", "ime_table.bin"))
    ap.add_argument("--top", type=int, default=0,
                    help="keep only the N most common hanzi (0 = keep all)")
    ap.add_argument("--slot", type=int, default=SLOT_SIZE_DEFAULT,
                    help="reserved flash slot size in bytes (default 512 KiB)")
    args = ap.parse_args()

    if not os.path.exists(args.src):
        sys.exit(f"source table not found: {args.src}")

    scheme = SCHEME_IDS[args.scheme]

    if args.scheme == "wubi":
        gen = load_wubi(args.src)
    else:
        gen = load_pinyin(args.src, shuangpin=(args.scheme == "shuangpin"))

    # collect, de-duplicating (code, char) keeping the best (lowest) score
    best = {}
    for code, char, score in gen:
        if len(code) > CODE_LEN:
            continue
        key = (code, char)
        if key not in best or score < best[key]:
            best[key] = score

    records = [(code, char, score) for (code, char), score in best.items()]

    # optional frequency filter: rank each char by its best score, keep top N
    if args.top > 0:
        char_best = {}
        for code, char, score in records:
            if char not in char_best or score < char_best[char]:
                char_best[char] = score
        kept = set(sorted(char_best, key=lambda c: char_best[c])[:args.top])
        records = [r for r in records if r[1] in kept]

    # sort by code asc; within a code, by score asc (most common candidate first)
    records.sort(key=lambda r: (r[0], r[2]))
    recs = [(code, char) for code, char, _ in records]

    index = build_prefix_index(recs, len(recs))
    record_size = CODE_LEN + 4

    out = bytearray()
    out += MAGIC
    out += struct.pack("<BBH", scheme, CODE_LEN, 0)
    out += struct.pack("<I", len(recs))
    out += struct.pack("<%dI" % INDEX_ENTRIES, *index)
    for code, char in recs:
        cb = code.encode("ascii")
        hb = char.encode("utf-8")
        if len(cb) > CODE_LEN or len(hb) != 3:
            continue
        out += cb + b"\x00" * (CODE_LEN - len(cb))  # code[CODE_LEN]
        out += hb                                    # hanzi[3]
        out += b"\x00"                               # flag

    real = len(out)
    if real > args.slot:
        sys.exit(f"table {real} bytes exceeds slot {args.slot}; use --top to shrink")

    # pad to the fixed slot with the flash-erase value
    out += bytes([PAD_BYTE]) * (args.slot - real)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    print(f"scheme       : {args.scheme} ({scheme})")
    print(f"unique hanzi : {len(set(r[1] for r in recs))}")
    print(f"records      : {len(recs)}  (record size {record_size} B)")
    print(f"real bytes   : {real}  ({real/1024:.1f} KiB)")
    print(f"slot bytes   : {args.slot}  ({args.slot/1024:.0f} KiB, {real*100//args.slot}% used)")
    print(f"output       : {args.out}")


if __name__ == "__main__":
    sys.exit(main())
