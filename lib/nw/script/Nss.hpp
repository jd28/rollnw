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
    type
};

/// Info regarding a particular symbol somewhere in a source file
struct Symbol {
    AstNode* node = nullptr;     ///< AstNode if symbol is used in a variable expression
    Declaration* decl = nullptr; ///< Original declaration
    std::string comment;         ///< Comment on original declaration, in case of functions decl is preferred over definition
    std::string type;            ///< Type of the symbol
    SymbolKind kind;             ///< The kind of symbol
    std::string provider;        ///< What script this symbol is from, i.e. "nwscript"
    std::string_view view;       ///< View of declaration
};

struct Nss {
    explicit Nss(const std::filesystem::path& filename, Context* ctx, bool command_script = false);
    explicit Nss(std::string_view script, Context* ctx, bool command_script = false);
    explicit Nss(ResourceData data, Context* ctx, bool command_script = false);

    /// Add diagnostic to script
    void add_diagnostic(Diagnostic diagnostic);

    /// Generates a list of potential completions (excluding dependencies)
    void complete(const std::string& needle, std::vector<std::string>& out) const;

    /// Get all completions (including dependencies)
    void complete_at(const std::string& needle, size_t line, size_t character, std::vector<std::string>& out) const;

    /// Script context
    Context* ctx() const;

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

    /// Is script a command script
    bool is_command_script() const noexcept { return is_command_script_; }

    /// Locate export, i.e. a top level symbols
    Symbol locate_export(const std::string& symbol, bool is_type, bool search_dependencies = false);

    /// Locate symbol in source file
    Symbol locate_symbol(const std::string& symbol, size_t line, size_t character);

    /// Script name
    std::string_view name() const noexcept;

    /// Parses script file
    void parse();

    /// Gets parser
    NssParser& parser();

    /// Gets parser
    const NssParser& parser() const;

    /// Process includes recursively
    void process_includes(Nss* parent = nullptr);

    /// Gets parsed AST
    Ast& ast();

    /// Gets parsed ast
    const Ast& ast() const;

    /// Resolves and type checks the Ast
    void resolve();

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
    NssParser parser_;
    Ast ast_;
    immer::map<std::string, Export> symbol_table_;
    std::vector<Diagnostic> diagnostics_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
    bool resolved_ = false;
    bool parsed_ = false;
    bool is_command_script_ = false;
};

} // namespace nw::script
