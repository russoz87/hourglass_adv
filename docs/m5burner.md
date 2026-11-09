# M5Burner publication - Hourglass

Working notes for the M5Burner `USER CUSTOM` -> `Publish` form, then the
description itself (paste everything below the divider).

## Form fields

| Field | Value |
| --- | --- |
| Name | Hourglass |
| Version | 0.1.1 (bump `src/version.h` and re-tag for each release) |
| Device Type | Cardputer-ADV |
| GitHub | <https://github.com/russoz87/hourglass_adv> |
| FirmWare | `hourglass.bin` from the GitHub release (the form takes the single app image) |
| Cover | a photo or screenshot of the running hourglass, 240x135 or larger |

---

# Hourglass

**A falling-sand hourglass for the M5Stack Cardputer-ADV.** Stand the device
on its side to start the countdown, flip it over to send time back, lay it
flat to pause - it behaves like the real thing, down to the last grain.

The sand is a live cellular-automaton simulation (5,000 grains in a classic
twin-bulb glass), not a pre-rendered animation. The neck is a valve that lets
grains through at exactly the interval's rate, so the picture and the clock
always agree.

## Features

- **Physical gestures (BMI270 IMU).** Standing on either side edge arms and
  starts the timer; flipping mid-run reverses the flow and the clock counts
  back up; laying the device flat - on either face or on a long edge - pauses
  the countdown and settles the sand sideways against the lower wall. Nothing
  ever crosses the neck while lying down.
- **Seven intervals:** 1, 2, 5, 10, 20, 25 and 30 minutes.
- **Eight sand colours**, with a live preview in Settings.
- **A soft completion chime** (A5 then D6, rendered sines) - can be switched
  off for a silent timer.
- **Clock switch:** show the `mm:ss` readout, or hide it and watch just the
  sand. The countdown is identical either way.
- **Settings survive power cycles** (stored in NVS).
- Flip a finished hourglass to start again - no buttons needed.

## Controls

The Cardputer keyboard has no dedicated arrow keys; navigation never needs
Fn:

| Key | Action |
| --- | --- |
| `;` / `.` | Up / Down |
| `,` / `/` | Left / Right |
| `Enter` or `Opt` | OK - select interval, arm the timer |
| `Del` | Back / cancel |
| `S` (in the menu) | Settings: sand colour, sound, clock |

Choose an interval, press `Enter`, and put the device on its side. While the
timer is running or paused, the Up/Down keys nudge the progress by 10% - handy
for trying the completion chime without waiting.

## Installation note

**Burning this firmware replaces whatever is installed on the device.** If
your Cardputer-ADV runs the bmorcelli Launcher, the burn replaces it and the
device boots straight into Hourglass (you can re-flash the Launcher with
M5Burner at any time).

To **keep** the Launcher, install Hourglass as a Launcher app instead: copy
`hourglass.bin` from the GitHub release to a microSD card and install it from
the Launcher's SD-card file menu. The same `hourglass.bin` is attached to
every release.

## Links

- Source code, build instructions and the full manual:
  <https://github.com/russoz87/hourglass_adv>
- Releases (microSD image + full-flash bundle):
  <https://github.com/russoz87/hourglass_adv/releases>
- Licence: MIT
