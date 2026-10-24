# Renderer del host: compila TamaPoke.ino contra un lienzo por software.
#   make shots               -> build/shots/*.ppm (ingles, 466x466)
#   make shots FILTER=main_day  -> solo esa pantalla
#   make shots LANG_ID=6     -> otro idioma (0 ES, 1 EN, 2 FR, 3 DE, 4 IT, 5 PT, 6 JA, 7 KO)
#   make check               -> compara con golden/<idioma> (falla si alguna pantalla cambia)
#   make golden              -> regenera golden/ (solo si el cambio de interfaz es intencionado)
#   make shots SIZE=240      -> otro tamano de lienzo (cuadrado, en pixeles)
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
                -ffp-contract=off $(if $(SIZE),-DLCD_WIDTH=$(SIZE) -DLCD_HEIGHT=$(SIZE)) -I shim -I ../shim -I $(ROOT) -I $(BUILD) -I $(GFX_SRC) $(CXXFLAGS)
GFX_SOURCES := $(GFX_SRC)/Arduino_GFX.cpp $(GFX_SRC)/Arduino_G.cpp $(GFX_SRC)/Arduino_DataBus.cpp \
               $(GFX_SRC)/canvas/Arduino_Canvas.cpp
SOURCES  := render_main.cpp host.cpp ../shim/shim.cpp $(ROOT)/pet.cpp $(ROOT)/i18n.cpp $(ROOT)/sdmon.cpp \
            $(GFX_SOURCES) $(BUILD)/cjk_fonts.cpp
DEPS     := $(wildcard $(ROOT)/*.h) $(wildcard shim/*) $(wildcard ../shim/*) $(GFX_FONT_FILE) \
            $(wildcard $(GFX_SRC)/*.h) $(wildcard $(GFX_SRC)/*/*.h) $(BUILD)/flags

# idiomas con imagenes de referencia (nombre:id); LANG_ID solo afecta a "make shots"
LANGS := en:1 es:0 de:3 ja:6 ko:7
# la receta depende de los flags y la fuente: si cambian, se recompila
$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CXX) $(CXXFLAGS_ALL)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS_ALL)' > $@
FORCE:

.PHONY: shots check golden clean
shots: $(BIN)
	@mkdir -p $(SHOTS) && find $(SHOTS) -maxdepth 1 -type f -delete
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) $(abspath $(SHOTS)) $(LANG_ID) $(FILTER)

# las referencias dependen de estas versiones: con otras, "check" fallaria en todo
# y "golden" las contaminaria (PIN_CHECK=0 salta la comprobacion)
GFX_VER := 1.6.9
U8G2_VER := 2.36.19
.PHONY: versions
versions:
	@[ "$(PIN_CHECK)" = 0 ] || { \
	  g=$$(sed -n 's/^version=//p' $(GFX_SRC)/../library.properties); \
	  u=$$(sed -n 's/^version=//p' $(dir $(U8G2_FONTS))../../library.properties); \
	  [ "$$g" = "$(GFX_VER)" ] && [ "$$u" = "$(U8G2_VER)" ] || { \
	    echo "las referencias son de GFX Library $(GFX_VER) y U8g2 $(U8G2_VER); aqui hay $$g y $$u (PIN_CHECK=0 para ignorarlo)"; exit 1; }; }

# renderiza todos los idiomas en build/check/<idioma> (aparte de build/shots)
CHECKDIR := $(BUILD)/check
.PHONY: renders nosize
nosize:
	@test -z "$(SIZE)" || { echo "SIZE solo vale para 'make shots': las referencias son de 466 px"; exit 1; }
renders: nosize $(BIN) versions
	@rm -rf $(CHECKDIR); for l in $(LANGS); do n=$${l%%:*}; \
	  $(MAKE) --no-print-directory shots LANG_ID=$${l##*:} SHOTS=$(CHECKDIR)/$$n >/dev/null || exit 1; done

check: renders
	@fail=0; for l in $(LANGS); do n=$${l%%:*}; \
	  echo "== $$n"; python3 compare.py $(CHECKDIR)/$$n golden/$$n || fail=1; done; exit $$fail

# solo escribe si todos los idiomas se han renderizado bien
golden: renders
	@for l in $(LANGS); do n=$${l%%:*}; \
	  python3 compare.py --update --prune $(CHECKDIR)/$$n golden/$$n || exit 1; done

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
