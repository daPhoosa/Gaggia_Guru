"""
PlatformIO pre-build script.

Parses include/io_pins.h and generates include/io_pins_table.gen.h, the table
the firmware uses to serve /api/io (dashboard "IO Status" page).

Active pin lines look like:
    #define <MACRO>  <gpio>   // <Name> | <Type> | <optional notes>
Lines starting with "//" are ignored.
"""
import os
import re
import sys

try:
    Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
    PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:  # allow running standalone: python scripts/gen_io_pins.py
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC = os.path.join(PROJECT_DIR, "include", "io_pins.h")
OUT = os.path.join(PROJECT_DIR, "include", "io_pins_table.gen.h")

TYPES = {"analog": "Analog", "digital": "Digital",
         "communications": "Communications", "unused": "Unused"}
MODULE_GPIOS = set(range(0, 6)) | set(range(12, 20)) | {21, 22, 23, 25, 26, 27} | set(range(32, 37)) | {39}
FLASH_GPIOS = set(range(6, 12))

LINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(\d+)\s*//(.*)$")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(\d+)\b")


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def fail(lineno, msg):
    sys.stderr.write(f"\n*** {SRC}:{lineno}: {msg}\n\n")
    sys.exit(1)


def parse():
    pins, seen = [], {}
    with open(SRC, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            m = LINE_RE.match(line)
            if not m:
                if DEFINE_RE.match(line) and not line.lstrip().startswith("#pragma"):
                    fail(lineno, "pin define is missing its '// Name | Type | notes' comment")
                continue
            macro, gpio, comment = m.group(1), int(m.group(2)), m.group(3)
            fields = [p.strip() for p in comment.split("|")]
            if len(fields) < 2 or not fields[0]:
                fail(lineno, f"{macro}: comment must be '// Name | Type | notes'")
            name, typ = fields[0], fields[1].lower()
            notes = " | ".join(fields[2:]).strip()
            if typ not in TYPES:
                fail(lineno, f"{macro}: type '{fields[1]}' must be one of {', '.join(TYPES.values())}")
            if gpio in FLASH_GPIOS:
                fail(lineno, f"{macro}: GPIO{gpio} is wired to the internal flash and cannot be used")
            if gpio not in MODULE_GPIOS:
                fail(lineno, f"{macro}: GPIO{gpio} is not available on the ESP32-WROOM-32U")
            if gpio in seen:
                fail(lineno, f"{macro}: GPIO{gpio} already defined as {seen[gpio]}")
            seen[gpio] = macro
            pins.append((gpio, macro, name, TYPES[typ], notes))
    return sorted(pins)


def generate():
    pins = parse()
    lines = [
        "// AUTO-GENERATED from include/io_pins.h by scripts/gen_io_pins.py - DO NOT EDIT.",
        "// Edit the comments in io_pins.h instead; this file is rebuilt on every build.",
        "#pragma once",
        "",
        "// { gpio, macro, name, type, notes }",
        "#define IO_PIN_TABLE \\",
    ]
    for gpio, macro, name, typ, notes in pins:
        lines.append(f"  {{{gpio}, {c_str(macro)}, {c_str(name)}, IoType::{typ}, {c_str(notes)}}}, \\")
    lines += ["", ""]
    text = "\n".join(lines)

    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else None
    if text != old:  # only touch the file when it changes (avoids needless rebuilds)
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"[io_pins] Generated {os.path.relpath(OUT, PROJECT_DIR)} ({len(pins)} pins)")


generate()
