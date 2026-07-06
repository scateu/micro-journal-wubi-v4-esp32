# micro-journal (Cardputer ADV) build helper
#
# Common flows:
#   make wubi                 build firmware-wubi.bin      (Wubi   IME [五])
#   make pinyin               build firmware-pinyin.bin    (Pinyin IME [拼])
#   make shuangpin            build firmware-shuangpin.bin (小鹤 Shuangpin [双])
#   make upload-pinyin        build the Pinyin firmware + flash
#   make monitor              open the serial monitor
#   make clean                remove build artifacts
#
# Each scheme has its own generated table (IME/ime_table_<scheme>.bin) and its
# own firmware artifact (firmware-<scheme>.bin). The build embeds the fixed path
# IME/ime_table.bin, so a scheme build copies its table there first.
# See IME.md for the table format and the no-build-env injector.

# ---- config ----------------------------------------------------------------
ENV      := cardputer-adv
PIO      := pio
PYTHON   := python3

GEN      := IME/gen_ime.py
INJECT   := IME/inject_ime.py
EMBED    := IME/ime_table.bin            # fixed path embedded by platformio.ini
BUILT    := .pio/build/$(ENV)/firmware.bin

SCHEMES  := wubi pinyin shuangpin

# scheme -> source table
SRC_wubi      := IME/wubi.ywvim
SRC_pinyin    := IME/pinyin_simp.dict.yaml
SRC_shuangpin := IME/pinyin_simp.dict.yaml

# scheme -> reserved flash slot. Wubi carries multi-hanzi phrases and needs a
# bigger slot (896 KiB); pinyin/shuangpin are single-char and fit 512 KiB. The
# slot size is baked into the firmware image (the embedded table is padded to
# it), so `inject` must use a matching --slot.
SLOT_wubi      := 917504
SLOT_pinyin    := 524288
SLOT_shuangpin := 524288

# ---- default ---------------------------------------------------------------
.DEFAULT_GOAL := help

# ---- IME tables (no build) --------------------------------------------------
# `make table-<scheme>` writes IME/ime_table_<scheme>.bin (a real file target).
IME/ime_table_%.bin:
	$(PYTHON) $(GEN) --scheme $* --src $(SRC_$*) --out $@ --slot $(SLOT_$*)

.PHONY: table-wubi table-pinyin table-shuangpin
table-wubi:      IME/ime_table_wubi.bin
table-pinyin:    IME/ime_table_pinyin.bin
table-shuangpin: IME/ime_table_shuangpin.bin

# ---- per-scheme firmware artifacts -----------------------------------------
# The build embeds the SHARED path $(EMBED), so a scheme build MUST regenerate
# its own table, re-copy it to $(EMBED), and rebuild every time - a cached
# firmware-<scheme>.bin once let `upload-shuangpin` flash a stale (pinyin)
# embed. The FORCE prereq makes the pattern rule always run. `flash=1` uploads.
FORCE:
.PHONY: FORCE

firmware-%.bin: FORCE
	$(PYTHON) $(GEN) --scheme $* --src $(SRC_$*) --out IME/ime_table_$*.bin --slot $(SLOT_$*)
	cp IME/ime_table_$*.bin $(EMBED)
	$(PIO) run -e $(ENV)$(if $(flash), -t upload)
	cp $(BUILT) $@
	@echo "built $@ ($* IME)$(if $(flash), + flashed)"

# Friendly aliases: `make wubi` -> build firmware-wubi.bin, etc.
.PHONY: $(SCHEMES)
wubi:      firmware-wubi.bin
pinyin:    firmware-pinyin.bin
shuangpin: firmware-shuangpin.bin

# ---- build that scheme + flash ---------------------------------------------
# Reuses the firmware-<scheme>.bin recipe with flash=1, so it ALWAYS regenerates
# + re-embeds the scheme's table before flashing - upload-shuangpin can never
# flash a stale/other-scheme embed.
.PHONY: upload-wubi upload-pinyin upload-shuangpin
upload-wubi upload-pinyin upload-shuangpin: upload-%:
	$(MAKE) firmware-$*.bin flash=1

# ---- plain build / flash / monitor -----------------------------------------
# Build/flash with whatever IME/ime_table.bin is currently embedded.
.PHONY: build upload monitor
build:
	$(PIO) run -e $(ENV)

upload:
	$(PIO) run -e $(ENV) -t upload

monitor:
	$(PIO) device monitor

# ---- inject into a prebuilt firmware (no build toolchain) -------------------
#   make inject-pinyin FIRMWARE_BIN=firmware_cardputer_adv.bin
# Generates the scheme table then patches FIRMWARE_BIN in place (keeps a .bak).
FIRMWARE_BIN ?= $(BUILT)
.PHONY: inject-wubi inject-pinyin inject-shuangpin
inject-wubi inject-pinyin inject-shuangpin: inject-%: IME/ime_table_%.bin
	$(PYTHON) $(INJECT) --firmware $(FIRMWARE_BIN) --table $< --slot $(SLOT_$*)

# ---- housekeeping ----------------------------------------------------------
.PHONY: clean distclean
clean:
	$(PIO) run -e $(ENV) -t clean

# also drop the generated per-scheme tables + firmware artifacts
distclean: clean
	rm -f IME/ime_table_wubi.bin IME/ime_table_pinyin.bin IME/ime_table_shuangpin.bin
	rm -f firmware-wubi.bin firmware-pinyin.bin firmware-shuangpin.bin

.PHONY: help
help:
	@echo "micro-journal (Cardputer ADV) targets:"
	@echo "  make wubi | pinyin | shuangpin       build firmware-<scheme>.bin"
	@echo "  make upload-<scheme>                 build that scheme + flash"
	@echo "  make firmware-<scheme>.bin           build that scheme's firmware artifact"
	@echo "  make table-<scheme>                  write IME/ime_table_<scheme>.bin only"
	@echo "  make build | upload | monitor        use the currently-embedded table"
	@echo "  make inject-<scheme> FIRMWARE_BIN=x  swap IME in a prebuilt .bin (no build)"
	@echo "  make clean | distclean               remove build artifacts"
