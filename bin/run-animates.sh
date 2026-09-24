#!/usr/bin/env bash
# Launch Animates (Ani) via GE-Proton with all the Linux/Wayland fixes applied.
# Settings: ~/.config/animates-linux.conf (see config.example).
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
CONF="${ANIMATES_CONF:-$HOME/.config/animates-linux.conf}"
# shellcheck source=../config.example
source "$REPO/config.example"
[ -f "$CONF" ] && source "$CONF"

PREFIX="$ANIMATES_HOME/prefix"
EXE="$PREFIX/pfx/drive_c/users/steamuser/AppData/Local/AnimateApp/current/Animates.exe"
[ -f "$EXE" ] || { echo "Animates is not installed in $PREFIX – run install.sh" >&2; exit 1; }

# Velopack updates overwrite the patched files: re-apply (verified by MD5).
ANIMATES_HOME="$ANIMATES_HOME" "$REPO/bin/patch-animates.py" ||
    { notify-send "Animates" "Patching failed – new Animates version? See README." 2>/dev/null; exit 1; }

export STEAM_COMPAT_DATA_PATH="$PREFIX"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam"
export WEBVIEW2_BROWSER_EXECUTABLE_FOLDER='C:\webview2fixed'
export WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS='--no-sandbox --remote-debugging-port=9222'

if [ "$WAYLAND" = 1 ]; then
    export PROTON_ENABLE_WAYLAND=1
    export ANIMATES_ALPHA_LAYER=1   # alpha-layer/: premultiplied alpha -> transparent background
fi

# GL always via Mesa. Never let the Wine Wayland driver present NVIDIA-rendered
# buffers on a hybrid laptop: the NVIDIA -> iGPU handoff crashed niri (Mesa abort).
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
export __GLX_VENDOR_LIBRARY_NAME=mesa

if [ "$DML_MODE" != cpu ]; then
    # AnimationCPP only uses DirectML if DXCore reports a D3D12 GPU; Wine's dxcore
    # stubs say no. Patched copy (dxcore/make-dxcore.py) goes into system32, since
    # callers load it with LOAD_LIBRARY_SEARCH_SYSTEM32. wined3d must use Vulkan
    # for Wine's dxcore to enumerate adapters at all.
    SYS32="$PREFIX/pfx/drive_c/windows/system32"
    [ -e "$SYS32/dxcore.dll.wine-orig" ] || cp -p "$SYS32/dxcore.dll" "$SYS32/dxcore.dll.wine-orig"
    cp -f "$ANIMATES_HOME/dxcore.dll" "$SYS32/dxcore.dll"
    export WINEDLLOVERRIDES="dxcore=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
    export WINE_D3D_CONFIG="renderer=vulkan"
fi

if [ "$DML_MODE" = dgpu ] && [ -n "$DGPU_PCI_ID" ]; then
    # Wine's dxcore reports only the first Vulkan device and ONNX Runtime hands that
    # adapter to vkd3d-proton -> list the dGPU first so DirectML (compute only, never
    # presents) runs there. Unity is forced onto the iGPU; WebView2 renders in
    # software so nothing presents from the dGPU.
    export MESA_VK_DEVICE_SELECT="$DGPU_PCI_ID"
    set -- -force-device-index "$UNITY_DEVICE_INDEX" "$@"
    WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS+=' --disable-gpu'
elif [ "$WAYLAND" = 1 ]; then
    export VK_DRIVER_FILES="$IGPU_ICD"   # single-GPU path: hide any dGPU entirely
fi

exec "$PROTON_DIR/proton" run "$EXE" "$@" >"$ANIMATES_HOME/last-run.log" 2>&1
