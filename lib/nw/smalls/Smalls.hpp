#pragma once

#include "Lexer.hpp"
#include "Parser.hpp"
#include "types.hpp"

#include "../resources/assets.hpp"
#include "Context.hpp"
#include "Diagnostic.hpp"

#include <absl/container/flat_hash_map.h>
#include <immer/map.hpp>

#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace nw::smalls {

struct NormalizedTypeExpr {
    enum class Kind : uint8_t {
        unknown,
        concrete,
        generic_param,
        applied,
    };

    Kind kind = Kind::unknown;
    TypeID concrete_type = invalid_type_id;
    uint16_t generic_param_index = 0;
    String applied_name;
    Vector<NormalizedTypeExpr> applied_args;
};

struct FunctionExportAbi {
    uint16_t min_arity = 0;
    uint16_t max_arity = 0;
    uint16_t generic_arity = 0;
    Vector<NormalizedTypeExpr> param_types;
    Vector<uint8_t> param_has_default;
    NormalizedTypeExpr return_type;
    String call_target;
};

/// Any exported symbol from a script.
///
/// Invariants:
/// - `decl` is optional and may be null after AST compaction.
/// - Runtime/compile paths should prefer metadata (`kind`, `type_id`, ABI fields)
///   when `decl` is unavailable.
/// - Tooling in `DebugLevel::full` may still rely on `decl` for rich symbol data.
struct Export {
    enum class Kind : uint8_t {
        unknown,
        variable,
        function,
        type,
        module_alias,
    };

    Declaration* decl = nullptr; ///< Optional declaration pointer while AST is retained.
    Kind kind = Kind::unknown;
    String provider_module;
    TypeID type_id = invalid_type_id;
    bool is_generic = false;
    bool is_native = false;
    bool has_defaults = false;
    std::optional<FunctionExportAbi> function_abi;
};

enum struct SymbolKind {
    variable,
    function,
    type,
    param,
    field,
};

/// Info regarding a particular symbol somewhere in a source file
struct Symbol {
    AstNode* node = nullptr;           ///< AstNode if symbol is used in a variable expression
    const Declaration* decl = nullptr; ///< Original declaration
    String comment;                    ///< Comment on original declaration, in case of functions decl is preferred over definition
    String type;                       ///< Type of the symbol
    SymbolKind kind;                   ///< The kind of symbol
    const Script* provider = nullptr;  ///< What script this symbol is from, i.e. "nwscript"
    StringView view;                   ///< View of declaration
};

struct InlayHint {
    String message;
    SourcePosition position;
};

struct SignatureHelp {
    const Declaration* decl = nullptr;    // Function declaration | definition
    const CallExpression* expr = nullptr; // Function declaration | definition
    size_t active_param = 0;
};

struct CompletionContext {
    void add(Symbol symbol)
    {
        String id = symbol.decl->identifier();
        auto it = completion_map.find(id);
        // This isn't super ideal, but there are only collisions between structs
        // and everything else.
        if (completion_map.find(id) == std::end(completion_map)
            || completions[it->second].kind != symbol.kind) {
            completion_map.insert({id, completions.size()});
            completions.push_back(std::move(symbol));
        }
    }

    std::unordered_map<String, size_t> completion_map;
    Vector<Symbol> completions;
};

struct Script {
    explicit Script(const std::filesystem::path& filename, Context* ctx);
    Script(StringView name, StringView script, Context* ctx);
    explicit Script(ResourceData data, Context* ctx);

    /// Add diagnostic to script
    void add_diagnostic(Diagnostic diagnostic);

    /// Gets parsed AST
    Ast& ast();

    /// Gets parsed ast
    const Ast& ast() const;

    /// Returns true if semantic tooling APIs are enabled.
    bool semantic_tooling_enabled() const noexcept;

    /// Discards source text retained by this script.
    void discard_source();

    /// Discards parsed AST and symbol table state.
    void discard_ast();

    /// Returns true if parsed AST can be discarded safely.
    bool can_discard_ast() const;

    /// Returns true if parsed AST has been discarded.
    bool ast_discarded() const noexcept { return ast_discarded_; }

    /// Returns true if script defines any generic declarations.
    bool has_generic_templates() const;

    /// Generates a list of potential completions (excluding dependencies)
    void complete(const String& needle, CompletionContext& out, bool no_filter = false) const;

    /// Get all completions (including dependencies)
    void complete_at(const String& needle, size_t line, size_t character, CompletionContext& out,
        bool no_filter = false);

    /// Get all completions (including dependencies)
    void complete_dot(const String& needle, size_t line, size_t character, Vector<Symbol>& out,
        bool no_filter = false);

    /// Script context
    Context* ctx() const;

    /// Converts declaration to symbol
    /// @note Declaration must be in script
    Symbol declaration_to_symbol(const Declaration* decl) const;

    /// Converts exported symbol metadata to symbol.
    Symbol export_to_symbol(StringView name, const Export& exp) const;

    /// Returns all transitive dependencies in 'preprocessed' order,
    /// i.e. dependencies()[n] was include before dependencies()[n+1]
    Vector<String> dependencies() const;

    /// Gets script diagnostics
    const Vector<Diagnostic>& diagnostics() const noexcept;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept { return errors_; }

    /// Table of symbols exported from script
    immer::map<String, Export> exports() const noexcept { return symbol_table_; }

    /// Count of symbols exported from script
    size_t export_count() const noexcept { return symbol_table_.size(); }

    /// Increments error count
    void increment_errors() noexcept { ++errors_; }

    /// Increments warning count
    void increment_warnings() noexcept { ++warnings_; }

    Vector<InlayHint> inlay_hints(SourceRange range);

    /// Locate export, i.e. a top level symbol
    Symbol locate_export(const String& symbol, bool search_dependencies = false) const;

    /// Locate symbol in source file
    Symbol locate_symbol(const String& symbol, size_t line, size_t character);

    /// Script name
    StringView name() const noexcept;

    /// Parses script file
    void parse();

    /// Resolves and type checks the Ast
    void resolve();

    /// Sets a scripts name
    void set_name(const String& new_name);

    SignatureHelp signature_help(size_t line, size_t character);

    /// Gets text of script
    StringView text() const noexcept;

    /// Gets a view of source file in specified range
    StringView view_from_range(SourceRange range) const noexcept;

    /// Returns how many warnings were found during parsing
    size_t warnings() const noexcept { return warnings_; }

private:
    bool emit_tooling_disabled_diagnostic(StringView api_name) const;

    Context* ctx_ = nullptr;
    ResourceData data_;
    StringView text_;
    Ast ast_;
    immer::map<String, Export> symbol_table_;
    Vector<String> dependency_paths_;
    Vector<Diagnostic> diagnostics_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
    bool resolved_ = false;
    bool parsed_ = false;
    bool includes_processed_ = false;
    bool ast_discarded_ = false;
    mutable bool tooling_disabled_diagnostic_emitted_ = false;
};

} // namespace nw::smalls
