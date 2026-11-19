#!/usr/bin/env bash
# Builds tools/media/tonearm-sequence against an existing CMake build (Linux).
# Usage: tools/media/build-sequence.sh [build-dir]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build}"
c++ -O2 -std=gnu++20 -pthread -DTONEARM_DATA_DIR="\"$root/data\"" -I"$root/src" \
  $(pkg-config --cflags pangocairo fontconfig) \
  "$root/tools/media/sequence.cpp" "$root/tools/tonearm-render/png.cpp" \
  "$build/libtonearm_media.a" "$build/libtonearm_core.a" \
  $(pkg-config --libs pangocairo fontconfig) -o "$build/tonearm-sequence"
echo "$build/tonearm-sequence"
