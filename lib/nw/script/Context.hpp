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
    Context(std::string command_script = "nwscript");
    virtual ~Context() = default;

    // Dependency Tracking
    absl::flat_hash_map<Resource, std::unique_ptr<Nss>> dependencies_;
    std::vector<std::string> include_stack_;
    Nss* get(Resref resref, bool command_script = false);

    // Spec
    std::string command_script_name_;
    Nss* command_script_ = nullptr;

    // Type Tracking
    absl::flat_hash_map<std::string, size_t> type_map_;
    std::vector<std::string> type_array_;
    std::vector<StructDecl*> struct_stack_;

    virtual void register_default_types();
    virtual void register_engine_types();
    size_t type_id(std::string_view type_name, bool define = false);
    size_t type_id(Type type_name, bool define = false);
    std::string_view type_name(size_t type_id);
    virtual bool type_check_binary_op(NssToken op, size_t lhs, size_t rhs);
    virtual bool is_type_convertible(size_t lhs, size_t rhs);

    // Error/Warning Tracking
    virtual void lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc);
    virtual void parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc);
    virtual void semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceLocation loc);
};

} // nw::script
