#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

cd "${repo_root}"

export NWN_ROOT="${NWN_ROOT:-${repo_root}/nwn}"

# libFuzzer is typically built with ASan; we currently defer LeakSanitizer.
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

"${script_dir}/ensure_nwn.sh"

declare -a configure_args=(--preset ci-linux-clang-fuzz)
clang_resource_dir="$(clang++ -print-resource-dir)"
clang_target="$(clang++ -dumpmachine)"
clang_arch="${clang_target%%-*}"
clang_fuzzer_runtime="${clang_resource_dir}/lib/linux/libclang_rt.fuzzer-${clang_arch}.a"
patched_resource_dir="${repo_root}/build-ci-linux-clang-fuzz/clang-resource-patched"

if [[ -f "${clang_fuzzer_runtime}" ]]; then
  symbol_dump="$(mktemp)"
  nm "${clang_fuzzer_runtime}" > "${symbol_dump}"
  if grep -q ' T __dynamic_cast$' "${symbol_dump}"; then
    cmake -E remove_directory "${patched_resource_dir}"
    cmake -E copy_directory "${clang_resource_dir}" "${patched_resource_dir}"
    llvm-objcopy --localize-symbol=__dynamic_cast \
      "${patched_resource_dir}/lib/linux/libclang_rt.fuzzer-${clang_arch}.a"
    configure_args+=("-DROLLNW_FUZZER_CLANG_RESOURCE_DIR=${patched_resource_dir}")
    echo "Using a temporary libFuzzer runtime without its incompatible __dynamic_cast override"
  else
    configure_args+=("-DROLLNW_FUZZER_CLANG_RESOURCE_DIR=")
  fi
  rm -f "${symbol_dump}"
else
  configure_args+=("-DROLLNW_FUZZER_CLANG_RESOURCE_DIR=")
fi

cmake "${configure_args[@]}"
cmake --build --preset ci-linux-clang-fuzz --target fuzz_format_parsers fuzz_mdl fuzz_rml_smalls_binding fuzz_text_mdl fuzz_smalls_parse fuzz_smalls_resolve fuzz_smalls_vm

build_dir="${repo_root}/build-ci-linux-clang-fuzz"
seed_corpus_root="${repo_root}/fuzz/corpus"
run_corpus_root="${repo_root}/tmp/fuzz-corpus-run"
artifact_dir="${repo_root}/tmp/fuzz-artifacts/"
dict_path="${repo_root}/fuzz/dict/smalls.dict"

max_total_time="${FUZZ_MAX_TOTAL_TIME:-60}"
timeout="${FUZZ_TIMEOUT:-3}"

mkdir -p "${artifact_dir}"

rm -rf "${run_corpus_root}"
corpora=(format_parsers mdl rml_smalls_binding text_mdl smalls_parse smalls_resolve smalls_vm)
for corpus in "${corpora[@]}"; do
  mkdir -p "${run_corpus_root}/${corpus}"
  if [[ -d "${seed_corpus_root}/${corpus}" ]]; then
    cp -a "${seed_corpus_root}/${corpus}/." "${run_corpus_root}/${corpus}/"
  else
    echo "No seed corpus for ${corpus}; starting empty."
  fi
done

dict_arg=()
if [[ -f "${dict_path}" ]]; then
  dict_arg=("-dict=${dict_path}")
fi

"${build_dir}/fuzz/fuzz_smalls_parse" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_parse"
"${build_dir}/fuzz/fuzz_smalls_resolve" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_resolve"
"${build_dir}/fuzz/fuzz_smalls_vm" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/smalls_vm"
"${build_dir}/fuzz/fuzz_rml_smalls_binding" "${dict_arg[@]}" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/rml_smalls_binding"
"${build_dir}/fuzz/fuzz_format_parsers" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/format_parsers"
"${build_dir}/fuzz/fuzz_mdl" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/mdl"
"${build_dir}/fuzz/fuzz_text_mdl" -artifact_prefix="${artifact_dir}" -max_total_time="${max_total_time}" -timeout="${timeout}" "${run_corpus_root}/text_mdl"
