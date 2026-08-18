#pragma once

#include <nw/smalls/runtime.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

// Parsed once from the host document's imports-only script block. All strings
// are owned here; no token, AST, or StringView from the scratch parse escapes.
struct RmlSmallsImportScope {
    struct Symbol {
        std::string local_name;
        std::string module_path;
        std::string export_name;
    };

    struct Alias {
        std::string local_name;
        std::string module_path;
    };

    std::vector<Symbol> symbols;
    std::vector<Alias> aliases;
};

enum class RmlSmallsArgumentKind : uint8_t {
    event,
    integer,
    floating,
    boolean,
    string,
};

// A listener-owned, allocation-free representation for primitive arguments,
// except for the owned bytes required by a bound string literal or constant.
struct RmlSmallsBoundArgument {
    RmlSmallsArgumentKind kind = RmlSmallsArgumentKind::event;
    nw::smalls::TypeID type_id = nw::smalls::invalid_type_id;
    int32_t integer = 0;
    float floating = 0.0f;
    bool boolean = false;
    std::string string;
};

struct RmlSmallsResolvedCall {
    std::string module_path;
    std::string function_name;
    nw::smalls::BytecodeModule* module = nullptr;
    const nw::smalls::CompiledFunction* function = nullptr;
    std::vector<RmlSmallsBoundArgument> arguments;

    [[nodiscard]] bool valid() const noexcept { return module && function; }
};

// Parses an imports-only host block in a nested TLS scratch scope. Input is
// rejected above the documented bounds; error receives a source-local message.
bool parse_rml_smalls_import_scope(nw::smalls::Runtime& runtime,
    std::string_view source, RmlSmallsImportScope& output, std::string& error);

// Parses one restricted direct-call expression and resolves it exclusively in
// the supplied host scope. Runtime-owned pointers are valid only for the current
// kernel service generation.
bool resolve_rml_smalls_call(nw::smalls::Runtime& runtime,
    const RmlSmallsImportScope& scope, std::string_view expression,
    RmlSmallsResolvedCall& output, std::string& error);

// Materializes a bound primitive for one dispatch. String results must be added
// to Runtime::ScopedRoots before any subsequent ScriptHeap allocation.
nw::smalls::Value materialize_rml_smalls_argument(nw::smalls::Runtime& runtime,
    const RmlSmallsBoundArgument& argument);

} // namespace nw::toolset
