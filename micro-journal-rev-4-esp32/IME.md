# Chinese IME tables (Wubi / Pinyin / Shuangpin)

The Cardputer ADV firmware has ONE Chinese input method active, fixed at build
time (or swapped into a prebuilt firmware). No on-device switching, no runtime
table loading — the dictionary is compiled into flash and read in place.

All IME assets live in the **`IME/`** folder:

```
IME/
  gen_ime.py             generator  (wubi / pinyin / shuangpin -> IME3 table)
  inject_ime.py          swap the table into a prebuilt firmware.bin (no build)
  ime_table.bin          the embedded table (what the firmware compiles in)
  wubi.ywvim             wubi 86 source
  pinyin_simp.dict.yaml  pinyin source (rime rime-pinyin-simp)
  gen_wubi.py            legacy wubi-only generator (superseded by gen_ime.py)
  wubi86.bin             legacy wubi table (superseded by ime_table.bin)
```

Three schemes are supported, all in the same **IME3** table format:

| scheme | code | indicator | source |
|--------|------|-----------|--------|
| `wubi` | 1–4 letters (Wubi 86) | `[五]` | `IME/wubi.ywvim` |
| `pinyin` | 1–6 letters (full pinyin) | `[拼]` | `IME/pinyin_simp.dict.yaml` |
| `shuangpin` | 2 letters (小鹤 / Xiaohe) | `[双]` | derived from the pinyin source |

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
python3 IME/gen_ime.py --scheme wubi      --src IME/wubi.ywvim            --out IME/ime_table.bin
python3 IME/gen_ime.py --scheme pinyin    --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table.bin
```

Options:

- `--scheme wubi|pinyin|shuangpin`  which IME to build (required).
- `--src PATH`   the source table for that scheme (required).
- `--out PATH`   output (default `IME/ime_table.bin`).
- `--top N`      keep only the N most common hanzi (0 = keep all). Use if a table
                 ever exceeds the slot.
- `--slot N`     reserved slot size in bytes (default `524288` = 512 KiB). Must
                 match the firmware's reserved slot.

Sources:

- **Pinyin / Shuangpin**: `IME/pinyin_simp.dict.yaml` (rime rime-pinyin-simp,
  `<char>\t<syllable>\t<weight>`). Candidates are ranked by descending weight so
  common characters come first. Shuangpin maps each syllable to its 2-letter
  小鹤 (Xiaohe) code.
- **Wubi**: the ywvim `IME/wubi.ywvim` `[Main]` section. (The shipped
  `IME/ime_table.bin` is the Wubi table.)

### IME3 format (little-endian), consumed by `src/service/IME/IME.cpp`

```
magic     : 4 bytes   "IME3"
scheme    : 1 byte    0=wubi 1=pinyin 2=shuangpin
codeLen   : 1 byte    code width (6)
reserved  : 2 bytes   0
count     : uint32    number of records
index     : 677 * uint32   first-two-letter prefix lower-bound index
records   : count * (codeLen+4) bytes, sorted by code:
              code : codeLen bytes  ASCII a-z, NUL-padded
              hanzi: 3 bytes         UTF-8
              flag : 1 byte          reserved
padding   : 0xFF up to the fixed slot size (512 KiB)
```

The 677-entry index lets the device jump straight to the records for a
two-letter prefix; only this ~2.7 KB index is copied to RAM at boot, the records
stay in flash. `scheme` drives the on-screen indicator and the max input length
(wubi 4 / pinyin 6 / shuangpin 2).

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

The table lives in a fixed **512 KiB** slot (padded with `0xFF`), so it can be
swapped later without relinking (see below).

## 2b. Inject into a prebuilt firmware — `IME/inject_ime.py`

For users without PlatformIO. It finds the 512 KiB IME slot in a released
`firmware.bin` (by locating and validating the `IME3` header) and overwrites it
with your table:

```sh
python3 IME/inject_ime.py --firmware firmware_cardputer_adv.bin --table my_table.bin
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
- Flash budget: the 512 KiB slot puts the ADV firmware around ~85% of its 3 MB
  app slot. If you add a much larger table, use `--top N` or a bigger `--slot`
  (and match it in `platformio.ini` + rebuild).
- Xiaohe covers 411/415 syllables; a handful of rare interjections (`hm/m/n/ng`)
  have no standard double-pinyin code and are dropped from the shuangpin table.
