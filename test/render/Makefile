# Renderer del host: compila TamaPoke.ino contra un lienzo por software.
#   make shots               -> build/shots/*.ppm (ingles, 466x466)
#   make shots FILTER=main_day  -> solo esa pantalla
#   make taps                -> toca el centro de cada boton y dialogo (admite SIZE=)
#   make power               -> que pantallas se repintan y cuando (ahorro de bateria)
#   make fit                 -> cada texto real cabe en su caja o en el circulo
#   make check               -> compara con golden/en y golden/sq480 (falla si alguna pantalla cambia)
#   make golden              -> regenera golden/ (solo si el cambio de interfaz es intencionado)
#   make readme              -> docs/screens/*.png para el README (desde golden/)
#   make shots SIZE=240      -> otro tamano de lienzo (cuadrado, en pixeles)
#   make shots ROUND=0       -> panel cuadrado (menus a pantalla completa); por defecto, redondo
# Compila el codigo grafico de la libreria GFX Library for Arduino
# (GFX_FONT_FILE si no esta en ~/Arduino).
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
# cada variante (SIZE/ROUND) compila en su carpeta: asi check no recompila el mismo binario
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

# la receta depende de los flags y la fuente: si cambian, se recompila
$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CXX) $(CXXFLAGS_ALL)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS_ALL)' > $@
FORCE:

.PHONY: shots taps fit power check golden readme clean
shots: $(BIN)
	@mkdir -p $(SHOTS) && find $(SHOTS) -maxdepth 1 -type f -delete
	cd $(ROOT) && TAMAPOKE_SD=$(SD_DIR) $(abspath $(BIN)) $(abspath $(SHOTS)) $(FILTER)

# las referencias dependen de estas versiones: con otras, "check" fallaria en todo
# y "golden" las contaminaria (PIN_CHECK=0 salta la comprobacion)
GFX_VER := 1.6.9
.PHONY: versions
versions:
	@[ "$(PIN_CHECK)" = 0 ] || { \
	  g=$$(sed -n 's/^version=//p' $(GFX_SRC)/../library.properties); \
	  [ "$$g" = "$(GFX_VER)" ] || { \
	    echo "las referencias son de GFX Library $(GFX_VER); aqui hay $$g (PIN_CHECK=0 para ignorarlo)"; exit 1; }; }

# renderiza las dos variantes en build/check/{en,sq480} (aparte de build/shots)
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

# solo escribe si las dos variantes se han renderizado bien
golden: renders
	@python3 compare.py --update --prune $(CHECKDIR)/en golden/en || exit 1
	@python3 compare.py --update --prune $(CHECKDIR)/sq480 golden/sq480

# imagenes del README (docs/screens/), a partir de las referencias de golden/
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
	@# -fpermissive vuelve avisos errores que el compilador del firmware si da (p. ej. un "return;" en una
	@# funcion bool); los de nuestros ficheros se tratan como errores
	@$(CXX) $(CXXFLAGS_ALL) -o $@ $(SOURCES) 2> $(BUILD)/cc.log; st=$$?; cat $(BUILD)/cc.log; \
	  if grep -E '^(\.\./\.\./[A-Za-z_]+\.(ino|cpp|h)|[A-Za-z_./]*TamaPoke\.gen\.cpp).*\[-fpermissive\]' $(BUILD)/cc.log; then \
	    echo "error: construccion permisiva en nuestro codigo (el compilador del firmware lo rechaza)"; rm -f $@; exit 1; fi; exit $$st

clean:
	rm -rf $(BUILD)
