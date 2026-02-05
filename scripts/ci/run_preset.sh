#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <configure_preset> <build_preset> <test_preset> [target]" >&2
}

if [[ $# -lt 3 ]]; then
  usage
  exit 2
fi

configure_preset="$1"
build_preset="$2"
test_preset="$3"
target="${4:-}"

cmake --preset "${configure_preset}"

if [[ -n "${target}" ]]; then
  cmake --build --preset "${build_preset}" --target "${target}"
else
  cmake --build --preset "${build_preset}"
fi

ctest --preset "${test_preset}"
