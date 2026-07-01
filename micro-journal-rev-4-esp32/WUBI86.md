# Wubi 86 dictionary (`wubi86.bin`)

How to (re)generate the on-device Wubi dictionary and embed it into the
`cardputer-adv` firmware.

The dictionary is a small binary table (`data/wubi86.bin`) that the Chinese IME
(`src/service/IME/IME.cpp`) binary-searches to turn a typed Wubi code into
candidate hanzi. On the `cardputer-adv` build it is **compiled into the firmware
flash** and read in place (memory-mapped) — it is *not* loaded from the SD card.

---

## 1. Generate `data/wubi86.bin`

The generator parses the **ywvim** Wubi 86 table (a complete full-code
single-character table) and keeps only the single-hanzi candidates.

```sh
# Get the source table (once):
curl -L -o /tmp/wubi.ywvim \
  https://raw.githubusercontent.com/scateu/ywvim/master/plugin/wubi.ywvim

# Generate the binary (default: keep every hanzi, ~33k records, ~261 KB):
python3 tools/gen_wubi.py --src /tmp/wubi.ywvim --out data/wubi86.bin
```

Options:

- `--src PATH`   path to the ywvim `wubi.ywvim` table (default `/tmp/wubi.ywvim`)
- `--out PATH`   output file (default `data/wubi86.bin`)
- `--top N`      keep only the N most common hanzi (ranked by shortest code,
                 then earliest candidate position). `0` = keep all (default).

The tool prints the record count and output size on success.

### File format (`WUB2`, little-endian)

Consumed by `src/service/IME/IME.cpp`:

```
magic   : 4 bytes   "WUB2"
count   : uint32     number of records
index   : 677 * uint32   first-two-letter prefix index (lower-bound table)
records : count * 8 bytes, sorted ascending by code:
            code  : 4 bytes  ASCII a-z, NUL-padded
            hanzi : 3 bytes  UTF-8 (BMP CJK is always 3 bytes)
            flag  : 1 byte   reserved (0)
```

The 677-entry (`26*26 + 1`) prefix index lets the device jump straight to the
records for a two-letter prefix instead of binary-searching all ~33k records.
Only this index (~2.7 KB) is copied to RAM at startup; the records are read
in place from flash.

The older `WUB1` format (no index) is still accepted by the firmware as a
fallback.

---

## 2. Embed it into the firmware

The `cardputer-adv` environment compiles `data/wubi86.bin` straight into flash.
This is already configured in `platformio.ini`:

```ini
[env:cardputer-adv]
; compile the dictionary into flash .rodata
board_build.embed_files = data/wubi86.bin

build_flags =
  ...
  -D WUBI_EMBEDDED
```

`board_build.embed_files` runs `objcopy` on the `.bin`, producing the linker
symbols (the name is derived from the **path**, so `data/wubi86.bin` becomes):

```
_binary_data_wubi86_bin_start
_binary_data_wubi86_bin_end
```

`IME.cpp` declares these `extern` under `#ifdef WUBI_EMBEDDED`, points `_blob`
at `_binary_data_wubi86_bin_start`, parses the header/index, and reads records
by offset with `memcpy` — no SD access, no file handle, no per-lookup seeks.

### Build

```sh
pio run -e cardputer-adv
```

There is **no separate step to copy the dictionary to the SD card** — rebuilding
the firmware picks up whatever is currently in `data/wubi86.bin`.

### Cost

- RAM: only the ~2.7 KB prefix index (records stay in flash).
- Flash: the dictionary itself (~261 KB) plus code; the 8 MB ADV layout has
  3 MB app slots with plenty of headroom.

---

## 3. Regenerate + reflash workflow

```sh
# 1. edit / re-download the source table if needed, then:
python3 tools/gen_wubi.py --src /tmp/wubi.ywvim --out data/wubi86.bin

# 2. rebuild the firmware (the .bin is embedded automatically):
pio run -e cardputer-adv

# 3. flash it (adjust the port as needed):
pio run -e cardputer-adv -t upload
```

---

## Notes

- **Non-`cardputer-adv` builds** (that still define `USE_IME` without
  `WUBI_EMBEDDED`) fall back to streaming `/wubi86.bin` from the SD card. For
  those, copy `data/wubi86.bin` to the SD card root instead of embedding.
- The source table is **ywvim**, not rime — rime's `wubi86.dict.yaml` is missing
  standard full codes for many base characters (e.g. `ssss`→木, `yygy`→文), which
  ywvim provides.
