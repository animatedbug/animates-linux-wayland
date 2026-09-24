#!/usr/bin/env bash
# Build the premultiplied-alpha Vulkan layer and register it as a user-level
# implicit layer (only active when ANIMATES_ALPHA_LAYER=1). Needs vulkan-headers.
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HOME/.local/lib/animates-alpha}"
mkdir -p "$OUT" "$HOME/.local/share/vulkan/implicit_layer.d"
cc -O2 -Wall -fPIC -shared -fvisibility=hidden -o "$OUT/libanimates_alpha.so" "$DIR/animates_alpha.c" -lpthread
sed "s|@LIBRARY_PATH@|$OUT/libanimates_alpha.so|" "$DIR/animates_alpha.json.in" \
    > "$HOME/.local/share/vulkan/implicit_layer.d/animates_alpha.json"
echo "alpha layer installed: $OUT/libanimates_alpha.so"
