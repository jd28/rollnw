#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../resources/ResourceData.hpp"
#include "../util/ByteArray.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace nw::script {

struct Nss;

struct ScriptContext {
    Nss* get(Resref resref);

    absl::flat_hash_map<Resource, std::unique_ptr<Nss>> scripts;
    std::vector<std::string> include_stack;
};

struct Nss {
    explicit Nss(const std::filesystem::path& filename);
    explicit Nss(std::string_view script);
    explicit Nss(ResourceData data);

    /// Returns all transitive dependencies
    std::set<std::string> dependencies() const;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept;

    /// Script name
    std::string_view name() const noexcept;

    /// Parses script file
    void parse();

    /// Gets parser
    NssParser& parser();

    /// Gets parser
    const NssParser& parser() const;

    bool process_includes(Nss* parent = nullptr);

    /// Gets parsed AST
    Ast& ast();

    /// Gets parsed ast
    const Ast& ast() const;

    /// Gets text of script
    std::string_view text() const noexcept;

    /// Returns how many warnings were found during parsing
    size_t warnings() const noexcept;

private:
    ScriptContext ctx_;
    ResourceData data_;
    NssParser parser_;
    Ast ast_;
};

} // namespace nw::script
