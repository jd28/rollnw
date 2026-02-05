#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

cd "${repo_root}"

export NWN_ROOT="${NWN_ROOT:-${repo_root}/nwn}"

"${script_dir}/ensure_nwn.sh"
ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0}" \
  "${script_dir}/run_preset.sh" ci-linux-clang-asan-ubsan ci-linux-clang-asan-ubsan ci-linux-clang-asan-ubsan rollnw_test
