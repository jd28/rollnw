#include "smalls_fuzz_common.hpp"

#include "rml_smalls_expression_binding.hpp"
#include "smalls_rmlui.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

using namespace std::literals;

constexpr auto kInputSeparator = "\n%%\n"sv;
constexpr size_t kMaxFuzzInputBytes = 16 * 1024 + 1024 + 16;

struct BindingFuzzState {
    nw::smalls::Runtime* runtime = nullptr;
    nw::toolset::RmlSmallsImportScope fixed_scope;
    bool initialized = false;
};

BindingFuzzState& fuzz_state()
{
    static BindingFuzzState state;
    return state;
}

bool report_script_failure(std::string_view stage, const nw::smalls::Script* script)
{
    fmt::print(stderr, "RML-Smalls fuzzer initialization failed at {}\n", stage);
    if (!script) {
        fmt::print(stderr, "  module was not loaded\n");
        return false;
    }

    for (const auto& diagnostic : script->diagnostics()) {
        fmt::print(stderr, "  {}\n", diagnostic.message);
    }
    return false;
}

bool initialize_fuzz_state()
{
    auto& state = fuzz_state();
    if (state.initialized) {
        return true;
    }

    nw::smalls::fuzz::ensure_kernel_started();
    auto& runtime = nw::kernel::runtime();
    nw::toolset::register_smalls_rmlui(runtime);
    auto* rmlui = runtime.load_module("core.rmlui"sv);
    if (!rmlui || !runtime.get_or_compile_module(rmlui)) {
        return report_script_failure("core.rmlui", rmlui);
    }

    auto* provider = runtime.load_module_from_source("fuzz.rml_binding_provider"sv, R"(
from core.rmlui import { Event, Command };

type PaletteIndex(int);
const palette_index = PaletteIndex(42);

fn handle(event: Event, value: int, scale: float, enabled: bool, label: string): array!(Command) {
    return {};
}
fn handle_newtype(value: PaletteIndex) { }
fn handle_void() { }
fn bad_return(value: int): int { return value; }
)");
    if (!provider || !runtime.get_or_compile_module(provider)) {
        return report_script_failure("provider module", provider);
    }

    constexpr auto fixed_imports = R"(
from fuzz.rml_binding_provider import {
    handle, handle_newtype, handle_void, bad_return, palette_index
};
import fuzz.rml_binding_provider as Provider;
)"sv;
    std::string error;
    if (!nw::toolset::parse_rml_smalls_import_scope(
            runtime, fixed_imports, state.fixed_scope, error)) {
        fmt::print(stderr,
            "RML-Smalls fuzzer initialization failed at fixed import scope\n  {}\n",
            error);
        return false;
    }

    state.runtime = &runtime;
    state.initialized = true;
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!initialize_fuzz_state()) {
        std::abort();
    }
    auto& state = fuzz_state();
    if (!state.initialized || !state.runtime) {
        std::abort();
    }
    if (size == 0) {
        return 0;
    }

    size = std::min(size, kMaxFuzzInputBytes);
    const std::string_view input{
        reinterpret_cast<const char*>(data), size};
    const auto separator = input.find(kInputSeparator);
    const auto import_source = separator == std::string_view::npos
        ? input
        : input.substr(0, separator);
    const auto expression = separator == std::string_view::npos
        ? input
        : input.substr(separator + kInputSeparator.size());

    nw::toolset::RmlSmallsImportScope parsed_scope;
    nw::toolset::RmlSmallsResolvedCall call;
    std::string error;

    const bool parsed = nw::toolset::parse_rml_smalls_import_scope(
        *state.runtime, import_source, parsed_scope, error);
    nw::toolset::resolve_rml_smalls_call(
        *state.runtime, state.fixed_scope, expression, call, error);
    if (parsed) {
        nw::toolset::resolve_rml_smalls_call(
            *state.runtime, parsed_scope, expression, call, error);
    }

    return 0;
}
