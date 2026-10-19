# Hourglass

An hourglass (sand timer) firmware for the **M5Stack Cardputer-ADV**
(ESP32-S3FN8, 8 MB flash, 240x135 IPS display, 56-key keyboard, BMI270 IMU,
ES8311 codec). Putting the device on its side starts the countdown, turning it
over reverses it, and laying it flat pauses it - like a real hourglass.

## Status: stage 3 - falling sand

The app runs: menu and settings, NVS, timer state machine, IMU orientation, the
completion chime, and the falling-sand animation. The sand is a cellular
automaton (`src/sand.h`): the neck is a valve that drips the load over exactly
the interval, and a flip rotates the grid instead of rebuilding the scene, so
the picture never jumps. Only the settings preview keeps the proportional fill.

### Modules

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | wiring: samples keyboard and IMU, feeds the state machine and the simulation, decides when to redraw, plays the chime |
| `src/app_state.*` | `AppState`, the transitions, sand progress integrated from millisecond deltas |
| `src/orientation.*` | accelerometer -> side / flat / unstable, filtered and debounced |
| `src/settings.*` | NVS namespace `hourglass`: `interval_idx`, `color_idx`, `sound_on` |
| `src/ui_menu.*` | menu and settings screens (landscape) |
| `src/hourglass_view.*` | ARMED / RUNNING / PAUSED / DONE / FINISH (portrait rotated frame) |
| `src/sand.*` | falling-sand simulation: platform independent, host-tested |
| `src/vessel.h` | the shape of the glass, shared by the drawing and the grid |
| `src/canvas.*` | one sprite per frame, each pushed in a single transfer |
| `src/palette.h` | the eight preset colours and the fixed UI colours |
| `src/input.*` | key press events |
| `src/log.*` | serial log format, uptime prefix |

### Screens and controls

There are no arrow or Esc keys on this keyboard. The arrows are `;` `.` `,` `/`:
the glyphs are on the Fn layer, but the character of a key is the same with and
without Fn, so the plain key does the same thing and navigation never depends on
Fn. `Del` is back/cancel - it sits where Esc is on a full keyboard. `Enter` and
`Opt` are OK.

**MENU** - the intervals 1/2/5/10/20/25/30 min, then `Settings`. The interval the
timer will use is gold with a marker, the row under the cursor is filled. `1`-`7`
select an interval directly, arrows move, `Enter` starts from an interval row or
opens the settings, `s` opens the settings.

**SETTINGS** - a grid of eight colour swatches (the active one framed, the cursor
in accent), a preview hourglass in the chosen colour and a `Sound` switch.
Arrows move; moving onto a swatch selects it; `Enter`, or left/right on the sound
row, toggles the sound. Each change is written to NVS immediately. `Del` goes
back to the menu.

**ARMED** (landscape) - the selected interval, `Place on side to start`, and the
debug hint.

**RUNNING** (portrait) - geometric hourglass, `mm:ss` and the sand level;
**PAUSED** is the same screen with a `Paused` label; **DONE** shows the full lower
bulb, `Flip to restart`, and plays a two-tone completion signal (A5, then a
longer D6) through `Speaker::tone()` when the sound is on.

### Timer model

`progress` in `[0, 1]` is the share of the sand in the lower bulb, integrated from
millisecond deltas in `app_state.cpp`, so the countdown does not depend on the
frame rate. The side the device is put down on when a cycle starts becomes the
direction of that cycle: that side makes progress grow, the opposite side makes it
fall (sand and clock run backwards), lying flat freezes it. Progress reaching 1
ends the cycle (DONE, signal); falling back to 0 returns to ARMED. Flipping a
finished hourglass to the other side starts a new cycle, and so does `Enter`.

### Orientation

Measured on the device with the stage 1 bring-up screen, accelerometer in g
(M5Unified applies no axis remap for the Cardputer-ADV):

| Pose | x | y | z |
| --- | --- | --- | --- |
| lying, screen up | -0.02 | +0.00 | +1.00 |
| standing on the left edge, USB-C at the bottom | -1.02 | -0.01 | +0.03 |
| standing on the right edge, USB-C at the top | +0.99 | -0.01 | -0.03 |

So `z` is the screen normal, `x` is the sand axis (the 240 px side of the
picture) and `y` stays near zero in every useful pose. Screen-down is inferred by
symmetry and still has to be confirmed on the device.

Thresholds, all in `src/orientation.cpp`: a pose counts when its axis reads more
than `0.80 g` and the other two stay below `0.45 g`; samples are smoothed with an
80 ms time constant; a pose is reported only after holding for 400 ms; while the
gyroscope reads more than 40 dps in total the stability timer restarts, so a flip
is not accepted halfway through. Standing on a long edge has no sand axis and is
reported as `Unstable`, which stops the sand like a flat pose does.

### Debug paths (no IMU needed)

- `Enter` in ARMED starts the countdown without waiting for a side.
- In RUNNING/PAUSED the up/down keys move progress by 10 % per press, which walks
  the whole flow - running, done, beep, restart - on a flat device.

### Serial log

```
Hourglass stage 3
board    : M5Cardputer-ADV
display  : 240x135 landscape, 135x240 portrait
keyboard : TCA8418 (ADV)
[hg     120] imu      : enabled
[hg     121] sprites  : landscape + portrait
[hg     123] heap     : 236195 bytes free
[hg     125] settings : interval 10 min, color 0 (Classic), sound on
[hg     130] nvs: interval_idx=3 color_idx=0 sound_on=1
[hg     142] state: Menu -> Armed (start pressed)
[hg     331] orientation: Unstable -> FlatUp (accel -0.01 +0.00 +1.00)
[hg     512] key: Enter (fn 0 shift 0 ctrl 0 opt 0 alt 0)
[hg    1090] imu: accel -0.02 +0.00 +1.00  (filtered -0.01 +0.00 +1.00)  pose FlatUp
```

Every line starts with the uptime in milliseconds. State changes, orientation
changes, key presses and NVS writes are logged; the raw accelerometer trace runs
once per second and is what the axis table above was measured with.

## Building

Requirements: PlatformIO Core 6 (`pio`) and a network connection for the first
build, which downloads the ESP32-S3 toolchain (a few hundred MB, several
minutes).

```sh
pio run                 # builds the default env: cardputer-adv
```

The image ends up in `.pio/build/cardputer-adv/firmware.bin`.

Pinned versions, all set in `platformio.ini` - they are the result of
bisecting real device panics, not defaults:

| Component | Version |
| --- | --- |
| Platform `espressif32` | 6.13.0 (Arduino core 2.0.17) |
| Board | `m5stack-stamps3`, with `ARDUINO_M5STACK_CARDPUTER` and USB CDC flags |
| `m5stack/M5Cardputer` | 1.1.1 |
| `m5stack/M5Unified` | 0.2.20 |
| `m5stack/M5GFX` | 0.2.27 |

The Cardputer-ADV board manifest only exists in `espressif32` >= 8.0.0 - the
generation whose speaker stack resets the board on every audio call (see
Troubleshooting) - so this build uses the StampS3 the device is made from,
with the `ARDUINO_M5STACK_CARDPUTER` marker and CDC-on-boot flags set
manually, the way the shipped AdvanceOS apps configure it. `M5Cardputer`
1.1.x needs `M5Unified`/`M5GFX` recent enough to detect `board_M5CardputerADV`
(that detection is what picks the TCA8418 keyboard reader); newer `M5Unified`
(0.2.22+) reworks the speaker task and panics on this board, which is why the
pair is pinned at 0.2.20 / 0.2.27 and must only move together.

Current image (stage 3):

```
RAM:   [=         ]  12.2% (used  39948 bytes from 327680 bytes)
Flash: [====      ]  37.0% (used 508581 bytes from 1376256 bytes)
firmware.bin: 508944 bytes
```

## Installing on the device

The Cardputer-ADV keeps **Launcher v2.6.x** installed, and Hourglass is
installed as a *Launcher app*, not as a flash-over-USB firmware. Flashing with
esptool/PIO would overwrite Launcher, so:

> **Do not run `pio run -t upload`, `pio run -t uploadfs` or `esptool` against
> this device.** `scripts/guard_usb_flash.py` makes those targets fail on
> purpose.

Install steps:

1. `pio run` and take `.pio/build/cardputer-adv/firmware.bin`.
2. Copy that file to the microSD card (folder of your choice, e.g. `/apps`).
3. Put the card into the device (FAT32, MBR, 32 GB or less) and boot into
   Launcher.
4. Open the SD-card file menu, select `firmware.bin` and install it.
5. Reboot: the installed Hourglass app starts by itself. To go back into
   Launcher, press **Enter** (`Sel`) on the boot screen instead of letting the
   app start.

### Size budget

Launcher writes an installed app into a single slot at `0x10000` of
`0x150000` bytes (its `custom_8Mb.csv`, mirrored in `partitions.csv`).
`board_upload.maximum_size` is set to that size, so `pio run` fails instead of
producing a binary that cannot be installed. Stage 3 uses 508581 of 1376256
bytes, leaving ~850 kB.

## Serial monitor

```sh
pio device monitor --baud 115200
```

USB CDC is enabled (`-DARDUINO_USB_CDC_ON_BOOT=1` is set in `platformio.ini`,
because the StampS3 manifest does not enable it), so the log
appears on the USB-C port the device is already connected through.

## Project layout

```
platformio.ini              build configuration (pinned platform/libraries)
partitions.csv              Launcher app-slot layout, used for the size limit
scripts/guard_usb_flash.py  blocks upload/erase targets
src/                        application modules, see the table above
tests/                      host tests for the sand simulation (g++, no
                            Arduino): g++ -std=c++17 -O2 -I src
                            tests/sand_test.cpp src/sand.cpp
```

## Troubleshooting

- **`Board : unknown` on the screen** - M5GFX did not recognise the hardware,
  and the keyboard would then not initialise either. Check the library
  versions above.
- **`IMU : not detected`** - `M5.Imu.isEnabled()` is false; the BMI270 is
  expected at I2C address 0x68/0x69 on the internal bus (SDA 8, SCL 9).
- **No keyboard events** - the ADV keyboard sits behind a TCA8418 at 0x34 on
  the same bus, with GPIO11 as interrupt line.
- **Reset at the completion chime (menu `last boot: panic`)** - a newer
  speaker stack has made it back into the build. The pins in
  `platformio.ini` (espressif32 6.13.0, M5Unified 0.2.20, M5GFX 0.2.27) were
  found by bisecting exactly this - the ES8311 reset family of
  espressif/esp-idf#18621 - so restore the pins first. The throwaway probe
  firmware that had bisected this was removed once the sound was confirmed
  on the device; it is in the git history around the `9da7d92` commit if
  another on-device audio bench is ever needed.

## Check on the device

After installing the build:

1. Interval, colour and sound survive a power cycle (`nvs:` lines in the log).
2. Lying flat, the menu and ARMED are landscape and readable.
3. Stand it on the left edge (USB-C at the bottom): ARMED must change to RUNNING
   within about half a second and the picture must be upright. If it is upside
   down, swap `kRotationSideA` / `kRotationSideB` in `src/hourglass_view.cpp`.
4. The sand must run downwards in the drawn picture on both sides. If it runs
   upwards, the frame rotation of that side is the wrong way round (same swap).
5. `mm:ss` counts down and the lower bulb fills; laying it flat shows `Paused` and
   freezes both. Putting it back on the same edge resumes.
6. Flipping to the other edge during the countdown makes the level recede and the
   clock count up; at the empty bulb the app shows FINISH (the same picture,
   "Flip to start") instead of switching screens, and flipping again starts a
   new cycle.
7. Let it finish (or walk it with the up key): the two-tone signal sounds and
   DONE shows; flipping to the other edge starts a new cycle. Enter restarts from
   DONE and FINISH too.
8. `Del` from RUNNING returns to the menu.
9. The menu carries a `last boot:` line with the reset reason (`power-on`,
   `brownout`, `panic`, `task watchdog`). After any unexpected restart, that
   line is the first thing to read; the same reason is logged in serial.
10. In the settings, switching Sound from Off to On plays the signal at once -
   a two-second way to check the audio path without waiting out an interval.

## Next stages

- **Later:** a way back into the Launcher from the app (that needs the Launcher's
  boot-entry convention), and dropping the raw IMU trace once the axis table is
  confirmed on the device.
- **Improvements (user backlog):** flat-pose physics - while the hourglass lies
  flat, the grains inside each bulb settle towards the physically lower wall of
  their own bulb, like a real hourglass lying on its side; nothing crosses the
  neck, the timer stays paused.
- **Improvements (user backlog):** a setting that shows or hides the mm:ss
  readout during the countdown, default on.
- **Open: a softer completion signal.** The shipped signal is the two-tone
  `Speaker::tone()` call (A5, then a longer D6), confirmed playing on the
  device. What made it possible at all is the toolchain pin: on
  arduino-esp32 3.x every speaker call reset the board (see
  Troubleshooting); on the pinned 2.0.17 generation the audio is proven on
  this hardware, and the speaker is started once at boot the way AdvanceOS
  does it - no `end()` dance. The soft PCM chime (`Speaker::playRaw()`,
  sine with fades) is still parked: it stayed silent at low levels and
  reset at high ones. The audio probe that bisected the panics was removed
  after the confirmation; resurrect it from git history (`9da7d92`) as the
  bench for the next round of experiments.
