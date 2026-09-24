#!/usr/bin/env python3
"""Apply the Wine compatibility patches for Animates 1.0.9.

Patches and offsets are from psre423p45's guide
(https://github.com/psre423p45/Animates-Ani-on-Linux). Idempotent: already
patched files are skipped; *.orig backups are kept next to each file.

  patch-animates.py            apply patches
  patch-animates.py --check    exit 0 if fully patched, 1 otherwise
  patch-animates.py --restore  restore the *.orig backups
"""
import hashlib
import os
import shutil
import sys
from pathlib import Path

HOME = Path(os.environ.get("ANIMATES_HOME", Path.home() / "Games/Animates"))
DATA = HOME / "prefix/pfx/drive_c/users/steamuser/AppData/Local/AnimateApp/current/Animates_Data"

# file -> (original md5, [(offset, original bytes, patched bytes, description)])
PATCHES = {
    "Managed/Assembly-CSharp.dll": ("827d426bc1421dbacca467daaed1fdcf", [
        (472266, b"\x17", b"\x16", "disable DirectComposition (black UI)"),
        (268016, b"\x02", b"\x2a", "no-op WindowsAPI.SetClickThrough (drag companion)"),
    ]),
    "Plugins/x86_64/WebViewHost.dll": ("9a382b1500b4a05fb4afbc8c8d6431f7", [
        (55728, b"\x40\x57\x48", b"\x31\xc0\xc3", "no-op WVH_SetClickThrough (clicks fall through UI)"),
    ]),
    "Managed/NAudio.Wasapi.dll": ("06df328b05d90a91e3f353577ef8a126", [
        (15350, b"\x02", b"\x2a", "no-op UnregisterNotifications (crash after login)"),
    ]),
}


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def state(data: bytes, orig_md5: str, patches) -> str:
    if md5(data) == orig_md5:
        return "original"
    if all(data[o:o + len(new)] == new for o, _, new, _ in patches):
        return "patched"
    return "unknown"


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "--apply"
    ok = True
    for rel, (orig_md5, patches) in PATCHES.items():
        path = DATA / rel
        backup = path.with_name(path.name + ".orig")
        if mode == "--restore":
            if backup.exists():
                shutil.copy2(backup, path)
                print(f"restored {rel}")
            continue

        data = path.read_bytes()
        st = state(data, orig_md5, patches)
        if mode == "--check":
            print(f"{rel}: {st}")
            ok &= st == "patched"
            continue

        if st == "patched":
            continue
        if st != "original":
            print(f"{rel}: UNKNOWN version (md5 {md5(data)}), not touching it. "
                  "Offsets must be re-derived for this Animates version (see the original guide).",
                  file=sys.stderr)
            ok = False
            continue

        buf = bytearray(data)
        for off, old, new, desc in patches:
            assert buf[off:off + len(old)] == old, f"{rel}@{off}: unexpected bytes"
            buf[off:off + len(new)] = new
            print(f"{rel}@{off}: {desc}")
        shutil.copy2(path, backup)
        path.write_bytes(buf)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
