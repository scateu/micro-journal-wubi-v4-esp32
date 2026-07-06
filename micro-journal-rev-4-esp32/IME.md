# Chinese IME tables (Wubi / Pinyin / Shuangpin)

The Cardputer ADV firmware has ONE Chinese input method active, fixed at build
time (or swapped into a prebuilt firmware). No on-device switching, no runtime
table loading — the dictionary is compiled into flash and read in place.

All IME assets live in the **`IME/`** folder:

```
IME/
  gen_ime.py             generator  (wubi / pinyin / shuangpin -> IME4 table)
  inject_ime.py          swap the table into a prebuilt firmware.bin (no build)
  ime_table.bin          the embedded table (what the firmware compiles in)
  wubi.ywvim             wubi 86 source
  pinyin_simp.dict.yaml  pinyin source (rime rime-pinyin-simp)
  gen_wubi.py            legacy wubi-only generator (superseded by gen_ime.py)
  wubi86.bin             legacy wubi table (superseded by ime_table.bin)
```

Three schemes are supported, all in the same **IME4** table format:

| scheme | code | indicator | source |
|--------|------|-----------|--------|
| `wubi` | 1–4 letters (Wubi 86) | `[五]` | `IME/wubi.ywvim` |
| `pinyin` | 1–6 letters (full pinyin) | `[拼]` | `IME/pinyin_simp.dict.yaml` |
| `shuangpin` | 2 letters (小鹤 / Xiaohe) | `[双]` | derived from the pinyin source |

**Wubi returns phrases** (词组: 工期, 葡萄牙, 花花世界, …), not just single hanzi —
the IME4 format stores variable-length words in a string pool. Pinyin/Shuangpin
stay single-character (their source is one hanzi per syllable). Because the wubi
table is much bigger with phrases, its flash slot is **896 KiB** (`--slot
917504`); pinyin/shuangpin keep the **512 KiB** slot.

Everything below uses **only the Python standard library** — no PlatformIO is
needed to (re)generate a table, and `IME/inject_ime.py` can swap the table into a
released `firmware.bin` with no build environment at all. Run the commands from
the repository root.

---

## Quick start

**Change the IME by rebuilding** (needs PlatformIO):

```sh
# pick ONE scheme -> IME/ime_table.bin, then build
python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
pio run -e cardputer-adv -t upload
```

**Change the IME with no build tools** (needs only Python + a released .bin):

```sh
python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out my_table.bin
python3 IME/inject_ime.py --firmware firmware_cardputer_adv.bin --table my_table.bin
# then flash firmware_cardputer_adv.bin as usual
```

---

## 1. Generate a table — `IME/gen_ime.py`

```sh
python3 IME/gen_ime.py --scheme wubi      --src IME/wubi.ywvim            --out IME/ime_table.bin --slot 917504
python3 IME/gen_ime.py --scheme pinyin    --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
```

(Or just use the Makefile: `make wubi` / `make pinyin` / `make shuangpin` picks
the right `--slot` per scheme automatically.)

Options:

- `--scheme wubi|pinyin|shuangpin`  which IME to build (required).
- `--src PATH`   the source table for that scheme (required).
- `--out PATH`   output (default `IME/ime_table.bin`).
- `--top N`      keep only the N most common words (0 = keep all).
- `--max-phrases N`  wubi only: keep at most N multi-hanzi phrases (default
                 `30000`, 0 = keep all). Single hanzi are always kept. Phrases
                 are ranked **shortest-code-first** (fewest keystrokes, then the
                 primary line candidate, then shorter word) and truncated to N so
                 the table fits the slot. The full ywvim phrase set is ~1.4 MB;
                 30k phrases + all singles is ~856 KiB (fits the 896 KiB slot).
- `--slot N`     reserved slot size in bytes (default `524288` = 512 KiB; wubi
                 uses `917504` = 896 KiB). Must match the firmware's reserved
                 slot.

Sources:

- **Pinyin / Shuangpin**: `IME/pinyin_simp.dict.yaml` (rime rime-pinyin-simp,
  `<char>\t<syllable>\t<weight>`). Candidates are ranked by descending weight so
  common characters come first. Shuangpin maps each syllable to its 2-letter
  小鹤 (Xiaohe) code.
- **Wubi**: the ywvim `IME/wubi.ywvim` `[Main]` section — both single hanzi and
  multi-hanzi phrases. (The shipped `IME/ime_table.bin` is the Wubi table.)

### IME4 format (little-endian), consumed by `src/service/IME/IME.cpp`

Records are **fixed-width** (so the binary search + prefix index are trivial),
and the variable-length hanzi/phrase text lives in a **string pool** appended
after the records, referenced by (offset, len). This is what lets wubi carry
phrases without a per-record size change.

```
magic     : 4 bytes   "IME4"
scheme    : 1 byte    0=wubi 1=pinyin 2=shuangpin
codeLen   : 1 byte    code width (6)
reserved  : 2 bytes   0
count     : uint32    number of records
poolBytes : uint32    size of the trailing string pool
index     : 677 * uint32   first-two-letter prefix lower-bound index
records   : count * (codeLen+4) bytes, sorted by code:
              code   : codeLen bytes  ASCII a-z, NUL-padded
              poolOff: 3 bytes (uint24 LE)  byte offset into the pool
              wordLen: 1 byte               phrase length in bytes (1..255)
pool      : poolBytes bytes   concatenated UTF-8 words (de-duplicated)
padding   : 0xFF up to the fixed slot size (896 KiB wubi / 512 KiB pinyin,shuang)
```

The pool starts right after the records (`recordBase + count*recordSize`), so the
device derives its offset. The 677-entry index lets the device jump straight to
the records for a two-letter prefix; only this ~2.7 KB index is copied to RAM at
boot, the records + pool stay in flash. `scheme` drives the on-screen indicator
and the max input length (wubi 4 / pinyin 6 / shuangpin 2).

---

## 2a. Embed at build time

`platformio.ini` embeds `IME/ime_table.bin` into flash:

```ini
[env:cardputer-adv]
board_build.embed_files = IME/ime_table.bin   ; -> _binary_IME_ime_table_bin_*
build_flags = ... -D USE_IME
```

`IME.cpp` points at the flash symbol and reads the table in place. Just rebuild
after regenerating `IME/ime_table.bin`:

```sh
pio run -e cardputer-adv
```

The table lives in a fixed slot (padded with `0xFF`) — **896 KiB** for the wubi
build, **512 KiB** for pinyin/shuangpin — so it can be swapped later without
relinking (see below). The slot size is baked into the firmware image, so an
injected table must use the matching `--slot`.

## 2b. Inject into a prebuilt firmware — `IME/inject_ime.py`

For users without PlatformIO. It finds the IME slot in a released `firmware.bin`
(by locating and validating the `IME4` header) and overwrites it with your table:

```sh
# match --slot to the firmware's build: wubi=917504, pinyin/shuangpin=524288
python3 IME/inject_ime.py --firmware firmware_cardputer_adv.bin --table my_table.bin --slot 917504
```

- Writes the firmware **in place**, keeping a `.bak` backup (use `--no-backup`
  to skip).
- Rejects a table larger than the slot; pads the slot with `0xFF` so its size is
  unchanged.
- Then flash the patched `firmware_cardputer_adv.bin` normally.

---

## Notes

- **Only one scheme per firmware.** Switching = regenerate + rebuild, or inject +
  reflash. This is deliberate: no runtime table loading keeps input instant.
- Flash budget: the wubi 896 KiB slot (with phrases) puts the ADV firmware at
  ~98% of its 3 MB app slot (~60 KiB free). If a build overflows, lower
  `--max-phrases` (e.g. 25000) to shrink the wubi table. Pinyin/shuangpin at
  512 KiB are comfortable (~85%).
- Xiaohe covers 411/415 syllables; a handful of rare interjections (`hm/m/n/ng`)
  have no standard double-pinyin code and are dropped from the shuangpin table.
