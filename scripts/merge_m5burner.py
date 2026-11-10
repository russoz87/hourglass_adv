#!/usr/bin/env python3
"""Merge the build products into the single full-flash image M5Burner wants.

M5Burner's USER CUSTOM publish/burn path writes the uploaded .bin from
address 0x0. build/hourglass.bin is only the application image (it belongs
at 0x10000), so uploading it leaves the device unbootable ("Invalid image
block, can't boot"). This script merges, from address 0x0:

    0x0000   bootloader.bin   from the PlatformIO build
    0x8000   partitions.bin   the app's partition table
    0xE000   boot_app0.bin    stock OTA-data image from the Arduino core;
                             our table has no OTA slots, so the boot falls
                             through to the app regardless of its contents
    0x10000  firmware.bin     the app

The result, build/hourglass-m5burner.bin, is what M5Burner's own Firmware
Exporter produces: one contiguous full-flash image. Flash mode, frequency
and size match the build's image header (DIO, 80 MHz, 8 MB).

Run after `pio run` (or `make firmware`):  python3 scripts/merge_m5burner.py
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_BUILD = os.path.join(ROOT, ".pio", "build", "cardputer-adv")
PACKAGES = os.path.expanduser("~/.platformio/packages")
OUT = os.path.join(ROOT, "build", "hourglass-m5burner.bin")

PARTS = (
    ("0x0", os.path.join(PIO_BUILD, "bootloader.bin")),
    ("0x8000", os.path.join(PIO_BUILD, "partitions.bin")),
    ("0xE000", os.path.join(PACKAGES, "framework-arduinoespressif32",
                            "tools", "partitions", "boot_app0.bin")),
    ("0x10000", os.path.join(PIO_BUILD, "firmware.bin")),
)


def esptool_command():
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        pass
    # No esptool module in this interpreter: use the one PlatformIO runs its
    # builds with (it has pyserial) plus the tool package's esptool.py.
    pio = shutil.which("pio")
    if pio:
        with open(pio) as f:
            shebang = f.readline()
        if shebang.startswith("#!"):
            return [shebang[2:].strip(),
                    os.path.join(PACKAGES, "tool-esptoolpy", "esptool.py")]
    sys.exit("esptool not found: pip install esptool, or run where pio exists")


def main():
    for _, path in PARTS:
        if not os.path.isfile(path):
            sys.exit("missing %s - run `pio run` first" % path)
    args = ["--chip", "esp32s3", "merge_bin", "-o", OUT,
            "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "8MB"]
    for offset, path in PARTS:
        args += [offset, path]
    subprocess.check_call(esptool_command() + args)
    print("[hourglass] M5Burner image -> %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
