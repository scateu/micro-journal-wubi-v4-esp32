# Project memory — micro-journal (Cardputer ADV)

Consolidated engineering notes for this firmware. Snapshot exported from the
assistant's project memory; treat the source files, `IME.md`, and
`FIRMWARE_FLASH.md` as authoritative if they diverge.

Contents:
- [Chinese IME engine + CJK rendering](#chinese-ime-engine--cjk-rendering)
- [IME schemes + firmware injection (IME3)](#ime-schemes--firmware-injection-ime3)
- [WordProcessor layout + rendering](#wordprocessor-layout--rendering)
- [Cardputer ADV backspace quirk](#cardputer-adv-backspace-quirk)
- [gfs() is the SD card](#gfs-is-the-sd-card)
- [Emacs / readline keybindings](#emacs--readline-keybindings)
- [USB drive export mode](#usb-drive-export-mode)
- [Build only cardputer-adv](#build-only-cardputer-adv)

---

## Chinese IME engine + CJK rendering

The Cardputer ADV firmware (`[env:cardputer-adv]`, 8 MB partition
`partition_cardputer_adv_8mb.csv`) adds a Chinese IME + Simplified-Chinese
display. ONE scheme per firmware: **Wubi / Pinyin / Shuangpin**, fixed at build
time. No on-device switching, no runtime table load. Full workflow + tools in
`IME.md`.

Key design points (all `#ifdef USE_IME`):
- **IME engine**: `src/service/IME/IME.cpp` reads `IME/ime_table.bin` (the
  "IME3" format) and binary-searches the typed code prefix. Records are
  `code[codeLen]+hanzi[3]+flag`, `codeLen=6`, sorted by code. Scheme + codeLen
  come from the 12-byte header (`magic "IME3" + scheme[1] + codeLen[1] +
  reserved[2] + count[4]`). `_maxCode` = 4/6/2 for wubi/pinyin/shuangpin caps
  the typed length; `IME::scheme()` drives the `[五]/[拼]/[双]` status indicator.
- **Dictionary is EMBEDDED IN FLASH in a fixed 512 KiB slot.** `platformio.ini`
  has `board_build.embed_files = IME/ime_table.bin`; objcopy emits
  `_binary_IME_ime_table_bin_start/_end` (symbol name is derived from the PATH:
  `IME/ime_table.bin` -> `_binary_IME_ime_table_bin_*`; moving the file changes
  the symbol, so the extern in IME.cpp must match). `IME::begin()` sets `_blob`;
  `readCode/readHanzi` are pointer reads by offset (no SD, no seeks, no file
  handle). Only the 2.7 KB prefix index is copied to RAM. Table padded to the
  512 KiB slot with 0xFF. Flash cost ~85% of the 3 MB app slot.
- **Prefix index (speed-up)**: 677-entry (`26*26+1`) uint32 first-two-letter
  lower-bound table between header and records. Entry `k=(c0-'a')*26+(c1-'a')` =
  first record index whose code sorts >= that 2-letter prefix; entry 676 is a
  sentinel == count, so prefix `ab` covers records `[index[k], index[k+1])`.
  `lookup()` calls `searchWindow()` to jump straight to that window and
  binary-searches inside it. `_recordBase = HEADER_SIZE + 677*4`.
- **Tooling (stdlib-only Python):** `IME/gen_ime.py --scheme wubi|pinyin|
  shuangpin` builds the IME3 table (pinyin from `IME/pinyin_simp.dict.yaml`,
  freq-ranked by weight; shuangpin = Xiaohe 2-letter map of the same).
  `IME/inject_ime.py` hot-swaps the table into a prebuilt `firmware.bin` by
  locating+validating the IME3 slot — lets non-PlatformIO users change the IME.
- **FLASH-ONLY — all SD-table code removed.** `IME.cpp/.h` no longer have
  `_file`, `_open`, `suspend()`, `ensureOpen()`, `readFull()`, `WUBI_PATH`, the
  `#ifdef WUBI_EMBEDDED` branch, or `<FS.h>`. The dictionary never touches the
  SD card, so IME lookups can never contend with journal writes.
- **REAL freeze cause (was NOT the dict): cross-core save-vs-edit race.** On
  ESP32-S3 the keyboard runs on **core 0** and the display on **core 1**
  (`main.cpp` pins `SecondaryCore` to core 1; `display_core()` returns 1). But
  autosave runs INSIDE the render path — `WP_render()` → `WP_check_saved()` →
  `Editor::saveFile()` — i.e. on **core 1**, while keystrokes mutate the same
  `Editor::buffer` on **core 0**. `saveFile()` reads `buffer` for the whole SD
  write while core 0 `memmove`s it → data race + shared-SPI contention →
  occasional hard freeze. FIX: a FreeRTOS **recursive mutex** in `Editor`
  (`EDITOR_LOCKING`, guarded by `BOARD_ESP32_S3`), taken by `Editor::Lock` RAII
  at the top of `keyboard()` and `saveFile()`. Recursive so
  `keyboard()`→`pageForward()`→`saveFile()` doesn't self-deadlock. No-op on
  BOARD_PICO (single core).
- **Wubi source = ywvim, NOT rime.** `IME/gen_ime.py --scheme wubi` parses the
  ywvim `IME/wubi.ywvim` `[Main]` section (space-sep, frequency order),
  single-hanzi candidates only. GOTCHA: the older rime-wubi `wubi86.dict.yaml`
  LACKS standard full codes for many base chars (e.g. `木` only under `#木 s`,
  `文` absent), so `ssss`/`yygy` returned nothing. ywvim is complete (`ssss`→木,
  `yygy`→文).
- **Single IME chokepoint**: `display_keyboard()` (`src/display/display.cpp`)
  calls `keyboard_ime_filter()` (`keyboard.cpp`) for every ASCII key, so BOTH
  the built-in and external USB-host keyboards get the IME with no per-driver
  code. Committed hanzi are re-emitted as UTF-8 bytes through display_keyboard
  with an `ime_emitting` re-entrancy guard.
- **Toggle**: Fn+Space (built-in) / Ctrl+Space (USB). IME ON by default at
  startup when the table loads.
- **Candidate keys** (`IME::handleKey`): digits 1-9 select; Space/Enter commit
  #1; Backspace deletes a code letter; ESC cancels. **Paging** (matters for long
  Pinyin lists): prev page = `-` / `;` / `,`; next page = `=` / `'` / `.`. A
  plain `-`/`=` only reaches the IME while composing (Ctrl-`-`/`=` font-size is
  intercepted earlier by the keyboard layer).
- **Storage**: hanzi stored as raw UTF-8 (3 bytes) in the Editor's byte buffer;
  files stay valid UTF-8. Backspace/arrows/delete step over whole UTF-8 chars.

Rendering (`display/CARDPUTER/WordProcessor/WordProcessor.cpp`): the line
renderer walks UTF-8 — ASCII via the profont, CJK via M5GFX built-in
`fonts::efontCN_*` (no external font file needed).

---

## IME schemes + firmware injection (IME3)

Supports **Wubi / Pinyin / Shuangpin**, ONE per firmware, fixed at build time.
User-facing guide: `IME.md`.

**IME3 table format** (`IME/gen_ime.py`): header `magic "IME3"[4] + scheme[1]
(0=wubi 1=pinyin 2=shuangpin) + codeLen[1](=6) + reserved[2] + count[4]`, then
677 uint32 prefix index, then `count` records of `code[6]+hanzi[3]+flag[1]`
sorted by code. Padded with 0xFF to a fixed **512 KiB slot**. The 6-byte code
field fits full pinyin (zhuang/chuang/shuang); wubi(≤4)/shuangpin(=2) fit
trivially.

**Sources:**
- Pinyin: `IME/pinyin_simp.dict.yaml` (rime rime-pinyin-simp; TSV
  `char<TAB>syllable<TAB>weight` after the `...` yaml header). Single-hanzi
  single-syllable rows only; ranked by DESC weight (score = -weight) so common
  chars come first. ~17k records, ~169 KiB.
- Shuangpin: derived from the same pinyin source via a **Xiaohe (小鹤)** map in
  gen_ime.py (`XIAOHE_INITIAL` zh/ch/sh→v/i/u; `XIAOHE_FINAL`; `XIAOHE_ZERO` for
  vowel-initial syllables). 411/415 syllables map; rare interjections
  (hm/m/n/ng) dropped. Same char+weight ranking, 2-letter codes.
- Wubi: ywvim `IME/wubi.ywvim [Main]`. `IME/ime_table.bin` ships the wubi table
  by default.

**Firmware injection** (`IME/inject_ime.py`, stdlib only): for users WITHOUT
PlatformIO. Scans a prebuilt `firmware.bin` for the IME3 slot by finding the
`IME3` magic and VALIDATING the header (scheme 0-2, codeLen 1-6, plausible
count) — NOT by alignment. GOTCHA: the embedded slot is NOT 4-byte aligned in
the .bin file (flash section file-offset differs from the mapped address; real
slot was at 0x13C3), and a stray "IME3" byte-sequence appears elsewhere — so
rely on header validation, not alignment. Overwrites the 512 KiB slot in place
(0xFF-padded), keeps a `.bak`.

**Makefile** (next to `IME.md`): `make wubi|pinyin|shuangpin` build
`firmware-<scheme>.bin`; `make upload-<scheme>` builds + flashes;
`make table-<scheme>` writes `IME/ime_table_<scheme>.bin`; `make inject-<scheme>
FIRMWARE_BIN=x` patches a prebuilt .bin. GOTCHA: the scheme build MUST always
regenerate + re-copy its table to the shared embedded path and rebuild (FORCE
prereq); a cached `firmware-<scheme>.bin` once let `upload-shuangpin` flash a
stale (pinyin) embed.

---

## WordProcessor layout + rendering

`src/display/CARDPUTER/WordProcessor/WordProcessor.cpp` — several constants are
tightly coupled; change one and re-check the others.

**Font size is runtime-selectable** (Ctrl-'+' / Ctrl-'-', both keyboards). The
former `const` metrics are mutable globals driven by a `FontLevel` table
(`FONT_LEVELS[]`, 3 levels): profont10+efontCN_10, profont12+efontCN_12
(default, `FONT_LEVEL_DEFAULT=1`, original look), profont17+efontCN_16.
`WP_set_font_size(level)` sets metrics, re-seats `g_profont22` (mutable) +
`g_cjkFont`, recomputes cols/rows, forces `clear_background`. GOTCHA: it sets
`Editor::cols/rows` DIRECTLY + calls `updateScreen()` — NOT `Editor::init()`,
which does `resetBuffer()` and would erase the open document mid-edit. Size
resets to default on each WP entry (not persisted). Combo mapping accepts
`+`/`=`→inc, `-`/`_`→dec; external HID adds keycodes 0x2D(-)/0x2E(=).

Metrics (profont12 default): glyph 6w×12h, ascent 8, descent 2; `font_width=7`,
`font_height=13`, `font_baseline=10`, `cjk_width=font_width*2=14`. `cols` =
display columns (Latin=1 col of 7px, hanzi=2 cols); 34×7=238px fits 240px.

- Layout: `editY=96` is the bottom text row. The edit/cursor line is NOT pinned
  to the bottom — normally at `cursorRowY = editY - LINES_BELOW_CURSOR*
  font_height` (`LINES_BELOW_CURSOR=2`), so two following lines stay visible
  below it. `rows = cursorRowY / font_height` (scrollback ABOVE). Status bar at
  height-18=117.
- **`WP_cursor_row_y()`** returns the edit line's ACTUAL render Y: near EOF the
  cursor line DROPS toward the bottom by the shortfall of following lines,
  reaching `editY` at the last line. Without this the cursor was stuck 2 rows up
  at EOF. Used by both `WP_render_text` branches and `WP_render_cursor` (which
  tracks `cursorY_prev` so the underline erases when the row shifts).
- **`WP_render_text` (on clear_background)** draws three bands around
  `WP_cursor_row_y()`: scrollback ABOVE (walk up until y<0/i<0 — fills the top
  even when the edit line dropped), the edit line, then following lines below
  (stop when y>editY). `clear_editline` repaints only the cursor-line strip.
- **GOTCHA (a higher line vanished, e.g. line 6 of 8 pressing DOWN at the last
  line):** every place touching the edit line's row must use `WP_cursor_row_y()`
  not the fixed `cursorRowY`. The `clear_editline` wipe rect was hard-coded to
  `cursorRowY`; near EOF the line renders lower, so the wipe erased a scrollback
  line never repainted. Fixed to clear at `WP_cursor_row_y()`.
- **Redraw triggers (`WP_render_clear`)**: full redraw when `cursorLine` OR
  `totalLine` changes; else incremental `clear_editline` on `cursorPos`/
  `bufferSize` change. GOTCHA: tracking only `cursorLine` missed DELETING a line
  (join) that shifts lines up without moving `cursorLine` — old lines stayed
  ("overlay"). Tracking `totalLine` fixes it (also wrap add/remove, paste).
- **Wubi IME bar does NOT reserve a row.** `imeBarY = editY` (bottom row). While
  composing, `WP_render_ime` OVERLAYS the bar there; on end it sets
  `clear_background = true` to force a full redraw NEXT frame (surgical single-
  line repaint was fragile). GOTCHA: `WP_render()` reset `clear_background=false`
  AFTER `WP_render_ime()`, clearing the request before it fired → covered line
  stayed blank. Fix: capture `full_redraw_this_frame` BEFORE render calls and
  only reset if it was set at frame start, so a late IME request survives.
- **Responsiveness**: `display_CARDPUTER_loop` refresh throttle 33ms (~30fps,
  was a legacy 150ms that lagged typed chars); `KEY_DEBOUNCE_INTERVAL` 40ms (was
  100ms). Safe because rendering is incremental.

Core rendering invariants:
- **`Editor::updateScreen` iterates per WHOLE character (clen bytes), never per
  byte** — guarantees a line boundary never splits a 3-byte hanzi (the "ä\nä"
  bug). Wrapping is pre-emptive: break BEFORE a char when `display_col +
  char_cols > cols` (word wrap at last space, else hard wrap). `lineLengths`
  counts the trailing `\n`.
- **Anti-flash is layered**: `WP_render_clear` full-wipe only on line/page
  change; same-line edits wipe just the edit-line strip. Every status-bar
  element is cached in a `static _prev` and repainted only on change. Repainting
  the whole bar every frame was the flicker.

---

## Cardputer ADV backspace quirk

On the M5Cardputer **ADV** keyboard (M5Cardputer lib 1.1.1, TCA8418 reader), a
plain **Backspace** sets `status.backspace = true`; `status.del` is ONLY set for
the Fn-layer DELETE (Fn+Backspace). The original code assumed plain backspace set
`status.del`, so backspace did nothing on the ADV until `keypad_CardPuter.cpp`
was changed to check `status.backspace || status.del`.

Related: **while FN is held, `keysState()` returns after the fn-layer pass and
NEVER sets `status.space`** (or other PASS-3 print flags). Detect a "Fn+Space"
chord via `M5Cardputer.Keyboard.isKeyPressed(' ')` (raw key list), not
`status.space`. Ctrl+Space works via the normal path (`status.ctrl &&
status.space`) because Ctrl is not the fn layer.

The lib auto-detects `board_M5CardputerADV` in `M5.begin()`; the public API is
identical to the original Cardputer, so one image runs on both boards.

---

## gfs() is the SD card

On the Cardputer / Cardputer-ADV envs, `gfs()` (`src/app/app.cpp`) returns a
**`FileSystemSD`** because the env defines `SD_CS`. Journals are read from the
**SD card**, NOT internal flash. The `board_build.filesystem=spiffs` / SPIFFS
partition only affects the data-image upload partition; the running firmware
does not read journals from SPIFFS. Runtime files must be on the SD card root,
not flashed via `uploadfs`. (The IME dictionary is now embedded in flash, not on
SD — see the IME sections.)

---

## Emacs / readline keybindings

The Cardputer editor has Emacs/readline shortcuts. The mapping is shared by BOTH
keyboards via `keyboard_editor_combo(letter, ctrl, meta)` in `keyboard.cpp`
(pure lookup `keyboard_combo_code()` + a `keyboard_editor_combo_bound()`
predicate), dispatched through `display_keyboard()`:
- **Internal** matrix: `keypad_CardPuter.cpp` (non-Fn `else` branch). Meta =
  `status.alt || status.opt`.
- **External** USB HID: `keyboard_HID2Ascii()`. Letter from HID keycode
  (0x04='a'..0x1D='z'); ctrl = modifier bit 0|4, meta = alt (bit 2|6). Dispatch
  on press only; the release edge is swallowed via
  `keyboard_editor_combo_bound()`. GOTCHA: `bool ctrl` must be declared OUTSIDE
  `#ifdef USE_IME` (rev_7/rev_5 USB builds compile this without USE_IME).

**Modifier facts:** `KeysState` has `ctrl`, `alt`, AND `opt`. When Ctrl/Shift is
held the lib pushes the **shifted** char into `status.word` (Ctrl-A → `'A'`), so
the mapping lower-cases it. Meta-only combos get `value_first` (lowercase).

**Bindings.** Cursor moves + delete REUSE existing control codes: C-a→Home(2),
C-e→End(3), C-b→Left(18), C-f→Right(19), C-p→Up(20), C-n→Down(21), C-d→Del(127).
New action codes in `display.h` (avoid 2,3,6,8,10,18-23,27,28=FN,127):
`KEY_SAVE 0x07` (C-s), `KEY_KILL_LINE 0x0B` (C-k), `KEY_WORD_FWD 0x0E` (M-f),
`KEY_WORD_BACK 0x0F` (M-b), `KEY_KILL_WORD_FWD 0x10` (M-d), `KEY_YANK 0x11`
(C-y), `KEY_FONT_INC 0x18` / `KEY_FONT_DEC 0x19` (Ctrl-'+'/'-'; NOT 0x12/0x13,
which are Left/Right arrows).

- **C-s** handled in `WP_keyboard()` (saves in place, returns before the buffer
  sees it) — unlike ESC which saves AND opens the menu.
- The rest handled in `Editor::keyboard()` in a branch before CURSORS.

**Editor methods** (`Editor.cpp`): `moveWordForward/Backward`, `killToEndOfLine`,
`killWordForward`, `yank`. A "word" = alphanumeric ASCII OR any byte >= 0x80 (so
hanzi count); motions step whole UTF-8 chars. C-k at EOL kills the `\n` (joins
next line). Single `killBuffer`; consecutive kills APPEND (`lastActionWasKill`,
reset at the top of `keyboard()` for any non-kill key).

---

## USB drive export mode

Cardputer ADV can expose its **SD/TF card** to a host PC as a USB drive, for
copying journals off. Enabled by `-D USE_USB_DRIVE` in `[env:cardputer-adv]`.

**Trigger:** hold **`e`** (export) at power-on. Detected in `app_setup()` right
after `M5Cardputer.begin()` (polls `Keyboard.isKeyPressed('e')` a few times with
a settle delay). If held → `_usbDrive=true`, `usbdrive_begin()`, screen =
`USBDRIVESCREEN`; if the SD is missing → `ERRORSCREEN` ("NO SD CARD").
`app_setup()` then `return`s early (skips config/editor/MSC setup) with
`_ready=true` so the display loop renders the status/error screen.

**Not the flash MSC path.** SEPARATE from `USE_MSC` (`service/MassStorage/
esp32`), which exposes an internal *flash* partition and repurposes `gfs()`.
cardputer-adv does NOT define `USE_MSC`; `gfs()` stays `FileSystemSD`. Code is
`src/service/UsbDrive/UsbDrive.{h,cpp}` (guarded by `USE_USB_DRIVE &&
BOARD_ESP32_S3`), added to the cardputer-adv `build_src_filter`.

**How it works:** `usbdrive_begin()` does its OWN `SD.begin()` then core
`USBMSC` with read/write callbacks straight to `SD.readRAW`/`SD.writeRAW` (raw
sectors), then `USB.begin()`. `usbdrive_loop()` updates `app["usbDriveStatus"]`
(WAITING/CONNECTED/TRANSFERRING/SAFE-TO-UNPLUG) for the status screen.

**USB host vs device (key constraint):** the ESP32-S3 OTG port can't be both a
USB *host* (external keyboard, `EspUsbHost`) and a USB *device* (MSC) at once.
So in export mode `main.cpp` skips `keyboard_setup()`/`keyboard_loop()` (via
`usbdrive_mode()`), and `app_loop()` early-returns after `usbdrive_loop()`. Env
is `ARDUINO_USB_MODE=0` (OTG) + CDC-on-boot, so MSC is another device interface
alongside CDC. To leave export mode: eject on the PC, then reboot.

**GOTCHA (screen showed the editor despite export mode):** `display_setup()`
(runs AFTER `app_setup()`) re-selects the screen and its `else` branch OVERWRITES
`app["screen"]` to WORDPROCESSOR/WAKEUP/KEYBOARD — it only preserved
`ERRORSCREEN`/`UPDATESCREEN`. Added a `USBDRIVESCREEN` early-return there too.

---

## Build only cardputer-adv

When verifying builds, only run `pio run -e cardputer-adv`. Do NOT build `rev_7`
(or other envs) as a cross-check unless explicitly asked — the user works on the
Cardputer ADV target. Keep shared-file changes portable/guarded, but only
compile-check with `cardputer-adv`.
