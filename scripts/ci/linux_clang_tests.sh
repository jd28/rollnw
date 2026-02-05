#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

cd "${repo_root}"

export NWN_ROOT="${NWN_ROOT:-${repo_root}/nwn}"

"${script_dir}/ensure_nwn.sh"
"${script_dir}/run_preset.sh" ci-linux-clang ci-linux-clang ci-linux-clang rollnw_test
