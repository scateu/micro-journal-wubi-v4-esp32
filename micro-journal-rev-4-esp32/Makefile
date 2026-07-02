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

# ---- default ---------------------------------------------------------------
.DEFAULT_GOAL := help

# ---- IME tables (no build) --------------------------------------------------
# `make table-<scheme>` writes IME/ime_table_<scheme>.bin (a real file target).
IME/ime_table_%.bin:
	$(PYTHON) $(GEN) --scheme $* --src $(SRC_$*) --out $@

.PHONY: table-wubi table-pinyin table-shuangpin
table-wubi:      IME/ime_table_wubi.bin
table-pinyin:    IME/ime_table_pinyin.bin
table-shuangpin: IME/ime_table_shuangpin.bin

# ---- per-scheme firmware artifacts -----------------------------------------
# `make firmware-<scheme>.bin`: embed that scheme's table, build, copy out.
# .PHONY-ish: always rebuild (source deps are broad; keep it simple + explicit).
firmware-%.bin: IME/ime_table_%.bin
	cp $< $(EMBED)
	$(PIO) run -e $(ENV)
	cp $(BUILT) $@
	@echo "built $@ ($* IME)"

# Friendly aliases: `make wubi` -> firmware-wubi.bin, etc.
.PHONY: $(SCHEMES)
wubi:      firmware-wubi.bin
pinyin:    firmware-pinyin.bin
shuangpin: firmware-shuangpin.bin

# ---- build that scheme + flash ---------------------------------------------
.PHONY: upload-wubi upload-pinyin upload-shuangpin
upload-wubi upload-pinyin upload-shuangpin: upload-%: firmware-%.bin
	$(PIO) run -e $(ENV) -t upload

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
	$(PYTHON) $(INJECT) --firmware $(FIRMWARE_BIN) --table $<

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
