#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build-ci-linux-clang-asan-ubsan"

cd "${repo_root}"

export NWN_ROOT="${NWN_ROOT:-${repo_root}/nwn}"

if [[ -z "${ASAN_SYMBOLIZER_PATH:-}" ]]; then
  if command -v llvm-symbolizer >/dev/null 2>&1; then
    export ASAN_SYMBOLIZER_PATH
    ASAN_SYMBOLIZER_PATH="$(command -v llvm-symbolizer)"
  elif command -v clang >/dev/null 2>&1; then
    clang_symbolizer="$(clang --print-prog-name=llvm-symbolizer 2>/dev/null || true)"
    if [[ -n "${clang_symbolizer}" && "${clang_symbolizer}" != "llvm-symbolizer" && -x "${clang_symbolizer}" ]]; then
      export ASAN_SYMBOLIZER_PATH
      ASAN_SYMBOLIZER_PATH="${clang_symbolizer}"
    fi
  fi
fi

if [[ -z "${ASAN_SYMBOLIZER_PATH:-}" ]]; then
  echo "[asan-ubsan] llvm-symbolizer not found; sanitizer stacks may be unsymbolized"
fi

export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

"${script_dir}/ensure_nwn.sh"

cmake --preset ci-linux-clang-asan-ubsan
cmake --build --preset ci-linux-clang-asan-ubsan --target rollnw_test

if ! ctest --preset ci-linux-clang-asan-ubsan --output-on-failure; then
  echo "::group::rerun failing tests verbosely"
  failed_tests_file="${build_dir}/Testing/Temporary/LastTestsFailed.log"

  if [[ -f "${failed_tests_file}" ]]; then
    while IFS=: read -r _ test_name; do
      if [[ -z "${test_name}" ]]; then
        continue
      fi
      echo "[asan-ubsan] Re-running failed test: ${test_name}"
      ctest --preset ci-linux-clang-asan-ubsan -R "^${test_name}$" -V --output-on-failure || true
    done < "${failed_tests_file}"
  else
    echo "[asan-ubsan] No LastTestsFailed.log found at ${failed_tests_file}"
  fi

  echo "::endgroup::"
  exit 1
fi
