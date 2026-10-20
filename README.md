# Hourglass

An hourglass (sand timer) firmware for the **M5Stack Cardputer-ADV**
(ESP32-S3FN8, 8 MB flash, 240x135 IPS display, 56-key keyboard, BMI270 IMU,
ES8311 codec). Putting the device on its side starts the countdown, turning it
over reverses it, and laying it flat pauses it - like a real hourglass.

## Status: stage 4 - flat-pose physics

The app runs: menu and settings (including the `Sound` and `Clock` switches), NVS,
timer state machine, IMU orientation, the soft rendered completion chime, and the
falling-sand animation. The vessel itself is the classic rounded-bulb hourglass
(`src/vessel.h`: quarter-ellipse walls flaring out of a short neck pipe); the whole
shape is one function (`halfWidthAt`), which the drawing, the grid mask and the
preview all share. The sand is a cellular
automaton (`src/sand.h`): the neck is a valve that drips the load over exactly
the interval, a flip rotates the grid instead of rebuilding the scene, so
the picture never jumps, and a flat pose - a paused clock - settles the sand
sideways toward the lower wall of each bulb. Only the settings preview keeps the
proportional fill.

### Modules

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | wiring: samples keyboard and IMU, feeds the state machine and the simulation, decides when to redraw, plays the chime |
| `src/app_state.*` | `AppState`, the transitions, sand progress integrated from millisecond deltas |
| `src/orientation.*` | accelerometer -> side / flat / unstable, filtered and debounced |
| `src/settings.*` | NVS namespace `hourglass`: `interval_idx`, `color_idx`, `sound_on`, `clock_on` |
| `src/ui_menu.*` | menu and settings screens (landscape) |
| `src/hourglass_view.*` | ARMED / RUNNING / PAUSED / DONE / FINISH (portrait rotated frame) |
| `src/sand.*` | falling-sand simulation: platform independent, host-tested; vertical flow and the lateral flat-settle |
| `src/vessel.h` | the shape of the glass, shared by the drawing and the grid |
| `src/canvas.*` | one sprite per frame, each pushed in a single transfer |
| `src/palette.h` | the eight preset colours, and the fixed theme colours: UI + the glass (classic walls, copper plates) |
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
in accent), a preview hourglass in the chosen colour and a two `Sound`/`Clock`
switches. Arrows move; moving onto a swatch selects it; `Enter`, or left/right
on a switch row, toggles it. `Sound` turns the completion chime on or off,
`Clock` shows or hides the `mm:ss` readout during the countdown (hidden, the
interval label stands in its place). Each change is written to NVS immediately.
`Del` goes back to the menu.

**ARMED** (landscape) - the selected interval and `Place on side to start`;
`Del` goes back to the menu, and a real placement is the only way a countdown
begins.

**RUNNING** (portrait) - geometric hourglass, `mm:ss` (unless the Clock setting
hides it) and the sand level; **PAUSED** is the same screen with a `Paused`
label; **DONE** shows the full lower
bulb, `Flip to restart`, and plays the soft chime - A5, then a longer D6,
rendered as sines with fades into one PCM buffer played through
`Speaker::playRaw()` - when the sound is on.

### Timer model

`progress` in `[0, 1]` is the share of the sand in the lower bulb, integrated from
millisecond deltas in `app_state.cpp`, so the countdown does not depend on the
frame rate. The side the device is put down on when a cycle starts becomes the
direction of that cycle: that side makes progress grow, the opposite side makes it
fall (sand and clock run backwards), lying flat freezes it. Progress reaching 1
ends the cycle (DONE, signal); falling back to 0 returns to ARMED. Flipping a
finished hourglass to the other side starts a new cycle, and so does `Enter`.

While the device lies flat, the frozen sand gets its own physics: the picture
keeps its frame, the gravity a real lying hourglass sees runs across it, and
the grains of each bulb slide toward the lower wall of their own bulb - the
automaton runs its usual straight-then-two-diagonals fall, just 90 degrees
sideways, with the valve and the governor standing still. Screen up settles
the pile to the left wall, screen down to the right one; which of the two
walls is physically lower cannot be read from a flat pose (the gravity there
points straight into the screen), so the mapping is a documented convention. A
lateral step never leaves its grid row, and the row is the boundary between
the bulbs, so nothing crosses the neck while lying. Standing the device back
on an edge returns to the standing physics and the clock.

The long-edge rest gets the same lying physics - that is the pose where the
glass truly lies on its side. The gravity now has a real in-picture direction:
the picture's horizontal axis is the device y axis, so the pose sign plus the
frame tell which picture wall is lower (`LongYUp` slides left in a SideA
frame and right in a SideB frame, `LongYDown` the other way round). Direction
confirmed on the device: the sand runs to the wall the device rests on. The
first mapping was derived by a right-hand cross from the x/z axis table and
came out inverted on the hardware - the triad's y sign had never been
measured (`y` stayed ~0 in every earlier pose), which is exactly what this
check pinned down.

One repose rule makes the settle repeatable: a grain whose straight and
diagonal falls are blocked but whose side column is two or more levels lower
rolls off the slope (a steeper-than-45-degrees pile cannot hold; a one-level
terrace is stable, so the stream's mounds keep their shape). This is what
heals the wall-hugging pile of the previous slide back toward the center
during the standing phases - only the narrowing funnel right at the neck
keeps a lean, the glass itself stops the grains there. Without it, the second
lie-down would find the pile already at the wall and nothing to move - the
slide would have happened once and never again.

**The Clock switch is two physics modes.** On (default, the timed mode) the
neck line is sealed: nothing crosses it sideways, the governor holds the level
against the timer, and DONE and the chime come from the countdown reaching its
end - which is what the visible `mm:ss` has to agree with. Off (the analog
mode, no digits are shown anyway) the hourglass rules itself: a lying pile
keeps leaking through the neck the way real glass drains from a lying bottle,
the grains that slide across are counted, and the level drifts ahead of the
frozen clock at will. DONE and the chime then come when the last grain
actually lands below the throat - even mid-pause, with the device still lying
on its side - and a fully rewound glass with an empty throat is a real FINISH.
The governor rests in the analog mode: nothing drags the sand back into line,
because the sand is the line.

The lateral diagonals never carry a grain across the neck line: a spill step
to the adjacent row is forbidden when the two rows sit on opposite sides of
the throat, so the bulbs stay separated whatever the pose does. (The first
version of this rule read the maintained counters, which the lateral steps do
not touch, and a lying pile leaked ~280 grains through the neck per settle -
the "several seconds" desync at unpausing - before the check moved to direct
cell counts.)

The idle animation gate is direction aware: the pause first runs the standing
physics until the stream has landed (the flat pose is reported only after the
pose hold time), and then the gravity change re-arms the settle, so the lying
pile always gets its slide - it never freezes before that. A paused hourglass
also drains nothing across the neck: the valve's leftover tokens are dropped,
so the only level change a pause may still do is the governor reconciling the
one or two grains the clock had already counted but the drip had not run.

### Orientation

Measured on the device with the stage 1 bring-up screen, accelerometer in g
(M5Unified applies no axis remap for the Cardputer-ADV):

| Pose | x | y | z |
| --- | --- | --- | --- |
| lying, screen up | -0.02 | +0.00 | +1.00 |
| standing on the left edge, USB-C at the bottom | -1.02 | -0.01 | +0.03 |
| standing on the right edge, USB-C at the top | +0.99 | -0.01 | -0.03 |

So `z` is the screen normal, `x` is the sand axis (the 240 px side of the
picture). The whole axis table is device-confirmed: both standing sides, both
flat faces and both long edges were verified with the sand of the running app.

Thresholds, all in `src/orientation.cpp`: a pose counts when its axis reads more
than `0.80 g` and the other two stay below `0.45 g`; samples are smoothed with an
80 ms time constant; a pose is reported only after holding for 400 ms; while the
gyroscope reads more than 40 dps in total the stability timer restarts, so a flip
is not accepted halfway through. Resting on a long edge reads as dominant `y`
and is reported as `LongYUp` / `LongYDown` - the hourglass lying on its side:
the sand axis lies horizontal, the pause keeps the sand animating, and the
pile slides across the picture toward the wall the device rests on
(device-verified; the first derived mapping ran the pile to the opposite
wall, which pinned down the triad's never-measured `y` sign).

### Debug path (no IMU needed)

- In RUNNING/PAUSED the up/down keys move progress by 10 % per press, which walks
  the whole flow - running, done, beep, restart - on a flat device. Start a
  cycle on an edge once, then lay the device flat and walk it from there.

### Serial log

```
Hourglass stage 4
[hg      3] reset    : power-on (1)
board    : M5Cardputer-ADV
display  : 240x135 landscape, 135x240 portrait
keyboard : TCA8418 (ADV)
[hg     120] imu      : enabled
[hg     121] sprites  : landscape + portrait
[hg     123] heap     : 236195 bytes free
[hg     125] settings : interval 10 min, color 0 (Classic), sound on, clock on
[hg     130] nvs: interval_idx=3 color_idx=0 sound_on=1 clock_on=1
[hg     142] state: Menu -> Armed (start pressed)
[hg     331] orientation: Unstable -> FlatUp (accel -0.01 +0.00 +1.00)
[hg     512] key: Enter (fn 0 shift 0 ctrl 0 opt 0 alt 0)
```

Every line starts with the uptime in milliseconds. State changes, orientation
changes, key presses and NVS writes are logged. The once-per-second raw
accelerometer trace of the bring-up days is retired: the axis table it was
measured with is confirmed on the device in full.

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

Current image (stage 4):

```
RAM:   [===       ]  26.8% (used  87892 bytes from 327680 bytes)
Flash: [====      ]  37.0% (used 508997 bytes from 1376256 bytes)
firmware.bin: 509360 bytes
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
producing a binary that cannot be installed. Stage 4 uses 509669 of 1376256
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
tests/                      host tests, plain C++ (g++, no Arduino):
                            g++ -std=c++17 -O2 -I src tests/sand_test.cpp src/sand.cpp
                            g++ -std=c++17 -O2 -I src tests/app_state_test.cpp src/app_state.cpp
```

## Troubleshooting

- **`Board : unknown` on the screen** - M5GFX did not recognise the hardware,
  and the keyboard would then not initialise either. Check the library
  versions above.
- **`IMU : not detected`** - `M5.Imu.isEnabled()` is false; the BMI270 is
  expected at I2C address 0x68/0x69 on the internal bus (SDA 8, SCL 9).
- **No keyboard events** - the ADV keyboard sits behind a TCA8418 at 0x34 on
  the same bus, with GPIO11 as interrupt line.
- **Reset at the completion chime** (the serial `reset:` line then reads
  `panic (crash)`) - a newer speaker stack has made it back into the build.
  The pins in `platformio.ini` (espressif32 6.13.0, M5Unified 0.2.20,
  M5GFX 0.2.27) were found by bisecting exactly this - the ES8311 reset
  family of espressif/esp-idf#18621 - so restore the pins first. The
  throwaway probe firmware that had bisected this was removed once the
  sound was confirmed on the device; it is in the git history around the
  `9da7d92` commit if another on-device audio bench is ever needed.

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
7. Let it finish (or walk it with the up key): the chime sounds and DONE shows;
   flipping to the other edge starts a new cycle. Enter restarts from
   DONE and FINISH too.
8. `Del` from RUNNING returns to the menu.
9. The serial log carries the reset reason at boot (`reset:` line:
   `power-on`, `brownout`, `panic`, `task watchdog`). After an unexpected
   restart, that line is the first thing to read.
10. In the settings, switching Sound from Off to On plays the signal at once -
   a two-second way to check the audio path without waiting out an interval.
11. In the settings, switching Clock to Off hides the mm:ss on the countdown
    screens (the interval label takes its place) and turns on the analog
    hourglass: a lying/edge pause keeps draining the sand through the neck
    (nothing freezes), and DONE with the chime come when the last grain really
    lands below the throat - not when the frozen clock does. Switching it back
    On restores the synchronized countdown. Both choices survive a power
    cycle (`nvs:` line).
12. While counting down, lay the device flat: `Paused` shows and the sand of
    both bulbs slides toward one side wall (screen up: left, screen down:
    right), nothing crosses the neck and `mm:ss` freezes with it. Laying it
    the other face down slides the pile to the other wall; standing it back on
    an edge resumes the countdown and the old pile shape falls into place.
13. Rest the device on one of its long edges while counting down: the pause is
    the lying-hourglass pose, the sand slides across the picture into the wall
    the device rests on, on both edges and from both standing sides (the
    frame mirrors the direction). The direction mapping is device-verified;
    if it ever reads wrong after a hardware or rotation change, the sign
    lives in the `LongY` branch of the gravity pick in `main.cpp`.

## Next stages

The roadmap is empty: the falling-sand animation, the flat-pose physics (both
flat faces and both long edges), the Clock switch and the soft rendered chime
are all shipped and confirmed on the device. What remains is polish and
whatever the device use suggests next.
