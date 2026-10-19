"""Refuse to flash the device over USB.

The Cardputer-ADV used for this project runs Launcher v2.6.x, and Hourglass is
installed as a Launcher app from the microSD card. A PlatformIO upload writes
bootloader, partition table and firmware with esptool, which would overwrite
that Launcher installation, so the uploading targets are blocked here.

Build with `pio run` and install firmware.bin through the Launcher SD menu.
"""

Import("env")  # noqa: F821 - provided by PlatformIO

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

BLOCKED_TARGETS = ("upload", "uploadfs", "uploadfsota", "erase", "eraseflash")

requested = [name for name in COMMAND_LINE_TARGETS if name in BLOCKED_TARGETS]
if requested:
    raise SystemExit(
        "[hourglass] refusing '%s': flashing over USB would erase the Launcher "
        "installation on the device.\n"
        "            Run `pio run` and install firmware.bin from the Launcher SD "
        "menu instead." % ", ".join(requested)
    )
