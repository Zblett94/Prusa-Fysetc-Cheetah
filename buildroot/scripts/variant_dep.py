#
# The printer variant header is included through a macro (#include FW_VARIANT, see
# Firmware/Configuration_var.h), which the dependency scanner of the build does not
# follow: editing the variant would not rebuild the files using it. Put a hash of the
# variant file into the compiler flags, so that a change of it rebuilds everything.
#
import hashlib
import os
import re

Import("env")

def variant_path():
    # pre: script: the build_flags are not parsed into CPPDEFINES yet
    for f in env.get("BUILD_FLAGS", []):
        m = re.match(r'-DFW_VARIANT=\\?"(.+?)\\?"$', str(f))
        if m:
            return m.group(1)
    return None

name = variant_path()
if name:
    path = os.path.join(env.subst("$PROJECT_DIR"), "Firmware", name)
    with open(path, "rb") as f:
        digest = hashlib.sha1(f.read()).hexdigest()[:12]
    env.Append(CPPDEFINES=[("FW_VARIANT_HASH", "0x" + digest)])
