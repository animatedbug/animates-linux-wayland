#!/usr/bin/env python3
"""Build a patched copy of Proton's Wine dxcore.dll so Animates can use DirectML.

Why: Animates' AnimationCPP plugin (ONNX Runtime) only uses the DirectML
execution provider if DXCore reports a D3D12-capable adapter. Wine's
IDXCoreAdapter::IsAttributeSupported / IsPropertySupported are stubs that always
return FALSE, so every animation model runs on the CPU (all cores busy while Ani
talks). DirectML itself works fine on vkd3d-proton.

What: both stubs are patched to `mov eax, 1; ret`. Functions are located via
the DLL's COFF symbol table, so this works across Proton builds as long as Wine
ships symbols. The "Wine builtin DLL" marker is cleared so Wine loads the file as
a native DLL (WINEDLLOVERRIDES=dxcore=n,b).

Usage: make-dxcore.py <proton dir> <output dxcore.dll>
Nothing from Wine is redistributed: the user's own Proton copy is patched locally.
"""
import struct
import sys
from pathlib import Path

TARGETS = [b"dxcore_adapter_IsAttributeSupported", b"dxcore_adapter_IsPropertySupported"]
RETURN_TRUE = b"\xb8\x01\x00\x00\x00\xc3"  # mov eax, 1; ret
MARKER = b"Wine builtin DLL"


def find_symbols(d: bytes) -> dict:
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    symptr, nsym = struct.unpack_from("<II", d, pe + 12)
    optsize = struct.unpack_from("<H", d, pe + 20)[0]
    if not symptr:
        sys.exit("dxcore.dll has no symbol table – cannot locate the stubs safely")
    sections = []
    for i in range(nsec):
        o = pe + 24 + optsize + i * 40
        vsize, rva, rawsize, raw = struct.unpack_from("<IIII", d, o + 8)
        sections.append((rva, raw, rawsize))
    strtab = symptr + nsym * 18
    found = {}
    i = 0
    while i < nsym:
        o = symptr + i * 18
        raw_name = d[o:o + 8]
        if raw_name[:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", raw_name, 4)[0]
            name = d[strtab + off:d.index(b"\0", strtab + off)]
        else:
            name = raw_name.rstrip(b"\0")
        value, secnum = struct.unpack_from("<Ih", d, o + 8)
        naux = d[o + 17]
        if name in TARGETS and secnum > 0:
            rva, raw, rawsize = sections[secnum - 1]
            found[name] = raw + value  # file offset
        i += 1 + naux
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src = Path(sys.argv[1]) / "files/lib/wine/x86_64-windows/dxcore.dll"
    out = Path(sys.argv[2])
    data = bytearray(src.read_bytes())
    syms = find_symbols(bytes(data))
    for name in TARGETS:
        if name not in syms:
            sys.exit(f"{name.decode()} not found in {src} – Wine changed; please report")
        off = syms[name]
        if data[off:off + len(RETURN_TRUE)] != RETURN_TRUE:
            data[off:off + len(RETURN_TRUE)] = RETURN_TRUE
        print(f"patched {name.decode()} @ {off:#x}")
    if data[0x40:0x40 + len(MARKER)] == MARKER:
        data[0x40:0x40 + len(MARKER)] = b"\0" * len(MARKER)
    out.write_bytes(data)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
