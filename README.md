# Hourglass

An hourglass (sand timer) firmware for the **M5Stack Cardputer-ADV**
(ESP32-S3FN8, 8 MB flash, 240x135 IPS display, 56-key keyboard, BMI270 IMU,
ES8311 codec). Putting the device on its side starts the countdown, turning it
over reverses it, and laying it flat pauses it - like a real hourglass.

## Status: stage 2 - application frame

The app runs: menu and settings, NVS, timer state machine, IMU orientation and the
completion beep. The falling-sand animation is **stage 3** and is not here yet: the
hourglass scene fills its bulbs proportionally to the timer progress.

### Modules

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | wiring: samples keyboard and IMU, feeds the state machine, decides when to redraw, plays the beeps |
| `src/app_state.*` | `AppState`, the transitions, sand progress integrated from millisecond deltas |
| `src/orientation.*` | accelerometer -> side / flat / unstable, filtered and debounced |
| `src/settings.*` | NVS namespace `hourglass`: `interval_idx`, `color_idx`, `sound_on` |
| `src/ui_menu.*` | menu and settings screens (landscape) |
| `src/hourglass_view.*` | ARMED / RUNNING / PAUSED / DONE (portrait rotated frame) |
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
bulb, `Flip to restart`, and plays two short 880 Hz beeps when the sound is on.

### Timer model

`progress` in `[0, 1]` is the share of the sand in the lower bulb, integrated from
millisecond deltas in `app_state.cpp`, so the countdown does not depend on the
frame rate. The side the device is put down on when a cycle starts becomes the
direction of that cycle: that side makes progress grow, the opposite side makes it
fall (sand and clock run backwards), lying flat freezes it. Progress reaching 1
ends the cycle (DONE, beep); falling back to 0 returns to ARMED. Flipping a
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
Hourglass stage 2
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

Pinned versions, all set in `platformio.ini`:

| Component | Version |
| --- | --- |
| Platform `espressif32` | 8.0.0 (Arduino core 3.3.12) |
| Board | `m5stack-cardputer` |
| `m5stack/M5Cardputer` | 1.1.1 |
| `m5stack/M5Unified` | 0.2.26 |
| `m5stack/M5GFX` | 0.2.33 |

`M5Cardputer` 1.1.x needs `M5Unified`/`M5GFX` recent enough to detect
`board_M5CardputerADV`; that detection is what picks the TCA8418 keyboard
reader, so these three libraries should be updated together.

Current image (stage 2):

```
RAM:   [=         ]   8.7% (used 28516 bytes from 327680 bytes)
Flash: [======    ]  59.6% (used 820263 bytes from 1376256 bytes)
firmware.bin: 854912 bytes
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
producing a binary that cannot be installed. Stage 2 uses 854912 of 1376256
bytes, leaving ~520 kB for the falling-sand simulation.

## Serial monitor

```sh
pio device monitor --baud 115200
```

USB CDC is enabled on the board (`DARDUINO_USB_CDC_ON_BOOT=1`), so the log
appears on the USB-C port the device is already connected through.

## Project layout

```
platformio.ini              build configuration (pinned platform/libraries)
partitions.csv              Launcher app-slot layout, used for the size limit
scripts/guard_usb_flash.py  blocks upload/erase targets
src/                        application modules, see the table above
```

## Troubleshooting

- **`Board : unknown` on the screen** - M5GFX did not recognise the hardware,
  and the keyboard would then not initialise either. Check the library
  versions above.
- **`IMU : not detected`** - `M5.Imu.isEnabled()` is false; the BMI270 is
  expected at I2C address 0x68/0x69 on the internal bus (SDA 8, SCL 9).
- **No keyboard events** - the ADV keyboard sits behind a TCA8418 at 0x34 on
  the same bus, with GPIO11 as interrupt line.

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
   clock count up; at the empty bulb the app returns to ARMED.
7. Let it finish (or walk it with the up key): two beeps and DONE; flipping to
   the other edge starts a new cycle.
8. `Del` from RUNNING returns to the menu.

## Next stages

- **Stage 3:** falling-sand cellular automaton in the portrait frame, in one
  colour, with the real stream through the neck. It replaces the two bulb fills
  in `drawHourglass()` (`src/hourglass_view.cpp`); `progress` already gives it the
  level and `session.flow` the direction.
- **Later:** a way back into the Launcher from the app (that needs the Launcher's
  boot-entry convention), and dropping the raw IMU trace once the axis table is
  confirmed on the device.
