#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../resources/ResourceData.hpp"
#include "Context.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <set>
#include <string>

namespace nw::script {

struct Nss;

struct Nss {
    explicit Nss(const std::filesystem::path& filename, Context* ctx);
    explicit Nss(std::string_view script, Context* ctx);
    explicit Nss(ResourceData data, Context* ctx);

    /// Add export
    void add_export(std::string_view name, Declaration* decl);

    /// Script context
    Context* ctx() const;

    /// Returns all transitive dependencies
    std::set<std::string> dependencies() const;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept { return errors_; }

    /// Increments error count
    void increment_errors() noexcept { ++errors_; }

    /// Increments warning count
    void increment_warnings() noexcept { ++warnings_; }

    /// Locate export
    /// @note This function is not recursive
    Declaration* locate_export(std::string_view name);

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
    absl::flat_hash_map<std::string, Declaration*> exports_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
    bool resolved_ = false;
    bool parsed_ = false;
};

} // namespace nw::script
