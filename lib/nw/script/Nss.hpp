#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../resources/ResourceData.hpp"
#include "Context.hpp"
#include "Diagnostic.hpp"

#include <absl/container/flat_hash_map.h>
#include <immer/map.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace nw::script {

/// Any exported symbol from an NWScript file
struct Export {
    Declaration* decl = nullptr; ///< Variable or function
    StructDecl* type = nullptr;  ///< Struct
    int command_id = -1;         ///< Command ID for command script, i.e. 'nwscript'
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
    std::string comment;               ///< Comment on original declaration, in case of functions decl is preferred over definition
    std::string type;                  ///< Type of the symbol
    SymbolKind kind;                   ///< The kind of symbol
    std::string provider;              ///< What script this symbol is from, i.e. "nwscript"
    std::string_view view;             ///< View of declaration
};

struct InlayHint {
    std::string message;
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
        std::string id = symbol.decl->identifier();
        auto it = completion_map.find(id);
        // This isn't super ideal, but there are only collisions between structs
        // and everything else.
        if (completion_map.find(id) == std::end(completion_map)
            || completions[it->second].kind != symbol.kind) {
            completion_map.insert({id, completions.size()});
            completions.push_back(std::move(symbol));
        }
    }

    std::unordered_map<std::string, size_t> completion_map;
    std::vector<Symbol> completions;
};

struct Nss {
    explicit Nss(const std::filesystem::path& filename, Context* ctx, bool command_script = false);
    explicit Nss(std::string_view script, Context* ctx, bool command_script = false);
    explicit Nss(ResourceData data, Context* ctx, bool command_script = false);

    /// Add diagnostic to script
    void add_diagnostic(Diagnostic diagnostic);

    /// Gets parsed AST
    Ast& ast();

    /// Gets parsed ast
    const Ast& ast() const;

    /// Generates a list of potential completions (excluding dependencies)
    void complete(const std::string& needle, CompletionContext& out) const;

    /// Get all completions (including dependencies)
    void complete_at(const std::string& needle, size_t line, size_t character, CompletionContext& out);

    /// Get all completions (including dependencies)
    void complete_dot(const std::string& needle, size_t line, size_t character, std::vector<Symbol>& out);

    /// Script context
    Context* ctx() const;

    /// Converts declaration to symbol
    /// @note Declaration must be in script
    Symbol declaration_to_symbol(const Declaration* decl) const;

    /// Returns all transitive dependencies
    std::set<std::string> dependencies() const;

    /// Gets script diagnostics
    const std::vector<Diagnostic>& diagnostics() const noexcept;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept { return errors_; }

    /// Table of symbols exported from script
    immer::map<std::string, Export> exports() const noexcept { return symbol_table_; }

    /// Count of symbols exported from script
    size_t export_count() const noexcept { return symbol_table_.size(); }

    /// Increments error count
    void increment_errors() noexcept { ++errors_; }

    /// Increments warning count
    void increment_warnings() noexcept { ++warnings_; }

    std::vector<InlayHint> inlay_hints(SourceRange range);

    /// Is script a command script
    bool is_command_script() const noexcept { return is_command_script_; }

    /// Locate export, i.e. a top level symbols
    Symbol locate_export(const std::string& symbol, bool is_type, bool search_dependencies = false) const;

    /// Locate symbol in source file
    Symbol locate_symbol(const std::string& symbol, size_t line, size_t character);

    /// Script name
    std::string_view name() const noexcept;

    /// Parses script file
    void parse();

    /// Process includes recursively
    void process_includes(Nss* parent = nullptr);

    /// Resolves and type checks the Ast
    void resolve();

    /// Sets a scripts name
    void set_name(const std::string& new_name);

    SignatureHelp signature_help(size_t line, size_t character);

    /// Gets text of script
    std::string_view text() const noexcept;

    /// Gets a view of source file in specified range
    std::string_view view_from_range(SourceRange range) const noexcept;

    /// Returns how many warnings were found during parsing
    size_t warnings() const noexcept { return warnings_; }

private:
    Context* ctx_ = nullptr;
    ResourceData data_;
    std::string_view text_;
    Ast ast_;
    immer::map<std::string, Export> symbol_table_;
    std::vector<Diagnostic> diagnostics_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
    bool resolved_ = false;
    bool is_command_script_ = false;
};

} // namespace nw::script
