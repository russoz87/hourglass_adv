# AGENT.md

Guidance for coding agents. The detailed, human-facing reference is
`README.md` - this file only orients you and lists the rules that are easy
to break while looking clever.

## What this is

Hourglass - a sand-timer app for the **M5Stack Cardputer-ADV** (ESP32-S3,
8 MB flash, 240x135 display, TCA8418 keyboard, BMI270 IMU, ES8311 codec).
Stage 4: the stage-3 app (menu/settings, NVS, timer state machine,
orientation, falling-sand animation, chime) plus flat-pose physics - a flat
device settles its sand sideways, nothing crosses the neck. There is no
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
  normal, `x` is the sand axis, and `y` is the long-edge axis: resting on a
  long edge reads dominant `y` and reports `LongYUp`/`LongYDown` (the hourglass
  lying on its side - it gets the lateral settle, and the direction is
  device-verified: the pile runs to the wall the device rests on; the first
  right-hand-cross guess was inverted, which finally pinned the triad's y
  sign). Thresholds, smoothing (80 ms), debouncing (400 ms) and the 40 dps
  flip guard live in `src/orientation.cpp` - they are measured, not invented,
  so don't tune them without the device. The screen-down pose is still
  **unconfirmed** on device.
- Audio: speaker is configured once at boot (AdvanceOS style:
  `sample_rate = 96000`, no `end()` dance; the rate is one constant,
  `kSpeakerSampleRate` in `main.cpp`, shared by the config and the chime -
  `playRaw()` takes the rate explicitly, so the two must agree). The
  completion chime is rendered once at boot (A5, then a longer D6, sines
  with fades, unsigned 8-bit, ~19% amplitude) and plays in one call on
  `Speaker::playRaw()` channel 0 - confirmed on the device, nothing is
  scheduled in the loop. Every `playRaw()` failure recorded before that
  (silent at low level, reset at high) happened under the espressif32@8.0.0
  speaker stack; when audio misbehaves, suspect the version pins first.
  `Speaker::tone()` in the git history is the proven fallback, and the probe
  that bisected the panic is around commit `9da7d92`.
- Flat poses (`Paused`) run a lateral settle: the automaton's fall turns 90
  degrees across the picture (`sand::Gravity::Left`/`::Right`), the valve and
  the governor stand still, and a lateral step never leaves its grid row -
  that row is what keeps the bulbs separated, so nothing crosses the neck.
  The screen-up-to-left / screen-down-to-right mapping is a documented
  convention: a flat pose cannot see which side wall is physically lower.
  The settle gate is direction aware (`settled(gravity)`): a pile that went
  still under the standing physics re-arms when the gravity switches, which
  is what gets the lying pile moving; a paused hourglass drops the valve's
  leftover tokens, so the only level change a pause may still make is the
  governor reconciling a grain or two the clock had already counted.
  Repeatability rests on the repose rule in `tryFall`: a grain resting on a
  slope steeper than 45 degrees rolls off (a side column two or more levels
  lower takes it). Without it the wall-hugging pile of the previous slide
  would keep its shape through the standing phases and the next lie-down
  would have nothing to slide. The lateral spill diagonals are forbidden at
  the neck line (checked by cell counts in the test, not by the counters -
  the counters belong to the valve and the governor and never see a lateral
  step), so nothing runs bulb to bulb while lying.

## Debugging without the IMU (flat on a desk)

- In RUNNING/PAUSED the up/down keys move progress by 10 % per press - that
  walks run/done/beep/restart on a flat device. A cycle starts from a real
  placement only; start one on an edge, then lay the device flat and walk it.
- The serial log opens with the reset reason (`reset:` line: `power-on`,
  `brownout`, `panic`, `task watchdog`). After an unexpected restart that is
  the first thing to read - every log line is prefixed with uptime ms.
- If the screen shows `Board : unknown`, a lib version was changed (see Hard
  rules #3) or the build flags `ARDUINO_M5STACK_CARDPUTER` were dropped.