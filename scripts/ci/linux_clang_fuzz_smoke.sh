#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

cd "${repo_root}"

export NWN_ROOT="${NWN_ROOT:-${repo_root}/nwn}"

# libFuzzer is typically built with ASan; we currently defer LeakSanitizer.
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0}"

"${script_dir}/ensure_nwn.sh"

cmake --preset ci-linux-clang-fuzz
cmake --build --preset ci-linux-clang-fuzz --target fuzz_smalls_parse fuzz_smalls_resolve fuzz_smalls_vm

build_dir="${repo_root}/build-ci-linux-clang-fuzz"
seed_corpus_root="${repo_root}/fuzz/corpus"
run_corpus_root="${repo_root}/tmp/fuzz-corpus-run"
artifact_dir="${repo_root}/tmp/fuzz-artifacts/"
dict_path="${repo_root}/fuzz/dict/smalls.dict"

max_total_time="${FUZZ_MAX_TOTAL_TIME:-60}"
timeout="${FUZZ_TIMEOUT:-3}"

mkdir -p "${artifact_dir}"

rm -rf "${run_corpus_root}"
mkdir -p "${run_corpus_root}/smalls_parse" "${run_corpus_root}/smalls_resolve" "${run_corpus_root}/smalls_vm"

cp -a "${seed_corpus_root}/smalls_parse/." "${run_corpus_root}/smalls_parse/" || true
cp -a "${seed_corpus_root}/smalls_resolve/." "${run_corpus_root}/smalls_resolve/" || true
cp -a "${seed_corpus_root}/smalls_vm/." "${run_corpus_root}/smalls_vm/" || true

dict_arg=()
if [[ -f "${dict_path}" ]]; then
  dict_arg=("-dict=${dict_path}")
fi

"${build_dir}/fuzz/fuzz_smalls_parse" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_parse"
"${build_dir}/fuzz/fuzz_smalls_resolve" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_resolve"
"${build_dir}/fuzz/fuzz_smalls_vm" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_vm"
