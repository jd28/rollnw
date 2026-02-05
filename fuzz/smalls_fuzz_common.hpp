#pragma once

#include "nw/kernel/Config.hpp"
#include "nw/kernel/Kernel.hpp"
#include "nw/smalls/Context.hpp"
#include "nw/smalls/Smalls.hpp"
#include "nw/smalls/runtime.hpp"

#include <nowide/cstdlib.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace nw::smalls::fuzz {

struct FuzzContext final : public nw::smalls::Context {
    nw::MemoryArena arena_storage{nw::MB(64)};
    nw::MemoryScope scope_storage{&arena_storage};

    void record_diagnostic(nw::smalls::Script* script, nw::StringView msg, bool is_warning, nw::smalls::SourceRange range, nw::smalls::DiagnosticType type)
    {
        if (!script) { return; }

        constexpr size_t max_total_diags = 128;
        constexpr size_t max_diags_to_store = 32;

        if (is_warning) {
            script->increment_warnings();
        } else {
            script->increment_errors();
        }

        if (script->errors() + script->warnings() > max_total_diags) {
            throw std::runtime_error("fuzz: too many diagnostics");
        }

        if (script->diagnostics().size() >= max_diags_to_store) { return; }

        nw::smalls::Diagnostic result;
        result.type = type;
        result.severity = is_warning ? nw::smalls::DiagnosticSeverity::warning : nw::smalls::DiagnosticSeverity::error;
        result.script = nw::String(script->name());
        result.message = nw::String(msg);
        result.location = range;
        script->add_diagnostic(std::move(result));
    }

    void lexical_diagnostic(nw::smalls::Script* script, nw::StringView msg, bool is_warning, nw::smalls::SourceRange range) override
    {
        record_diagnostic(script, msg, is_warning, range, nw::smalls::DiagnosticType::lexical);
    }

    void parse_diagnostic(nw::smalls::Script* script, nw::StringView msg, bool is_warning, nw::smalls::SourceRange range) override
    {
        record_diagnostic(script, msg, is_warning, range, nw::smalls::DiagnosticType::parse);
    }

    void semantic_diagnostic(nw::smalls::Script* script, nw::StringView msg, bool is_warning, nw::smalls::SourceRange range) override
    {
        record_diagnostic(script, msg, is_warning, range, nw::smalls::DiagnosticType::semantic);
    }

    FuzzContext()
    {
        arena = &arena_storage;
        scope = &scope_storage;
        config.use_color = false;

        limits.max_ast_nodes = 10000;
        limits.max_parse_depth = 64;
        limits.max_type_instantiations = 2048;
        limits.max_generic_function_instantiations = 512;
    }
};

inline void ensure_kernel_started()
{
    namespace fs = std::filesystem;
    static bool started = false;
    if (!started) {
        const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path();
        const fs::path user_root = repo_root / "tests" / "test_data" / "user";

        fs::path install_root;
        if (auto p = nowide::getenv("NWN_ROOT")) {
            install_root = fs::path(p);
        } else {
            install_root = repo_root / "nwn";
        }

        nw::kernel::config().set_paths(install_root, user_root);
        nw::kernel::config().initialize();
        nw::kernel::services().start();

        // Enable filesystem module loading for stdlib imports.
        // scripts/core has a package.json so modules resolve as core.*
        const fs::path stdlib_root = repo_root / "lib" / "nw" / "smalls" / "scripts" / "core";
        nw::kernel::runtime().add_module_path(stdlib_root);

        started = true;
    }
}

inline std::string to_source(const uint8_t* data, size_t size)
{
    constexpr size_t max_size = 64 * 1024;
    if (size > max_size) {
        size = max_size;
    }
    return std::string(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + size);
}

inline nw::smalls::Script make_script(std::string_view source, nw::smalls::Context* ctx)
{
    return nw::smalls::Script("fuzz", source, ctx);
}

} // namespace nw::smalls::fuzz
