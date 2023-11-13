#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../resources/ResourceData.hpp"
#include "Context.hpp"

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

struct Nss {
    explicit Nss(const std::filesystem::path& filename, Context* ctx);
    explicit Nss(std::string_view script, Context* ctx);
    explicit Nss(ResourceData data, Context* ctx);

    /// Generates a list of potential completions from exports
    /// @note This is just a baby step.
    std::vector<std::string> complete(const std::string& needle);

    /// Script context
    Context* ctx() const;

    /// Returns all transitive dependencies
    std::set<std::string> dependencies() const;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept { return errors_; }

    size_t export_count() const noexcept { return symbol_table_.size(); }

    /// Increments error count
    void increment_errors() noexcept { ++errors_; }

    /// Increments warning count
    void increment_warnings() noexcept { ++warnings_; }

    /// Locate export
    /// @note This function is not recursive
    Declaration* locate_export(const std::string& name, bool is_type);

    /// Script name
    std::string_view name() const noexcept;

    /// Parses script file
    void parse();

    /// Gets parser
    NssParser& parser();

    /// Gets parser
    const NssParser& parser() const;

    /// Process includes recursively
    bool process_includes(Nss* parent = nullptr);

    /// Gets parsed AST
    Ast& ast();

    /// Gets parsed ast
    const Ast& ast() const;

    /// Resolves and type checks the Ast
    void resolve();

    /// Gets text of script
    std::string_view text() const noexcept;

    /// Returns how many warnings were found during parsing
    size_t warnings() const noexcept { return warnings_; }

private:
    Context* ctx_ = nullptr;
    ResourceData data_;
    NssParser parser_;
    Ast ast_;
    immer::map<std::string, Export> symbol_table_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
    bool resolved_ = false;
    bool parsed_ = false;
};

} // namespace nw::script
