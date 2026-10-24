# Tests of the firmware logic on the PC (no board).
#   make        -> build and run
#   make FILTER=evolve  -> only the tests whose name contains "evolve"
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
            -fno-omit-frame-pointer
LDFLAGS  ?=
SRC_DIR  := ..
BUILD    := build
BIN      := $(BUILD)/tamapoke_tests

SOURCES := main.cpp shim/shim.cpp test_pet.cpp test_dex.cpp test_i18n.cpp test_layout.cpp test_blit.cpp test_power.cpp test_nearby.cpp test_batcurve.cpp test_steps.cpp test_chirp.cpp test_thumbs.cpp test_clockset.cpp \
           $(SRC_DIR)/pet.cpp $(SRC_DIR)/i18n.cpp $(SRC_DIR)/species_chirp.cpp

.PHONY: all run clean asan
all: run

$(BIN): $(SOURCES) framework.h shim/Arduino.h shim/Preferences.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I shim -o $@ $(SOURCES) $(LDFLAGS)

run: $(BIN)
	@./$(BIN) $(FILTER)

# same suite under AddressSanitizer + UBSan: catches overflows and bad indices
asan: CXXFLAGS += -fsanitize=address,undefined -fno-sanitize-recover=all
asan: BIN := $(BUILD)/tamapoke_tests_asan
asan:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I shim -o $(BUILD)/tamapoke_tests_asan $(SOURCES) $(LDFLAGS)
	@./$(BUILD)/tamapoke_tests_asan $(FILTER)

clean:
	rm -rf $(BUILD)
