# Hourglass - the build entry points.
#
# The firmware itself is built by PlatformIO: it resolves the pinned M5
# libraries, the board flags and the Lander slot layout, and it checks the
# image size against the Launcher's app slot. Replacing it with a hand-written
# Makefile would only mean re-implementing all of that, so PlatformIO stays
# and this file is the convenient front door:
#
#   make            the firmware (build/hourglass.bin), the M5Burner image
#                   (build/hourglass-m5burner.bin) and the host tests
#   make firmware  just the firmware
#   make m5burner  the firmware plus the merged full-flash M5Burner image
#   make test       just the host tests (sand + state machine)
#   make clean      drop the build tree and the images
#
# The host tests are plain C++ (no Arduino), built with the same g++ the
# project has always used; their binaries land in build/ next to the image.

PIO       := pio
BUILD_DIR := build
HEADERS   := $(wildcard src/*.h)

.PHONY: all firmware m5burner test clean

all: firmware m5burner test

firmware:
	$(PIO) run

m5burner: firmware
	python3 scripts/merge_m5burner.py

test: $(BUILD_DIR)/sand_test $(BUILD_DIR)/app_state_test
	$(BUILD_DIR)/sand_test
	$(BUILD_DIR)/app_state_test

$(BUILD_DIR)/sand_test: tests/sand_test.cpp src/sand.cpp $(HEADERS) | $(BUILD_DIR)
	g++ -std=c++17 -O2 -I src tests/sand_test.cpp src/sand.cpp -o $@

$(BUILD_DIR)/app_state_test: tests/app_state_test.cpp src/app_state.cpp $(HEADERS) | $(BUILD_DIR)
	g++ -std=c++17 -O2 -I src tests/app_state_test.cpp src/app_state.cpp -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	$(PIO) run -t clean
	rm -rf $(BUILD_DIR)
