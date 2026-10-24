# Renderer del host: compila TamaPoke.ino contra un lienzo por software.
#   make shots               -> build/shots/*.ppm (ingles, 466x466)
#   make shots FILTER=main_day  -> solo esa pantalla
#   make shots LANG_ID=6     -> otro idioma (0 ES, 1 EN, 2 FR, 3 DE, 4 IT, 5 PT, 6 JA, 7 KO)
# Compila el codigo grafico de la libreria GFX Library for Arduino y saca las fuentes
# CJK de la libreria U8g2 (GFX_FONT_FILE / U8G2_FONTS si no estan en ~/Arduino).
CXX      ?= g++
GFX_FONT_FILE ?= $(firstword $(wildcard $(HOME)/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h \
                  $(HOME)/Documents/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h))
LANG_ID  ?= 1
SHOTS    ?= $(BUILD)/shots
SD_DIR   ?= tools/sdcard
GFX_SRC  := $(patsubst %/font/glcdfont.h,%,$(GFX_FONT_FILE))
U8G2_FONTS ?= $(firstword $(wildcard $(HOME)/Arduino/libraries/U8g2/src/clib/u8g2_fonts.c \
                  $(HOME)/Documents/Arduino/libraries/U8g2/src/clib/u8g2_fonts.c))
ifneq ($(MAKECMDGOALS),clean)
ifeq ($(GFX_FONT_FILE),)
$(error falta la libreria GFX Library for Arduino: instalala o pasa GFX_FONT_FILE=.../src/font/glcdfont.h)
endif
ifeq ($(U8G2_FONTS),)
$(error falta la libreria U8g2: instalala o pasa U8G2_FONTS=.../src/clib/u8g2_fonts.c)
endif
endif
ROOT     := ../..
BUILD    := build
BIN      := $(BUILD)/render
OPT      ?= -O1 -g
CXXFLAGS_ALL := -std=c++17 $(OPT) -Wno-narrowing -Wno-write-strings -fpermissive \
                -ffp-contract=off -I shim -I ../shim -I $(ROOT) -I $(BUILD) -I $(GFX_SRC) $(CXXFLAGS)
GFX_SOURCES := $(GFX_SRC)/Arduino_GFX.cpp $(GFX_SRC)/Arduino_G.cpp $(GFX_SRC)/Arduino_DataBus.cpp \
               $(GFX_SRC)/canvas/Arduino_Canvas.cpp
SOURCES  := render_main.cpp host.cpp ../shim/shim.cpp $(ROOT)/pet.cpp $(ROOT)/i18n.cpp $(ROOT)/sdmon.cpp \
            $(GFX_SOURCES) $(BUILD)/cjk_fonts.cpp
DEPS     := $(wildcard $(ROOT)/*.h) $(wildcard shim/*) $(wildcard ../shim/*) $(GFX_FONT_FILE) \
            $(wildcard $(GFX_SRC)/*.h) $(wildcard $(GFX_SRC)/*/*.h) $(BUILD)/flags

# la receta depende de los flags y la fuente: si cambian, se recompila
$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CXX) $(CXXFLAGS_ALL)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS_ALL)' > $@
FORCE:

.PHONY: shots clean
shots: $(BIN)
	@mkdir -p $(SHOTS) && find $(SHOTS) -maxdepth 1 -type f -delete
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) $(abspath $(SHOTS)) $(LANG_ID) $(FILTER)

$(BUILD)/TamaPoke.gen.cpp: $(ROOT)/TamaPoke.ino protos.py
	@mkdir -p $(BUILD)
	python3 protos.py $< $@

$(BUILD)/cjk_fonts.cpp: fonts.py $(U8G2_FONTS)
	@mkdir -p $(BUILD)
	python3 fonts.py $(U8G2_FONTS) $@

$(BIN): $(BUILD)/TamaPoke.gen.cpp $(SOURCES) $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS_ALL) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD)
