"""Deliver the installable image as build/hourglass.bin.

PlatformIO keeps its build tree (.pio/build/...) for itself; the Launcher
install expects one named image on the microSD card. This post action copies
the image that just built into <project>/build/hourglass.bin - the app's name
is the name the Launcher's SD install menu will show.

It also assembles the distribution bundle under build/burner/: a full flash
(see README "Distributing via M5Burner") is three images - the bootloader at
0x0000, the partition table at 0x8000 and the app at 0x10000.
"""

Import("env")  # noqa: F821 - provided by PlatformIO

import os
import shutil

OUT_NAME = "hourglass.bin"


def copy_out(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    pio_dir     = os.path.dirname(str(target[0]))
    build_dir   = os.path.join(project_dir, "build")

    # The microSD image for the Launcher install.
    os.makedirs(build_dir, exist_ok=True)
    out_path = os.path.join(build_dir, OUT_NAME)
    shutil.copy2(str(target[0]), out_path)
    print("[hourglass] image -> %s" % os.path.relpath(out_path))

    # The full-flash bundle for the M5Burner distribution.
    burner_dir = os.path.join(build_dir, "burner")
    os.makedirs(burner_dir, exist_ok=True)
    for name, copied in (("bootloader.bin", "bootloader.bin"),
                         ("partitions.bin", "partitions.bin"),
                         (os.path.basename(str(target[0])), OUT_NAME)):
        produced = os.path.join(pio_dir, name)
        if os.path.isfile(produced):
            shutil.copy2(produced, os.path.join(burner_dir, copied))
    print("[hourglass] burner bundle -> %s" % os.path.relpath(burner_dir))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_out)
