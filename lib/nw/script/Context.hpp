#pragma once

#include "../kernel/Resources.hpp"
#include "../resources/Resource.hpp"
#include "Token.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>
#include <string>

namespace nw::script {

struct Nss;
struct StructDecl;
struct Type;

struct IncludeStackEntry {
    String resref;
    Nss* script = nullptr;
};

struct Context {
    Context(Vector<String> include_paths = {}, String command_script = "nwscript");
    virtual ~Context() = default;

    // Resource Loading / Dependency Tracking
    Vector<String> include_paths_;
    absl::flat_hash_map<Resource, std::unique_ptr<Nss>> dependencies_;
    Vector<IncludeStackEntry> include_stack_;
    Vector<IncludeStackEntry> preprocessed_;
    kernel::Resources resman_;

    /// Adds include path to internal resman
    void add_include_path(const std::filesystem::path& path);

    /// Gets a script from internal resman
    Nss* get(Resref resref, bool command_script = false);

    /// Gets command script
    const Nss* command_script() const noexcept { return command_script_; }

    // Spec ..
    String command_script_name_;
    Nss* command_script_ = nullptr;

    // Type Tracking
    absl::flat_hash_map<String, size_t> type_map_;
    Vector<String> type_array_;
    Vector<StructDecl*> struct_stack_;

    virtual void register_default_types();
    virtual void register_engine_types();
    size_t type_id(StringView type_name, bool define = false);
    size_t type_id(Type type_name, bool define = false);
    StringView type_name(size_t type_id);
    virtual size_t type_check_binary_op(NssToken op, size_t lhs, size_t rhs);
    virtual bool is_type_convertible(size_t lhs, size_t rhs);

    // Error/Warning Tracking
    virtual void lexical_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range);
    virtual void parse_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range);
    virtual void semantic_diagnostic(Nss* script, StringView msg, bool is_warning, SourceRange range);
};

} // nw::script
