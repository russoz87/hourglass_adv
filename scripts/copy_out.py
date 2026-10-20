"""Deliver the installable image as build/hourglass.bin.

PlatformIO keeps its build tree (.pio/build/...) for itself; the Launcher
install expects one named image on the microSD card. This post action copies
the image that just built into <project>/build/hourglass.bin - the app's name
is the name the Launcher's SD install menu will show.
"""

Import("env")  # noqa: F821 - provided by PlatformIO

import os
import shutil

OUT_NAME = "hourglass.bin"


def copy_out(source, target, env):
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, OUT_NAME)
    shutil.copy2(str(target[0]), out_path)
    print("[hourglass] image -> %s" % os.path.relpath(out_path))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_out)