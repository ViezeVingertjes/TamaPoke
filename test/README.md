# Tests

A test suite that runs **on the PC, without a board**: it compiles the pure logic of
the firmware (`pet.cpp`, `i18n.cpp`, `dex.h`) against Arduino shims and runs it
with a fake clock and a fake `random()`, so results are deterministic and take
less than a second.

```bash
./test/run_tests.sh           # everything
./test/run_tests.sh --asan    # also with AddressSanitizer + UBSan
```

Or in parts:

```bash
cd test && make               # firmware logic only
cd test && make FILTER=evolve # only the tests with "evolve" in the name
cd test && make asan          # with sanitizers
python3 test/test_tools.py    # only the tools/ scripts
```

## What is covered

| File | What it checks |
|---|---|
| `test_pet.cpp` | egg and starter choice, food, stats tick, sleep, neglect, levels, evolution (including the Eevee branch), combat stats, training and minigame, streak and bond, medals, ceremonies, offline progression and saving to NVS |
| `test_dex.cpp` | Pokedex integrity: ASCII names, evolution chains without loops, every species reachable, no line impossible to complete, UI buffer limits |
| `test_i18n.cpp` | all strings present and in ASCII (the GFX font is), and with the same `%u`/`%s` the sketch passes in each call |
| `test_tools.py` | the `tools/` scripts compile, the Pokedex data is consistent and `dex.h` still matches what `tools/gen_dex.py` generates |

## How it is set up

- `shim/Arduino.h` + `shim/Preferences.h` + `shim/shim.cpp`: the bare minimum of Arduino
  that `pet.cpp` and `i18n.cpp` use. The clock (`millis()`) and `random()` are fake
  and controllable; `Preferences` is an in-memory map that mimics the NVS, so
  a reboot can be simulated by creating another `Pet` and calling `begin()`.
- `framework.h` + `main.cpp`: own micro framework, no dependencies.
- Tests marked with `TEST_KNOWN_ISSUE` describe the **correct** behavior of
  something that fails today: they do not break the suite, they are listed
  separately, and they warn if they ever start passing (so the mark can be removed).

## What is NOT covered

Everything that needs the board: display and touch (`TamaPoke.ino`), audio
(`audio.cpp`), SD card (`sdmon.cpp`) and RTC/battery (`rtcbat.cpp`). The
`.ino` is not compiled here either: that takes `arduino-cli` with the ESP32 core.
