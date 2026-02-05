#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

build_dir="${BUILD_DIR:-${repo_root}/build-ci-linux-clang-fuzz}"

declare -a targets=(
  "smalls_parse"
  "smalls_resolve"
  "smalls_vm"
)

if [[ ! -d "${build_dir}" ]]; then
  echo "build dir not found: ${build_dir}" >&2
  echo "Tip: run: cmake --preset ci-linux-clang-fuzz && cmake --build --preset ci-linux-clang-fuzz --target fuzz_smalls_parse fuzz_smalls_resolve fuzz_smalls_vm" >&2
  exit 2
fi

if [[ -z "${ASAN_OPTIONS:-}" ]]; then
  export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0"
fi

tmp_root="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_root}"
}
trap cleanup EXIT

for t in "${targets[@]}"; do
  corpus_dir="${repo_root}/fuzz/corpus/${t}"
  fuzzer_bin="${build_dir}/fuzz/fuzz_${t}"
  out_dir="${tmp_root}/${t}"

  if [[ ! -x "${fuzzer_bin}" ]]; then
    echo "fuzzer not found: ${fuzzer_bin}" >&2
    exit 2
  fi

  if [[ ! -d "${corpus_dir}" ]]; then
    echo "corpus dir not found: ${corpus_dir}" >&2
    exit 2
  fi

  mkdir -p "${out_dir}"

  echo "Merging corpus: ${t}"
  "${fuzzer_bin}" -merge=1 "${out_dir}" "${corpus_dir}"

  rm -rf "${corpus_dir}"/*
  cp -a "${out_dir}"/. "${corpus_dir}"/

  echo "OK: ${t} -> $(ls -1 "${corpus_dir}" | wc -l) files"
done
