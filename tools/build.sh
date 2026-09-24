#!/usr/bin/env bash
# Build the DirectML diagnostics (Windows PE) with clang + lld – no mingw needed.
#   dmltest.exe   D3D12 (vkd3d-proton) + DMLCreateDevice + max DML feature level
#   dmltest2.exe  the ONNX Runtime path: DXCore adapters -> D3D12 -> DirectML per adapter
# Run inside the prefix, e.g.:
#   WINEPREFIX=~/Games/Animates/prefix/pfx WINE_D3D_CONFIG=renderer=vulkan \
#   WINEDLLOVERRIDES="d3d12=n;d3d12core=n;dxgi=n;d3d11=n;dxcore=n,b" "$PROTON_DIR/files/bin/wine" dmltest2.exe
set -eu
cd "$(dirname "$0")"
printf 'LIBRARY kernel32.dll\nEXPORTS\nLoadLibraryA\nGetProcAddress\nExitProcess\n' > kernel32.def
printf 'LIBRARY msvcrt.dll\nEXPORTS\nprintf\n' > msvcrt.def
llvm-dlltool -m i386:x86-64 -d kernel32.def -l kernel32.lib
llvm-dlltool -m i386:x86-64 -d msvcrt.def -l msvcrt.lib
for t in dmltest dmltest2; do
    clang --target=x86_64-pc-windows-msvc -O1 -fuse-ld=lld -nostdlib -Wl,/subsystem:console \
        -Wl,/entry:mainCRTStartup -o "$t.exe" "$t.c" kernel32.lib msvcrt.lib
done
rm -f kernel32.def msvcrt.def kernel32.lib msvcrt.lib
echo built dmltest.exe dmltest2.exe
