#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1:-"$ROOT/native/macos/out"}
mkdir -p "$OUT"
clang -dynamiclib -arch arm64 -mmacosx-version-min=12.0 -O2 \
  -Wall -Wextra -Werror -fvisibility=hidden \
  -o "$OUT/libmoon-sweepers-client.dylib" \
  "$ROOT/native/macos/runtime_probe.c" \
  "$ROOT/native/macos/client_network.c" \
  "$ROOT/native/macos/mono_game_bridge.c" \
  "$ROOT/native/macos/password_prompt.m" \
  -framework AppKit -framework Foundation
file "$OUT/libmoon-sweepers-client.dylib"
shasum -a 256 "$OUT/libmoon-sweepers-client.dylib"
