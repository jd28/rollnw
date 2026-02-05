#pragma once

#include "../kernel/Memory.hpp"
#include "types.hpp"

#include <cstdint>

namespace nw::smalls {

struct Script;
struct SourceRange;

enum class DebugLevel : uint8_t {
    none,
    source_map,
    full,
};

struct DiagnosticConfig {
    DebugLevel debug_level = DebugLevel::source_map;
    bool use_color = true;
};

struct Limits {
    // 0 means unlimited.
    uint32_t max_ast_nodes = 0;
    uint32_t max_parse_depth = 0;
    uint32_t max_type_instantiations = 0;
    uint32_t max_generic_function_instantiations = 0;
};

struct Context {
    virtual ~Context() = default;

    virtual void lexical_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range);
    virtual void parse_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range);
    virtual void semantic_diagnostic(Script* script, StringView msg, bool is_warning, SourceRange range);

    MemoryArena* arena = nullptr;
    MemoryScope* scope = nullptr;
    DiagnosticConfig config{};
    Limits limits{};
};

} // nw::smalls
