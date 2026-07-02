# Flashing the firmware with esptool

For the Cardputer ADV (`[env:cardputer-adv]`). Use this if you don't want to run
a PlatformIO upload — e.g. to flash a released `firmware_cardputer_adv.bin`, or a
per-scheme `firmware-<scheme>.bin`, or one patched by `IME/inject_ime.py`.

**Chip:** ESP32-S3 · **flash:** 8 MB, QIO, 80 MHz · standard Arduino-S3 offsets.

A `pio run -e cardputer-adv` build produces, in `.pio/build/cardputer-adv/`:

```
bootloader.bin      partitions.bin      firmware.bin
```

plus `boot_app0.bin` from the framework:

```
~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
```

---

## App only (fastest — you just changed firmware / IME)

```sh
esptool.py --chip esp32s3 --port /dev/tty.usbmodem* --baud 921600 \
  write_flash -z 0x10000 .pio/build/cardputer-adv/firmware.bin
```

Use this to flash any app image at `0x10000`, e.g. a per-scheme build
(`firmware-shuangpin.bin`) or an injected release
(`firmware_cardputer_adv.bin`) — those are plain app images.

## Full flash (bootloader + partitions + app) — first time / clean

```sh
cd .pio/build/cardputer-adv

esptool.py --chip esp32s3 --port /dev/tty.usbmodem* --baud 921600 \
  write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0000  bootloader.bin \
  0x8000  partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 firmware.bin
```

### Flash offsets (ESP32-S3)

| offset   | image            |
|----------|------------------|
| `0x0000` | bootloader.bin   |
| `0x8000` | partitions.bin   |
| `0xe000` | boot_app0.bin    |
| `0x10000`| firmware.bin (app) |

---

## Notes

- **Port:** replace `/dev/tty.usbmodem*` with your actual port
  (`ls /dev/tty.usbmodem*` on macOS, `/dev/ttyACM0` on Linux). If auto-detect
  works you can drop `--port`.
- **`--flash_mode`:** the build uses `qio`, but flashing with **`dio`** is the
  safe, universally-compatible choice on the S3 (what the Arduino stub uses).
  Either works; use `dio` if unsure.
- **Download mode:** if esptool can't connect, hold **G0 / BOOT**, tap
  **RESET**, then release BOOT (the Cardputer boots straight to the app
  otherwise).
- **`-z`** compresses the transfer (faster). Drop `--baud` to `115200` if
  `921600` is unreliable.
- **esptool v3 vs v4:** newer esptool is invoked as `esptool` (no `.py`); the
  flags are identical.
- **Erase everything** (recover a bricked/confused flash), then do a full flash:
  ```sh
  esptool.py --chip esp32s3 --port /dev/tty.usbmodem* erase_flash
  ```

---

## Switching the IME without rebuilding

To change the on-device IME (Wubi / Pinyin / Shuangpin) on a prebuilt image,
patch it first, then flash the app only:

```sh
python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out my_table.bin
python3 IME/inject_ime.py --firmware firmware_cardputer_adv.bin --table my_table.bin
esptool.py --chip esp32s3 --port /dev/tty.usbmodem* --baud 921600 \
  write_flash -z 0x10000 firmware_cardputer_adv.bin
```

See `IME.md` for details.
