#pragma once

#include "../resources/Resource.hpp"
#include "Token.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>
#include <string>

namespace nw::script {

struct Nss;
struct StructDecl;
struct Type;

struct Context {
    Context();
    virtual ~Context() = default;

    // Dependency Tracking
    absl::flat_hash_map<Resource, std::unique_ptr<Nss>> dependencies_;
    std::vector<std::string> include_stack_;
    Nss* get(Resref resref, std::shared_ptr<Context> ctx);

    // Type Tracking
    absl::flat_hash_map<std::string, size_t> type_map_;
    std::vector<std::string> type_array_;
    std::vector<StructDecl*> struct_stack_;

    virtual void register_default_types();
    size_t type_id(std::string_view type_name);
    size_t type_id(Type type_name);
    std::string_view type_name(size_t type_id);

    // Error/Warning Tracking
    virtual void lexical_error(Nss* script, std::string_view msg, SourceLocation loc);
    virtual void lexical_warning(Nss* script, std::string_view msg, SourceLocation loc);

    virtual void parse_error(Nss* script, std::string_view msg, SourceLocation loc = {});
    virtual void parse_warning(Nss* script, std::string_view msg, SourceLocation loc = {});

    virtual void semantic_error(Nss* script, std::string_view msg, SourceLocation loc = {});
    virtual void semantic_warning(Nss* script, std::string_view msg, SourceLocation loc = {});
};

} // nw::script