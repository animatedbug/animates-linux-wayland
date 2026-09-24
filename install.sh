#!/usr/bin/env bash
# animates-linux-wayland installer. Idempotent: safe to re-run.
#
#   ./install.sh /path/to/Animates.exe
#
# You download Animates.exe yourself from https://animates.ai (official installer).
# Everything else (WebView2 fixed runtime from nuget.org, patches, helpers) is set up here.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
CONF="$HOME/.config/animates-linux.conf"
say() { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }
ask() { read -r -p "$1 [Y/n] " a; [[ -z "$a" || "$a" =~ ^[Yy] ]]; }

INSTALLER="${1:-}"

# ---------------------------------------------------------------- config
source "$REPO/config.example"
if [ -f "$CONF" ]; then
    say "Using existing config $CONF"
    source "$CONF"
else
    say "Creating $CONF"
    cp "$REPO/config.example" "$CONF"
    # Detect a discrete GPU (hybrid laptop) for DirectML.
    dgpu=""
    for dev in /sys/bus/pci/devices/*; do
        cls=$(cat "$dev/class"); [[ "$cls" == 0x03* ]] || continue
        ven=$(cat "$dev/vendor"); did=$(cat "$dev/device")
        [ "$ven" = 0x8086 ] && continue   # Intel iGPU
        [ "$(cat "$dev/boot_vga" 2>/dev/null)" = 1 ] && continue
        dgpu="${ven#0x}:${did#0x}"
    done
    if [ -n "$dgpu" ]; then
        say "Discrete GPU found: $dgpu -> DML_MODE=dgpu"
        sed -i "s|^DGPU_PCI_ID=.*|DGPU_PCI_ID=\"$dgpu\"|" "$CONF"
    else
        say "No discrete GPU found -> DML_MODE=igpu"
        sed -i "s|^DML_MODE=.*|DML_MODE=igpu|" "$CONF"
    fi
    source "$CONF"
fi

PREFIX="$ANIMATES_HOME/prefix"
PFX="$PREFIX/pfx"
APP="$PFX/drive_c/users/steamuser/AppData/Local/AnimateApp/current"
mkdir -p "$ANIMATES_HOME/downloads"

# ---------------------------------------------------------------- deps
for c in python3 curl unzip cc; do command -v "$c" >/dev/null || die "missing: $c"; done
[ -f /usr/include/vulkan/vk_layer.h ] || die "missing Vulkan headers (Arch: vulkan-headers, Debian/Ubuntu: libvulkan-dev)"
[ -x "$PROTON_DIR/proton" ] || die "GE-Proton not found at $PROTON_DIR (set PROTON_DIR in $CONF)"
command -v zenity >/dev/null || say "note: zenity not found – the text chat prompt (tap PTT key) needs it"

# ---------------------------------------------------------------- Animates
if [ ! -f "$APP/Animates.exe" ]; then
    [ -n "$INSTALLER" ] && [ -f "$INSTALLER" ] || die "Animates not installed yet: ./install.sh /path/to/Animates.exe"
    say "Installing Animates into $PREFIX (the app will auto-start and hang afterwards – that's expected)"
    mkdir -p "$PREFIX"
    STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
        timeout 900 "$PROTON_DIR/proton" run "$(readlink -f "$INSTALLER")" || true
    for _ in $(seq 1 120); do [ -f "$APP/Animates.exe" ] && break; sleep 2; done
    sleep 10
    WINEPREFIX="$PFX" "$PROTON_DIR/files/bin/wineserver" -k || true
    [ -f "$APP/Animates.exe" ] || die "installation failed – see the Proton output above"
fi
say "Animates installed: $(grep -o '<version>[^<]*' "$APP/sq.version" | cut -d'>' -f2)"

# ---------------------------------------------------------------- WebView2 109 fixed runtime
if [ ! -f "$PFX/drive_c/webview2fixed/msedgewebview2.exe" ]; then
    say "Downloading WebView2 fixed runtime 109.0.1518.78 from nuget.org (~200 MB)"
    NUPKG="$ANIMATES_HOME/downloads/webview2.runtime.x64.109.0.1518.78.nupkg"
    [ -f "$NUPKG" ] || curl -fL -o "$NUPKG" \
        https://api.nuget.org/v3-flatcontainer/webview2.runtime.x64/109.0.1518.78/webview2.runtime.x64.109.0.1518.78.nupkg
    tmp=$(mktemp -d); unzip -q "$NUPKG" 'contentFiles/any/any/WebView2/*' -d "$tmp"
    cp -a "$tmp/contentFiles/any/any/WebView2" "$PFX/drive_c/webview2fixed"; rm -rf "$tmp"
fi

# ---------------------------------------------------------------- patches + dxcore
say "Patching Animates (Wine compatibility, see the original guide)"
ANIMATES_HOME="$ANIMATES_HOME" "$REPO/bin/patch-animates.py"
if [ "$DML_MODE" != cpu ]; then
    say "Building patched dxcore.dll (GPU/DirectML for the animation AI)"
    python3 "$REPO/dxcore/make-dxcore.py" "$PROTON_DIR" "$ANIMATES_HOME/dxcore.dll"
fi

# ---------------------------------------------------------------- alpha layer
if [ "$WAYLAND" = 1 ]; then
    say "Building the transparency Vulkan layer"
    "$REPO/alpha-layer/build.sh"
fi

# ---------------------------------------------------------------- helpers
say "Python venv for push-to-talk / ani-ctl"
[ -x "$ANIMATES_HOME/venv/bin/python" ] || python3 -m venv "$ANIMATES_HOME/venv"
"$ANIMATES_HOME/venv/bin/pip" install -q -r "$REPO/ptt/requirements.txt"

mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.config/systemd/user"
ln -sf "$REPO/bin/ani-ctl" "$HOME/.local/bin/ani-ctl"
ln -sf "$REPO/bin/run-animates.sh" "$HOME/.local/bin/animates"
cat > "$HOME/.local/share/applications/animates.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Animates (Ani)
Comment=AI desktop companion via GE-Proton
Exec=$REPO/bin/run-animates.sh
Icon=avatar-default
Categories=Game;
Terminal=false
EOF

if id -nG | grep -qw input; then
    if ask "Install the push-to-talk service (hold $PTT_KEY = voice, tap = text chat)?"; then
        sed -e "s|@VENV@|$ANIMATES_HOME/venv|" -e "s|@REPO@|$REPO|" "$REPO/systemd/animates-ptt.service.in" \
            > "$HOME/.config/systemd/user/animates-ptt.service"
        systemctl --user daemon-reload; systemctl --user enable --now animates-ptt.service
    fi
else
    say "Push-to-talk skipped: your user is not in the 'input' group (sudo usermod -aG input \$USER, re-login)"
fi

if command -v niri >/dev/null; then
    if ask "Install the niri workspace-follow service (Ani follows you across workspaces)?"; then
        sed -e "s|@REPO@|$REPO|" "$REPO/systemd/animates-follow.service.in" \
            > "$HOME/.config/systemd/user/animates-follow.service"
        systemctl --user daemon-reload; systemctl --user enable --now animates-follow.service
    fi
    say "Add the window rules from $REPO/niri/animates.kdl to your niri config (not done automatically)."
fi

say "Done. Start Ani with 'animates' or from your launcher. Log: $ANIMATES_HOME/last-run.log"
