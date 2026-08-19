#!/usr/bin/env python3
"""Run every machine-readable audit in the game and print one table.

Before this, each of the twenty TEST_* flags in source/debug.h needed somebody to
edit the header, hand-build a binary, launch it, screenshot the top screen and
read the verdict off the picture. Nine of those flags print a verdict in words,
so nine of them never needed a human at all - they needed the words to leave the
framebuffer. source/audit_log.c copies console output to sdmc:/3ds/modelkit/
audit.log; this reads that file.

    python tools/regress.py                 every audit
    python tools/regress.py paint saveload  just those
    python tools/regress.py --list          what there is to run

Each audit is a clean build (the flags reach almost every translation unit) and
one emulator launch, so the full run is minutes, not seconds. Exit status is 0
only if every audit selected actually ran and passed - so this can gate a commit.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
DEBUG_H = PROJECT / "source" / "debug.h"

# The devkitPro MSYS2 shell. Builds do not work from Git Bash: the toolchain
# paths and the /opt/devkitpro rules only exist inside it.
MSYS_BASH = Path(os.environ.get("DEVKITPRO_BASH",
                                "C:/devkitPro/msys2/usr/bin/bash.exe"))

# Azahar's emulated SD card, which is where the game's sdmc:/ writes land.
SDMC = Path(os.environ.get("APPDATA", "")) / "Azahar" / "sdmc" / "3ds" / "modelkit"
AUDIT_LOG = SDMC / "audit.log"
SAVE_BIN = SDMC / "save.bin"

EMU_PS1 = Path(__file__).resolve().parent / "emu.ps1"

# How each audit says whether it passed.
#
# The lines are the audits' own wording, unchanged - the harness was built around
# what they already print rather than the other way round, so no existing output
# had to be reworded and no audit had to be touched to be included here.
#
#   flag    : the TEST_* macro to switch on
#   verdict : regex identifying the audit's final line
#   passing : regex the same line must also match to count as a pass
AUDITS = [
    dict(name="kits",      flag="TEST_AUDIT_ALL_KITS",
         verdict=r"^GAMEPLAY AUDIT .*$",           passing=r"OK\s*$"),
    dict(name="camidle",   flag="TEST_CAMERA_IDLE_AUDIT",
         verdict=r"^CAM IDLE .*$",                 passing=r"^(?!.*FAIL).*$"),
    dict(name="workspace", flag="TEST_LEVEL1_WORKSPACE_AUDIT",
         verdict=r"^WORKSPACE L1 .*$",             passing=r"OK\s*$"),
    dict(name="campan",    flag="TEST_CAMERA_PAN_AUDIT",
         verdict=r"^PAN AUDIT .*$",                passing=r"PASS\s*$"),
    dict(name="ceiling",   flag="TEST_CEILING_AUDIT",
         verdict=r"^CEILING AUDIT .*$",            passing=r"normal=DRAW high=HIDE"),
    dict(name="collision", flag="TEST_COLLISION_AUDIT",
         verdict=r"^COLLISION AUDIT .*$",          passing=r"20/20 OK\s*$"),
    dict(name="hint",      flag="TEST_HINT_AUDIT",
         verdict=r"^HINT AUDIT .*$",               passing=r"PASS\s*$"),
    dict(name="paint",     flag="TEST_PAINT_AUDIT",
         verdict=r"^PAINT AUDIT .*$",              passing=r"PASS\s*$"),
    dict(name="saveload",  flag="TEST_SAVELOAD_AUDIT",
         verdict=r"^SAVELOAD AUDIT .*$",           passing=r"PASS\s*$"),
    dict(name="undo",      flag="TEST_UNDO_AUDIT",
         verdict=r"^UNDO AUDIT .*$",               passing=r"PASS\s*$"),
]

# Three of the audits hold their output on screen for fifteen seconds so a person
# can photograph it, and the end marker is written after that hold. Sixty seconds
# covers the slowest of them (saveload walks all twenty kits first) with room to
# spare on a slow machine.
RUN_TIMEOUT_S = 60


def fail(msg):
    print(f"regress: {msg}", file=sys.stderr)
    sys.exit(2)


def check_flags_exist():
    """Every flag this file names must still be in debug.h.

    A renamed or deleted flag would otherwise build fine - the -D just defines an
    unused macro - and the audit would silently never run while the table went on
    reporting whatever the log happened to contain.
    """
    text = DEBUG_H.read_text(encoding="utf-8", errors="replace")
    missing = [a["flag"] for a in AUDITS
               if not re.search(rf"^#define {a['flag']} 0$", text, re.M)]
    if missing:
        fail("not in source/debug.h any more: " + ", ".join(missing))


def powershell(*args):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                           "-File", str(EMU_PS1), *args],
                          capture_output=True, text=True)


def build(flag):
    """One clean build with exactly one flag on. Returns (ok, tail of output)."""
    cmd = (f'cd "{PROJECT.as_posix()}" && make clean >/dev/null && '
           f'make HARNESS=-D{flag}=1 2>&1')
    r = subprocess.run([str(MSYS_BASH), "-lc", cmd], capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    return r.returncode == 0 and "built ... modelkit.3dsx" in out, out.strip()[-800:]


def run_one(audit):
    """Build, launch, wait for the log's end marker, read the verdict."""
    ok, out = build(audit["flag"])
    if not ok:
        return dict(audit, status="BUILD FAILED", line=out.splitlines()[-1] if out else "")

    powershell("-Action", "kill")
    AUDIT_LOG.unlink(missing_ok=True)
    if AUDIT_LOG.exists():
        return dict(audit, status="ERROR", line=f"could not clear {AUDIT_LOG}")

    powershell("-Action", "boot", "-App", str(PROJECT / "modelkit.3dsx"),
               "-W", "1040", "-H", "1385", "-SettleMs", "1500")

    # Wait for the marker, not for a fixed sleep. An audit that crashed leaves a
    # log with no end marker, which is a result the table can show rather than a
    # timeout somebody has to interpret.
    text, deadline = "", time.time() + RUN_TIMEOUT_S
    while time.time() < deadline:
        if AUDIT_LOG.exists():
            text = AUDIT_LOG.read_text(encoding="utf-8", errors="replace")
            if "AUDIT LOG END" in text:
                break
        time.sleep(1)
    powershell("-Action", "kill")

    if not text:
        return dict(audit, status="NO LOG", line="the build wrote nothing to the card")
    if "AUDIT LOG END" not in text:
        tail = [l for l in text.splitlines() if l.strip()]
        return dict(audit, status="DIED", line=tail[-1] if tail else "(empty)")

    hits = [l for l in text.splitlines() if re.match(audit["verdict"], l)]
    if not hits:
        return dict(audit, status="NO VERDICT", line="log has no line matching " + audit["verdict"])

    # The last one: an audit that prints per-level failures before its summary
    # would otherwise be read off the wrong line.
    line = hits[-1].rstrip()
    passed = re.search(audit["passing"], line) is not None
    return dict(audit, status="PASS" if passed else "FAIL", line=line)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="audits to run (default: all)")
    ap.add_argument("--list", action="store_true", help="list them and stop")
    args = ap.parse_args()

    if args.list:
        for a in AUDITS:
            print(f"  {a['name']:<10} {a['flag']}")
        return 0

    if not MSYS_BASH.exists():
        fail(f"devkitPro MSYS2 shell not found at {MSYS_BASH}")
    if not EMU_PS1.exists():
        fail(f"emulator driver not found at {EMU_PS1}")
    if not SDMC.exists():
        fail(f"Azahar's SD card not found at {SDMC} - run the game once first")
    check_flags_exist()

    todo = AUDITS
    if args.names:
        known = {a["name"] for a in AUDITS}
        unknown = [n for n in args.names if n not in known]
        if unknown:
            fail("no such audit: " + ", ".join(unknown) + " (try --list)")
        todo = [a for a in AUDITS if a["name"] in args.names]

    # The audits fabricate game state and some of them delete save.bin outright.
    # main.c refuses to write the card while a TEST_* flag is on, but the saveload
    # audit writes it deliberately, so the real file is put somewhere safe first
    # and put back at the end whatever happens.
    backup = None
    if SAVE_BIN.exists():
        backup = SAVE_BIN.with_suffix(".regress-backup")
        shutil.copy2(SAVE_BIN, backup)
        print(f"save.bin backed up to {backup.name}")

    results = []
    try:
        for i, a in enumerate(todo, 1):
            print(f"[{i}/{len(todo)}] {a['name']} ({a['flag']}) ...", flush=True)
            r = run_one(a)
            print(f"        {r['status']}  {r['line']}", flush=True)
            results.append(r)
    finally:
        if backup:
            shutil.copy2(backup, SAVE_BIN)
            backup.unlink()
            print("save.bin restored")

    width = max(len(r["name"]) for r in results)
    print("\n" + "=" * 72)
    for r in results:
        print(f"  {r['name']:<{width}}  {r['status']:<12}  {r['line']}")
    print("=" * 72)

    passed = sum(1 for r in results if r["status"] == "PASS")
    print(f"  {passed}/{len(results)} passed")
    # Anything that is not a clean PASS fails the run, including a build that did
    # not compile and an audit that never reported. "Did not run" is not the same
    # as "passed" and must never be scored as one.
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
