#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../resources/ResourceData.hpp"
#include "../util/ByteArray.hpp"
#include "Context.hpp"

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace nw::script {

struct Nss;

struct Nss {
    explicit Nss(const std::filesystem::path& filename,
        std::shared_ptr<Context> ctx = std::make_shared<Context>());
    explicit Nss(std::string_view script,
        std::shared_ptr<Context> ctx = std::make_shared<Context>());
    explicit Nss(ResourceData data,
        std::shared_ptr<Context> ctx = std::make_shared<Context>());

    /// Script context
    std::shared_ptr<Context> ctx() const;

    /// Returns all transitive dependencies
    std::set<std::string> dependencies() const;

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept { return errors_; }

    /// Increments error count
    void increment_errors() noexcept { ++errors_; }

    /// Increments warning count
    void increment_warnings() noexcept { ++warnings_; }


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

    /// Gets text of script
    std::string_view text() const noexcept;

    /// Returns how many warnings were found during parsing
    size_t warnings() const noexcept { return warnings_; }

private:
    std::shared_ptr<Context> ctx_;
    ResourceData data_;
    NssParser parser_;
    Ast ast_;
    size_t errors_ = 0;
    size_t warnings_ = 0;
};

} // namespace nw::script
