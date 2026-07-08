# micro-journal (Cardputer ADV) build helper
#
# This Makefile is written out the long way ON PURPOSE: no variables, no loops,
# no pattern rules. Every target spells out the exact commands it runs, so you
# can read a recipe top-to-bottom and see precisely what happens - or copy the
# lines straight into a terminal. Think of it as a runnable manual.
#
# Common flows:
#   make wubi                 build firmware-wubi.bin      (Wubi   IME [五])
#   make pinyin               build firmware-pinyin.bin    (Pinyin IME [拼])
#   make shuangpin            build firmware-shuangpin.bin (小鹤 Shuangpin [双])
#   make upload-pinyin        build the Pinyin firmware + flash
#   make monitor              open the serial monitor
#   make clean                remove build artifacts
#
# How the IME gets into the firmware:
#   Each scheme has its own generated table (IME/ime_table_<scheme>.bin) and its
#   own firmware artifact (firmware-<scheme>.bin). The firmware always embeds one
#   FIXED path, IME/ime_table.bin (see platformio.ini), so a scheme build copies
#   its table onto that fixed path first, then builds.
#
# Reserved flash slot per scheme:
#   Wubi carries multi-hanzi phrases and needs a bigger slot (896 KiB = 917504).
#   Pinyin/Shuangpin are single-char and fit 512 KiB = 524288. The slot size is
#   baked into the firmware image (the embedded table is padded to it), so the
#   inject targets pass a matching --slot.
#
# See IME.md for the table format and the no-build-env injector.

.DEFAULT_GOAL := help

# =============================================================================
# IME tables only (no firmware build)
#   Runs gen_ime.py to produce IME/ime_table_<scheme>.bin. Nothing else.
# =============================================================================

.PHONY: table-wubi
table-wubi:
	python3 IME/gen_ime.py --scheme wubi --src IME/wubi86.dict.yaml --out IME/ime_table_wubi.bin --slot 917504

.PHONY: table-pinyin
table-pinyin:
	python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_pinyin.bin --slot 524288

.PHONY: table-shuangpin
table-shuangpin:
	python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_shuangpin.bin --slot 524288

# =============================================================================
# Per-scheme firmware (build, no flash)
#   Each target regenerates its table, copies it onto the fixed embedded path
#   IME/ime_table.bin, builds, then saves the result as firmware-<scheme>.bin.
#
#   These are .PHONY (always run) on purpose: because every scheme shares the
#   one embedded path IME/ime_table.bin, a cached firmware-<scheme>.bin once let
#   `upload-shuangpin` flash a stale (pinyin) embed. Always regenerate + rebuild.
# =============================================================================

.PHONY: wubi
wubi:
	python3 IME/gen_ime.py --scheme wubi --src IME/wubi86.dict.yaml --out IME/ime_table_wubi.bin --slot 917504
	cp IME/ime_table_wubi.bin IME/ime_table.bin
	pio run -e cardputer-adv
	cp .pio/build/cardputer-adv/firmware.bin firmware-wubi.bin
	@echo "built firmware-wubi.bin (wubi IME)"

.PHONY: pinyin
pinyin:
	python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_pinyin.bin --slot 524288
	cp IME/ime_table_pinyin.bin IME/ime_table.bin
	pio run -e cardputer-adv
	cp .pio/build/cardputer-adv/firmware.bin firmware-pinyin.bin
	@echo "built firmware-pinyin.bin (pinyin IME)"

.PHONY: shuangpin
shuangpin:
	python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_shuangpin.bin --slot 524288
	cp IME/ime_table_shuangpin.bin IME/ime_table.bin
	pio run -e cardputer-adv
	cp .pio/build/cardputer-adv/firmware.bin firmware-shuangpin.bin
	@echo "built firmware-shuangpin.bin (shuangpin IME)"

# =============================================================================
# Per-scheme build + flash
#   Same steps as above, but `pio run` also uploads (-t upload). Always
#   regenerates + re-embeds first, so you can never flash another scheme's embed.
# =============================================================================

.PHONY: upload-wubi
upload-wubi:
	python3 IME/gen_ime.py --scheme wubi --src IME/wubi86.dict.yaml --out IME/ime_table_wubi.bin --slot 917504
	cp IME/ime_table_wubi.bin IME/ime_table.bin
	pio run -e cardputer-adv -t upload
	cp .pio/build/cardputer-adv/firmware.bin firmware-wubi.bin
	@echo "built firmware-wubi.bin (wubi IME) + flashed"

.PHONY: upload-pinyin
upload-pinyin:
	python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_pinyin.bin --slot 524288
	cp IME/ime_table_pinyin.bin IME/ime_table.bin
	pio run -e cardputer-adv -t upload
	cp .pio/build/cardputer-adv/firmware.bin firmware-pinyin.bin
	@echo "built firmware-pinyin.bin (pinyin IME) + flashed"

.PHONY: upload-shuangpin
upload-shuangpin:
	python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_shuangpin.bin --slot 524288
	cp IME/ime_table_shuangpin.bin IME/ime_table.bin
	pio run -e cardputer-adv -t upload
	cp .pio/build/cardputer-adv/firmware.bin firmware-shuangpin.bin
	@echo "built firmware-shuangpin.bin (shuangpin IME) + flashed"

# =============================================================================
# Plain build / flash / monitor
#   Uses whatever table is currently at IME/ime_table.bin. No regeneration.
# =============================================================================

.PHONY: build
build:
	pio run -e cardputer-adv

.PHONY: upload
upload:
	pio run -e cardputer-adv -t upload

.PHONY: monitor
monitor:
	pio device monitor

# =============================================================================
# Inject an IME into an ALREADY-BUILT firmware (no build toolchain needed)
#   Generates the scheme table, then patches the given firmware .bin in place
#   (inject_ime.py keeps a .bak). Point FIRMWARE_BIN at your prebuilt image:
#     make inject-pinyin FIRMWARE_BIN=firmware_cardputer_adv.bin
#   If you omit FIRMWARE_BIN it defaults to the last local build output.
# =============================================================================

FIRMWARE_BIN ?= .pio/build/cardputer-adv/firmware.bin

.PHONY: inject-wubi
inject-wubi:
	python3 IME/gen_ime.py --scheme wubi --src IME/wubi86.dict.yaml --out IME/ime_table_wubi.bin --slot 917504
	python3 IME/inject_ime.py --firmware $(FIRMWARE_BIN) --table IME/ime_table_wubi.bin --slot 917504

.PHONY: inject-pinyin
inject-pinyin:
	python3 IME/gen_ime.py --scheme pinyin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_pinyin.bin --slot 524288
	python3 IME/inject_ime.py --firmware $(FIRMWARE_BIN) --table IME/ime_table_pinyin.bin --slot 524288

.PHONY: inject-shuangpin
inject-shuangpin:
	python3 IME/gen_ime.py --scheme shuangpin --src IME/pinyin_simp.dict.yaml --out IME/ime_table_shuangpin.bin --slot 524288
	python3 IME/inject_ime.py --firmware $(FIRMWARE_BIN) --table IME/ime_table_shuangpin.bin --slot 524288

# =============================================================================
# Housekeeping
# =============================================================================

.PHONY: clean
clean:
	pio run -e cardputer-adv -t clean

# clean + drop the generated per-scheme tables and firmware artifacts
.PHONY: distclean
distclean:
	pio run -e cardputer-adv -t clean
	rm -f IME/ime_table_wubi.bin IME/ime_table_pinyin.bin IME/ime_table_shuangpin.bin
	rm -f firmware-wubi.bin firmware-pinyin.bin firmware-shuangpin.bin

.PHONY: help
help:
	@echo "micro-journal (Cardputer ADV) targets:"
	@echo "  make wubi | pinyin | shuangpin       build firmware-<scheme>.bin"
	@echo "  make upload-wubi | upload-pinyin | upload-shuangpin"
	@echo "                                       build that scheme + flash"
	@echo "  make table-wubi | table-pinyin | table-shuangpin"
	@echo "                                       write IME/ime_table_<scheme>.bin only"
	@echo "  make build | upload | monitor        use the currently-embedded table"
	@echo "  make inject-wubi | inject-pinyin | inject-shuangpin FIRMWARE_BIN=x"
	@echo "                                       swap IME in a prebuilt .bin (no build)"
	@echo "  make clean | distclean               remove build artifacts"
