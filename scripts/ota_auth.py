"""
PlatformIO post script for the esp32-ota environment.

Adds --auth=<password> to the espota upload flags so the OTA password is set in
one place only. Source, first match wins:
  1. environment variable GAGGIA_OTA_PASS
  2. #define OTA_PASSWORD "..." in include/secrets.h
"""
import os
import re

Import("env")  # noqa: F821


def ota_password():
    pw = os.environ.get("GAGGIA_OTA_PASS")
    if pw:
        return pw
    path = os.path.join(env.subst("$PROJECT_INCLUDE_DIR"), "secrets.h")  # noqa: F821
    try:
        with open(path, encoding="utf-8") as f:
            m = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"((?:[^"\\]|\\.)*)"', f.read(), re.M)
    except OSError:
        m = None
    return m.group(1).encode().decode("unicode_escape") if m else None


def add_auth(source, target, env):
    pw = ota_password()
    if not pw:
        print("[ota_auth] No OTA password: set OTA_PASSWORD in include/secrets.h or GAGGIA_OTA_PASS")
        env.Exit(1)
    flags = [f for f in env["UPLOADERFLAGS"] if not str(f).startswith("--auth")]
    env.Replace(UPLOADERFLAGS=flags + [f"--auth={pw}"])


env.AddPreAction("upload", add_auth)  # noqa: F821
env.AddPreAction("uploadfs", add_auth)  # noqa: F821
