#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${NEOTOOLKIT_BUILD_DIR:-$ROOT/build/native}"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEOTOOLKIT_BUILD_GUI=ON "$@"
cmake --build "$BUILD_DIR" --parallel "${NEOTOOLKIT_BUILD_JOBS:-3}"
printf 'Built workspace in %s\n' "$BUILD_DIR"
