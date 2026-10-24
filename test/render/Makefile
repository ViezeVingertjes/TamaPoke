# Host renderer: builds TamaPoke.ino against a software canvas.
#   make shots               -> build/shots/*.ppm (English, 466x466)
#   make shots FILTER=main_day  -> only that screen
#   make taps                -> taps the center of every button and dialog (accepts SIZE=)
#   make power               -> which screens repaint and when (battery saving)
#   make fit                 -> every real text fits in its box or in the circle
#   make check               -> compares with golden/en and golden/sq480 (fails if any screen changes)
#   make golden              -> regenerates golden/ (only if the interface change is intentional)
#   make readme              -> docs/screens/*.png for the README (from golden/)
#   make shots SIZE=240      -> another canvas size (square, in pixels)
#   make shots ROUND=0       -> square panel (full-screen menus); round by default
# Builds the graphics code of the GFX Library for Arduino library
# (GFX_FONT_FILE if it is not in ~/Arduino).
CXX      ?= g++
GFX_FONT_FILE ?= $(firstword $(wildcard $(HOME)/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h \
                  $(HOME)/Documents/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h))
SHOTS    ?= $(BUILD)/shots
SD_DIR   ?= tools/sdcard
GFX_SRC  := $(patsubst %/font/glcdfont.h,%,$(GFX_FONT_FILE))
ifneq ($(MAKECMDGOALS),clean)
ifeq ($(GFX_FONT_FILE),)
$(error falta la libreria GFX Library for Arduino: instalala o pasa GFX_FONT_FILE=.../src/font/glcdfont.h)
endif
endif
ROOT     := ../..
# each variant (SIZE/ROUND) builds in its own folder: so check does not rebuild the same binary
BUILD    := build$(if $(SIZE)$(ROUND),/v$(SIZE)_$(ROUND))
BIN      := $(BUILD)/render
OPT      ?= -O1 -g
CXXFLAGS_ALL := -std=c++17 $(OPT) -Wno-narrowing -Wno-write-strings -fpermissive \
                -ffp-contract=off $(if $(SIZE),-DLCD_WIDTH=$(SIZE) -DLCD_HEIGHT=$(SIZE)) $(if $(ROUND),-DLCD_ROUND=$(ROUND)) -I shim -I ../shim -I $(ROOT) -I $(BUILD) -I $(GFX_SRC) $(CXXFLAGS)
GFX_SOURCES := $(GFX_SRC)/Arduino_GFX.cpp $(GFX_SRC)/Arduino_G.cpp $(GFX_SRC)/Arduino_DataBus.cpp \
               $(GFX_SRC)/canvas/Arduino_Canvas.cpp
SOURCES  := render_main.cpp host.cpp ../shim/shim.cpp $(ROOT)/pet.cpp $(ROOT)/i18n.cpp $(ROOT)/sdmon.cpp\
            $(GFX_SOURCES)
DEPS     := $(wildcard $(ROOT)/*.h) $(wildcard shim/*) $(wildcard ../shim/*) $(GFX_FONT_FILE) \
            $(wildcard $(GFX_SRC)/*.h) $(wildcard $(GFX_SRC)/*/*.h) $(BUILD)/flags

# the recipe depends on the flags and the font: if they change, it rebuilds
$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CXX) $(CXXFLAGS_ALL)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS_ALL)' > $@
FORCE:

.PHONY: shots taps fit power check golden readme clean
shots: $(BIN)
	@mkdir -p $(SHOTS) && find $(SHOTS) -maxdepth 1 -type f -delete
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) $(abspath $(SHOTS)) $(FILTER)

# the references depend on these versions: with others, "check" would fail on everything
# and "golden" would contaminate them (PIN_CHECK=0 skips the check)
GFX_VER := 1.6.9
.PHONY: versions
versions:
	@[ "$(PIN_CHECK)" = 0 ] || { \
	  g=$$(sed -n 's/^version=//p' $(GFX_SRC)/../library.properties); \
	  [ "$$g" = "$(GFX_VER)" ] || { \
	    echo "las referencias son de GFX Library $(GFX_VER); aqui hay $$g (PIN_CHECK=0 para ignorarlo)"; exit 1; }; }

# renders both variants into build/check/{en,sq480} (separate from build/shots)
CHECKDIR := $(BUILD)/check
.PHONY: renders nosize
nosize:
	@test -z "$(SIZE)$(ROUND)" || { echo "SIZE y ROUND solo valen para shots, taps y fit: las referencias en son de 466 px redondos"; exit 1; }
renders: nosize $(BIN) versions
	@rm -rf $(CHECKDIR)
	@$(MAKE) --no-print-directory shots SHOTS=$(CURDIR)/$(CHECKDIR)/en >/dev/null || exit 1
	@$(MAKE) --no-print-directory shots SIZE=480 ROUND=0 SHOTS=$(CURDIR)/$(CHECKDIR)/sq480 >/dev/null || exit 1

check: renders
	@fail=0; echo "== en"; python3 compare.py $(CHECKDIR)/en golden/en || fail=1; \
	echo "== sq480 (panel cuadrado de 480 px)"; python3 compare.py $(CHECKDIR)/sq480 golden/sq480 || fail=1; exit $$fail

# only writes if both variants rendered correctly
golden: renders
	@python3 compare.py --update --prune $(CHECKDIR)/en golden/en || exit 1
	@python3 compare.py --update --prune $(CHECKDIR)/sq480 golden/sq480

# README images (docs/screens/), from the golden/ references
readme:
	python3 readme_shots.py

taps: $(BIN)
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) /dev/null --taps

power: $(BIN)
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) /dev/null --power

fit: $(BIN)
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) /dev/null --fit

$(BUILD)/TamaPoke.gen.cpp: $(ROOT)/TamaPoke.ino protos.py
	@mkdir -p $(BUILD)
	python3 protos.py $< $@

$(BIN): $(BUILD)/TamaPoke.gen.cpp $(SOURCES) $(DEPS)
	@mkdir -p $(BUILD)
	@# -fpermissive turns into warnings errors that the firmware compiler does raise (e.g. a "return;" in a
	@# bool function); those in our own files are treated as errors
	@$(CXX) $(CXXFLAGS_ALL) -o $@ $(SOURCES) 2> $(BUILD)/cc.log; st=$$?; cat $(BUILD)/cc.log; \
	  if grep -E '^(\.\./\.\./[A-Za-z_]+\.(ino|cpp|h)|[A-Za-z_./]*TamaPoke\.gen\.cpp).*\[-fpermissive\]' $(BUILD)/cc.log; then \
	    echo "error: construccion permisiva en nuestro codigo (el compilador del firmware lo rechaza)"; rm -f $@; exit 1; fi; exit $$st

clean:
	rm -rf $(BUILD)
