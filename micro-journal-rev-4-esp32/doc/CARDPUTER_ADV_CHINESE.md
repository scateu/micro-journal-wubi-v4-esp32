# Chinese (Wubi 86) input on the M5Cardputer ADV

The `cardputer-adv` firmware adds a **Wubi 86 (五笔86) Chinese input method** and
**Simplified-Chinese display** to the micro-journal typewriter, on top of the
existing Cardputer build.

The same firmware also runs on the original Cardputer (M5Unified auto-detects the
board at runtime), but the Chinese feature is only built into the `cardputer-adv`
environment because the bundled font needs the ADV's larger (8 MB) flash.

## What you get

- Type Wubi 86 codes (`a`–`z`); candidate hanzi appear in a bar over the edit line.
- ~3,500 common hanzi (GB2312 level-1 frequency set).
- Simplified-Chinese glyphs render in the editor using M5GFX's built-in `efontCN`
  font — no external font files needed.
- Plain ASCII typing is completely unaffected when the IME is off.

## Keys

Chinese (Wubi) input is **enabled by default** at start-up whenever the dictionary
is present on the SD card. The IME applies to **both the built-in keyboard and an
external USB keyboard** — they share one input pipeline.

| Key | Action |
|-----|--------|
| **Fn + Space** (built-in) | Toggle Chinese (Wubi) input on / off (starts on) |
| **Ctrl + Space** (USB keyboard) | Toggle Chinese (Wubi) input on / off |
| `a`–`z` | Append to the Wubi code (max 4 letters) |
| `1`–`9` | Pick that numbered candidate |
| **Space** / **Enter** | Commit the first candidate |
| **Backspace** | Delete the last code letter (while composing) |
| `,` / `;` | Previous candidate page |
| `.` / `'` | Next candidate page |
| **Esc** (Fn + \`) | Cancel the current composition |

When no composition is in progress, all of these keys behave normally in the editor.

## Architecture note

The IME is applied at a single shared chokepoint — `display_keyboard()` in
`src/display/display.cpp` calls `keyboard_ime_filter()` for every ASCII key,
regardless of which keyboard produced it. This is why both the built-in matrix
keyboard and the external USB-host keyboard get Wubi input without each driver
needing its own IME code.

## Build & flash

```sh
# 1. Generate the dictionary (only needed once, or after changing --top)
python3 tools/gen_wubi.py            # writes data/wubi86.bin (~34 KB)

# 2. Build + flash the firmware
pio run -e cardputer-adv -t upload

# 3. Put the dictionary on the SD card
#    The Cardputer reads journals (and the dictionary) from the SD card.
#    Copy data/wubi86.bin to the root of the SD card as /wubi86.bin
cp data/wubi86.bin /Volumes/<SD_CARD>/wubi86.bin
```

If `/wubi86.bin` is missing from the SD card, the firmware still boots and works in
English; Chinese input stays off and **Fn + Space** will not enable it (the IME
reports the dictionary as unavailable).

## How it fits together

- **Storage** — committed hanzi are written into the editor's text buffer as their
  raw UTF-8 bytes (3 per hanzi). Journal files therefore stay valid UTF-8 text.
- **Editing** — backspace, delete, and the left/right arrows step over whole UTF-8
  characters, and line-wrapping counts a hanzi as two display columns.
- **Rendering** — `WP_render_line()` walks each line as UTF-8: ASCII draws with the
  monospaced Latin font; a multi-byte run draws one `efontCN_24` glyph at double
  width.
- **IME** — `src/service/IME/` loads `wubi86.bin` into PSRAM and binary-searches the
  typed code prefix to produce an ordered candidate list.

## Dictionary source / licensing

`data/wubi86.bin` is generated from the **rime-wubi** `wubi86.dict.yaml` table
(<https://github.com/rime/rime-wubi>) by `tools/gen_wubi.py`, filtered to the
~3,500 most frequent single hanzi. See the rime-wubi repository for its license and
original table authorship.

## Tuning

- Change the character-set size with `--top`, e.g. `python3 tools/gen_wubi.py --top 6000`.
- The editor body font is `efontCN_24`; the candidate bar uses `efontCN_16`. Smaller
  `efontCN_16` / `efontCN_14` are available in M5GFX if you want to save flash.
