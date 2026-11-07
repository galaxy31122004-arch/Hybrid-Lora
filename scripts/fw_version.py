"""PlatformIO pre script (pre so FW_VERSION reaches the project sources): define FW_VERSION and keep a copy of each build per version.

The version comes from `custom_fw_version` in platformio.ini. After a build,
firmware.bin and firmware.elf are copied to
releases/v<version>/hybrid-lora-<env>-v<version>.{bin,elf}.
"""

import os
import re
import shutil

Import("env")  # noqa: F821 - provided by PlatformIO/SCons

version = env.GetProjectOption("custom_fw_version", "").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    raise SystemExit(
        "platformio.ini: custom_fw_version must be MAJOR.MINOR.PATCH, got %r" % version
    )

env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % version)])


def copy_release(source, target, env):
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "releases", "v" + version)
    os.makedirs(out_dir, exist_ok=True)
    stem = "hybrid-lora-%s-v%s" % (env.subst("$PIOENV"), version)
    for ext in ("bin", "elf"):
        built = env.subst("$BUILD_DIR/${PROGNAME}.%s" % ext)
        if os.path.isfile(built):
            dest = os.path.join(out_dir, "%s.%s" % (stem, ext))
            shutil.copyfile(built, dest)
            print("Firmware v%s -> %s" % (version, dest))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_release)
