# micro-journal (Cardputer ADV) build helper
#
# Common flows:
#   make wubi            build firmware with the Wubi   IME  [五]
#   make pinyin          build firmware with the Pinyin IME  [拼]
#   make shuangpin       build firmware with the Shuangpin   [双]  (小鹤/Xiaohe)
#   make upload-pinyin   build + flash the Pinyin firmware
#   make monitor         open the serial monitor
#   make clean           remove build artifacts
#
# Each IME target regenerates IME/ime_table.bin for that scheme, then builds.
# See IME.md for the table format and the no-build-env injector.

# ---- config ----------------------------------------------------------------
ENV      := cardputer-adv
PIO      := pio
PYTHON   := python3

GEN      := IME/gen_ime.py
INJECT   := IME/inject_ime.py
TABLE    := IME/ime_table.bin
FIRMWARE := .pio/build/$(ENV)/firmware.bin

# scheme -> source table
SRC_wubi      := IME/wubi.ywvim
SRC_pinyin    := IME/pinyin_simp.dict.yaml
SRC_shuangpin := IME/pinyin_simp.dict.yaml

# ---- default ---------------------------------------------------------------
.DEFAULT_GOAL := help

# ---- per-IME firmware ------------------------------------------------------
# `make wubi|pinyin|shuangpin` regenerates the embedded table then builds.
.PHONY: wubi pinyin shuangpin
wubi pinyin shuangpin: %: table-% build

# `make upload-wubi|-pinyin|-shuangpin` does the same, then flashes.
.PHONY: upload-wubi upload-pinyin upload-shuangpin
upload-wubi upload-pinyin upload-shuangpin: upload-%: table-% upload

# ---- IME table generation (no build) --------------------------------------
# `make table-pinyin` etc. writes IME/ime_table.bin for that scheme only.
.PHONY: table-wubi table-pinyin table-shuangpin
table-wubi table-pinyin table-shuangpin: table-%:
	$(PYTHON) $(GEN) --scheme $* --src $(SRC_$*) --out $(TABLE)

# ---- plain build / flash / monitor -----------------------------------------
# Build/flash with whatever IME/ime_table.bin is currently present.
.PHONY: build upload monitor
build:
	$(PIO) run -e $(ENV)

upload:
	$(PIO) run -e $(ENV) -t upload

monitor:
	$(PIO) device monitor

# ---- inject into a prebuilt firmware (no build toolchain) -------------------
# Swap the IME in an already-built/released firmware .bin. Example:
#   make inject-pinyin FIRMWARE_BIN=firmware_cardputer_adv.bin
# Regenerates a scheme table to /tmp then patches FIRMWARE_BIN in place (+ .bak).
FIRMWARE_BIN ?= $(FIRMWARE)
.PHONY: inject-wubi inject-pinyin inject-shuangpin
inject-wubi inject-pinyin inject-shuangpin: inject-%:
	$(PYTHON) $(GEN) --scheme $* --src $(SRC_$*) --out /tmp/ime_$*.bin
	$(PYTHON) $(INJECT) --firmware $(FIRMWARE_BIN) --table /tmp/ime_$*.bin

# ---- housekeeping ----------------------------------------------------------
.PHONY: clean
clean:
	$(PIO) run -e $(ENV) -t clean

.PHONY: help
help:
	@echo "micro-journal (Cardputer ADV) targets:"
	@echo "  make wubi | pinyin | shuangpin       regenerate that IME table + build"
	@echo "  make upload-<scheme>                 build that IME + flash"
	@echo "  make table-<scheme>                  write IME/ime_table.bin only"
	@echo "  make build | upload | monitor        use the current IME/ime_table.bin"
	@echo "  make inject-<scheme> FIRMWARE_BIN=x  swap IME in a prebuilt .bin (no build)"
	@echo "  make clean                           remove build artifacts"
