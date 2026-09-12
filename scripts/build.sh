#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# CI uses the same entry point, with CMake coordinating platform packaging.
if [[ "${1:-}" == "--ci-phase" ]]; then
  [[ $# -ge 2 ]] || { echo "--ci-phase requires configure, build, or package" >&2; exit 2; }
  phase="$2"
  shift 2
  build_dir="$ROOT/build/ci"
  if [[ "${1:-}" == "--build-dir" ]]; then
    [[ $# -ge 2 ]] || { echo "--build-dir requires a path" >&2; exit 2; }
    build_dir="$2"
    shift 2
  fi
  [[ $# == 0 ]] || { echo "Unexpected CI arguments: $*" >&2; exit 2; }
  exec cmake "-DNEO_CI_PHASE=$phase" "-DNEO_CI_BUILD_DIR=$build_dir" \
    -P "$ROOT/scripts/ci/RunNative.cmake"
fi
BUILD_DIR="${NEOTOOLKIT_BUILD_DIR:-$ROOT/build/native}"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEOTOOLKIT_BUILD_GUI=ON "$@"
cmake --build "$BUILD_DIR" --parallel "${NEOTOOLKIT_BUILD_JOBS:-3}"
printf 'Built workspace in %s\n' "$BUILD_DIR"
