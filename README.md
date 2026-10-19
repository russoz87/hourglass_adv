# Hourglass

An hourglass (sand timer) firmware for the **M5Stack Cardputer-ADV**
(ESP32-S3FN8, 8 MB flash, 240x135 IPS display, 56-key keyboard, BMI270 IMU,
ES8311 codec). Putting the device on its side starts the countdown, turning it
over reverses it, and laying it flat pauses it - like a real hourglass.

## Status: stage 1 - bring-up

This stage contains no timer logic yet. It exists to prove on real hardware
that the toolchain, the display, the keyboard and the IMU all work, because
every later stage depends on them:

- `M5Cardputer.begin(cfg, true)` with the landscape 240x135 frame,
- keyboard input read through `M5Cardputer.Keyboard` (on the ADV this is a
  TCA8418 I2C controller, not the GPIO matrix of the original Cardputer),
- IMU read through `M5.Imu` (`update()` / `getImuData()`), which is a BMI270
  on this board,
- the same values on screen and on the USB serial port at 115200 baud.

### What the screen shows

```
Hourglass bring-up
------------------------------------------------
Board  : M5Cardputer-ADV
Key    : a               <- last key event, gold
Mods   : SHIFT           <- modifiers held with it
Events : 3  (held 2)
Accel g:  -0.012  +0.004  +1.001
Gyro dps:  +0.13   -0.42   +0.07
IMU    : enabled, 1234 samples
Uptime : 41.7 s
```

`Board` is the board M5GFX detected; on the ADV it is what selects the TCA8418
keyboard reader, so it is the first line to check when something misbehaves.

### What the serial port shows

```
Hourglass bring-up
board         : M5Cardputer-ADV
display       : 240x135, rotation 1
keyboard      : TCA8418 (ADV)
IMU           : enabled
serial        : 115200 baud

[key] a   mods: SHIFT   held: 2   events: 3
[imu] accel [g] -0.012 +0.004 +1.001   gyro [dps] +0.13 -0.42 +0.07
```

Key events are printed as they happen, the IMU trace twice per second.

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

Current image (stage 1):

```
RAM:   [=         ]   8.6% (used 28084 bytes from 327680 bytes)
Flash: [======    ]  55.8% (used 768239 bytes from 1376256 bytes)
firmware.bin: 801456 bytes
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
producing a binary that cannot be installed. Stage 1 uses 801456 of 1376256
bytes, leaving ~575 kB for the timer, the falling-sand simulation and the UI.

## Serial monitor

```sh
pio device monitor --baud 115200
```

USB CDC is enabled on the board (`DARDUINO_USB_CDC_ON_BOOT=1`), so the log
appears on the USB-C port the device is already connected through.

## Project layout

```
platformio.ini            build configuration (pinned platform/libraries)
partitions.csv            Launcher app-slot layout, used for the size limit
scripts/guard_usb_flash.py  blocks upload/erase targets
src/main.cpp              stage 1 bring-up application
```

## Troubleshooting

- **`Board : unknown` on the screen** - M5GFX did not recognise the hardware,
  and the keyboard would then not initialise either. Check the library
  versions above.
- **`IMU : not detected`** - `M5.Imu.isEnabled()` is false; the BMI270 is
  expected at I2C address 0x68/0x69 on the internal bus (SDA 8, SCL 9).
- **No keyboard events** - the ADV keyboard sits behind a TCA8418 at 0x34 on
  the same bus, with GPIO11 as interrupt line.

## Next stages

- **Stage 2:** timer state machine (idle / running / paused / finished),
  interval selection 1/2/5/10/20/25/30 min, `mm:ss` countdown.
- **Stage 3:** falling-sand cellular automaton drawn in the 90-degree rotated
  frame, one image colour, sound on finish.
- **Stage 4:** IMU orientation control from `M5.Imu` - on its side runs,
  flipped reverses, flat pauses; needs debouncing and hysteresis on the
  gravity vector, since the accelerometer here is unsmoothed.
