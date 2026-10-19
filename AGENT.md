# AGENT.md

Guidance for coding agents. The detailed, human-facing reference is
`README.md` - this file only orients you and lists the rules that are easy
to break while looking clever.

## What this is

Hourglass - a sand-timer app for the **M5Stack Cardputer-ADV** (ESP32-S3,
8 MB flash, 240x135 display, TCA8418 keyboard, BMI270 IMU, ES8311 codec).
Stage 3, feature-complete: menu/settings, NVS, timer state machine,
orientation, falling-sand animation, completion chime. There is no
Launcher-less firmware mode: the app ships as a **Launcher app**, not as a
USB-flashed firmware.

## Hard rules

1. **Never flash this device over USB.** `pio run -t upload`, `-t uploadfs`,
   `-t erase` and `esptool` would overwrite the installed Launcher.
   `scripts/guard_usb_flash.py` makes those targets fail on purpose - do not
   bypass or neuter it. Building produces the `.bin`; install goes through
   the Launcher SD-card menu (steps in README).
2. **Respect the image size budget.** Launcher installs into one slot at
   `0x10000`, `0x150000` bytes long (`partitions.csv`); `board_upload.maximum_size`
   is set to that, so an oversized build fails the build. Do not raise the
   limit or diverge `partitions.csv` from the Launcher layout.
3. **The version pins in `platformio.ini` are load-bearing.** They are the
   result of bisecting real panics on device:
   - `espressif32@8.0.0` (arduino-esp32 3.x) makes *every* speaker call reset
     the board (ESP_RST_PANIC, ES8311 regression family of
     espressif/esp-idf#18621). The build uses `espressif32@6.13.0`
     (arduino-esp32 2.0.17, the AdvanceOS generation) with
     `board = m5stack-stamps3` + `-DARDUINO_USB_CDC_ON_BOOT=1`
     `-DARDUINO_M5STACK_CARDPUTER`, because the Cardputer-ADV board manifest
     only exists in newer platform packages.
   - `M5Unified` 0.2.22+ reworked the speaker task and also panics; the pinned
     0.2.20 + `M5GFX` 0.2.27 are the pair that both knows
     `board_M5CardputerADV` (which is what selects the TCA8418 keyboard
     reader) and plays audio on this hardware. `M5Cardputer` 1.1.1 sits on top.
   Bumping any of these versions silently reintroduces the panic. If audio
   breaks after a dependency change, the versions are the first suspect.
4. **Code must compile under that old-generation toolchain.** arduino-esp32
   2.x builds the device as gnu++11 with GCC 8.4, so: constexpr functions
   must be a single `return` statement (`src/palette.h` `dimmed()` is the
   pattern), and the USB CDC `Serial` (`HWCDC`) has no `vprintf` - format
   with `vsnprintf()` into a buffer first (`src/log.cpp`). The host tests use
   `-std=c++17`; device code has to stay gnu++11-clean.

## Commands

```sh
pio run            # single environment: cardputer-adv (the app)
pio device monitor --baud 115200   # USB CDC is enabled on boot
```

Artifact: `.pio/build/cardputer-adv/firmware.bin` - install it into the
device from the Launcher SD menu; never flash it over USB.

Host tests (the sand simulation and the state machine are plain C++; the
state-machine test stubs the pose API and the device log - see its file
header):

```sh
g++ -std=c++17 -O2 -I src tests/sand_test.cpp src/sand.cpp \
    -o sand_test && ./sand_test
g++ -std=c++17 -O2 -I src tests/app_state_test.cpp src/app_state.cpp \
    -o app_state_test && ./app_state_test
```

## Architecture in one breath

`main.cpp` samples keyboard (`input.*`) and IMU (`orientation.*`), feeds
`app_state.*` (Menu -> Armed -> Running/Paused -> Done/Finish transitions,
`progress` in [0,1] integrated from millisecond deltas so the countdown is
frame-rate independent) and `sand.*` (cellular automaton, neck is the drip
valve), and pushes one sprite per frame (`canvas.*`) drawn by
`hourglass_view.*` / `ui_menu.*`. Settings live in NVS namespace `hourglass`
(`settings.*`). `vessel.h` is the glass shape shared by the drawing and the
grid; `palette.h` holds UI colours + eight sand presets.

Facts that keep code honest:

- `src/sand.*` is platform-independent and host-tested - keep it free of
  Arduino includes. Anything device-specific stays in `main.cpp` or in a
  module file pair.
- Flip = rotate the grid, not a rebuild, so the picture never jumps. A flip
  mid-run reverses progress; flat pauses. Don't restructure this per-pose.
- Keyboard has no arrows or Esc: arrows are `;` `.` `,` `/` (same character
  with/without Fn - navigation must never require Fn), `Del` is back/cancel,
  `Enter`/`Opt` are OK. Mapping lives in `src/input.cpp`.
- IMU axes (measured on device, see the table in README): `z` is the screen
  normal, `x` is the sand axis, `y` is never a useful pose axis. Thresholds,
  smoothing (80 ms), debouncing (400 ms) and the 40 dps flip guard live in
  `src/orientation.cpp` - they are measured, not invented, so don't tune them
  without the device. The screen-down pose is still **unconfirmed** on device.
- Audio: speaker is configured once at boot (AdvanceOS style:
  `sample_rate = 96000`, no `end()` dance; the rate is one constant,
  `kSpeakerSampleRate` in `main.cpp`, shared by the config and the chime -
  `playRaw()` takes the rate explicitly, so the two must agree). The
  completion chime is rendered once at boot (A5, then a longer D6, sines
  with fades, unsigned 8-bit, ~19% amplitude) and plays in one call on
  `Speaker::playRaw()` channel 0 - nothing is scheduled in the loop anymore.
  `Speaker::tone()` played the same figure and is the proven fallback:
  every `playRaw()` failure recorded so far (silent at low level, reset at
  high) happened under the espressif32@8.0.0 speaker stack. The chime's
  audition on the device is README checklist items 7 and 10; when it
  misbehaves, suspect the version pins first. The probe that bisected the
  original panic is in git history around commit `9da7d92`.

## Debugging without the IMU (flat on a desk)

- `Enter` in ARMED starts the countdown. In RUNNING/PAUSED the up/down keys
  move progress by 10 % per press - that walks run/done/beep/restart on a
  flat device.
- The serial log opens with the reset reason (`reset:` line: `power-on`,
  `brownout`, `panic`, `task watchdog`). After an unexpected restart that is
  the first thing to read - every log line is prefixed with uptime ms.
- If the screen shows `Board : unknown`, a lib version was changed (see Hard
  rules #3) or the build flags `ARDUINO_M5STACK_CARDPUTER` were dropped.