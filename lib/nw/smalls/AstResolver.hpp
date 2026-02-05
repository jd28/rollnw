#pragma once

#include "Ast.hpp"
#include "Context.hpp"
#include "Smalls.hpp"
#include "VirtualMachineIntrinsics.hpp"
#include "runtime.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <immer/map.hpp>

#include <unordered_map>
#include <utility>

namespace nw::smalls {

struct NameResolver;
struct TypeResolver;
struct Validator;

struct ScopeDecl {
    bool ready = false;
    Declaration* decl = nullptr;
};

struct AstResolver {
    using ScopeMap = std::unordered_map<String, ScopeDecl>;
    using ScopeStack = Vector<ScopeMap>;
    using EnvStack = Vector<immer::map<String, Export>>;
    using GlobalDeclMap = absl::flat_hash_map<String, Declaration*>;
    using DeclProviderMap = absl::flat_hash_map<const Declaration*, Script*>;

    AstResolver(Script* parent, Context* ctx);
    ~AstResolver() = default;

    void resolve(Ast* script);

    void begin_scope();
    void end_scope();
    void declare_global(StringView name, Declaration* decl);
    void declare_local(Token token, Declaration* decl);
    void record_decl_provider(const Declaration* decl, Script* provider);
    Script* provider_for_decl(const Declaration* decl) const;
    immer::map<String, Export> symbol_table() const;

    const Declaration* resolve(StringView token, SourceRange range);
    TypeID resolve_type(TypeExpression* type_name, SourceRange range);
    TypeID resolve_type(StringView type_name, SourceRange range);
    TypeID get_or_instantiate_type(TypeID generic_type_id, const Vector<TypeID>& type_args, SourceRange range);
    std::optional<int32_t> eval_const_int(Expression* expr, SourceRange range);

    void report(SourceRange range, StringView message, bool is_warning) const;
    void error(SourceRange range, StringView message) const;
    void warn(SourceRange range, StringView message) const;

    template <typename... Args>
    void errorf(SourceRange range, const char* fmt_str, Args&&... args) const
    {
        error(range, fmt::format(fmt::runtime(fmt_str), std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warnf(SourceRange range, const char* fmt_str, Args&&... args) const
    {
        warn(range, fmt::format(fmt::runtime(fmt_str), std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error_at(const AstNode* node, const char* fmt_str, Args&&... args) const
    {
        if (!node) { return; }
        errorf(node->range_, fmt_str, std::forward<Args>(args)...);
    }

    bool in_function_context() const noexcept;
    void clear_type_substitutions();
    void set_type_substitutions(const absl::flat_hash_map<String, TypeID>& subs);
    TypeID current_expected_type() const;
    TypeID current_return_type() const;
    bool infer_return_enabled() const;
    void merge_inferred_return(TypeID actual_return, SourceRange range);
    Vector<StringView> collect_env_names(const immer::map<String, Export>& env) const;
    Vector<StringView> collect_module_exports(const Script* module) const;
    const Annotation* get_annotation(Declaration* decl, StringView annotation_name) const;
    bool has_annotation(Declaration* decl, StringView annotation_name) const;
    void record_map_key_check(TypeID key_type, SourceRange range);
    TypeID handle_type_param(StringView name, SourceRange range);
    std::optional<IntrinsicId> resolve_intrinsic_id(FunctionDefinition* decl, SourceRange range, bool report_error);
    size_t find_declaration_scope(const String& name) const;
    void set_active_visitor(BaseVisitor* visitor) { active_visitor_ = visitor; }
    void sync_operator_alias_summaries();

    Script* parent_ = nullptr;
    Context* ctx_ = nullptr;
    GlobalDeclMap global_decls_;
    DeclProviderMap decl_providers_;
    ScopeStack scope_stack_;
    EnvStack env_stack_;
    int loop_stack_ = 0;
    int switch_stack_ = 0;
    StructDecl* struct_decl_stack_ = nullptr;
    FunctionDefinition* func_def_stack_ = nullptr;
    size_t function_context_depth_ = 0;
    struct ReturnContext {
        TypeID declared = invalid_type_id;
        TypeID inferred = invalid_type_id;
        bool infer = false;
    };
    Vector<ReturnContext> return_context_stack_;
    Script* core_prelude_ = nullptr;
    Script* user_prelude_ = nullptr;

    struct FunctionContext {
        AstNode* node;
        uint32_t depth;
        size_t scope_base;
    };
    Vector<FunctionContext> function_stack_;
    LambdaExpression* current_lambda_ = nullptr;

    absl::flat_hash_set<String> current_type_params_;
    FunctionDefinition* resolving_generic_func_ = nullptr;
    Declaration* resolving_generic_type_ = nullptr;
    absl::flat_hash_map<String, TypeID> type_substitutions_;

    struct MapKeyCheck {
        TypeID key_type = invalid_type_id;
        SourceRange range;
    };
    Vector<MapKeyCheck> map_key_checks_;
    Vector<TypeID> expected_type_stack_;

    uint32_t type_instantiation_count_ = 0;

    struct OperatorAliasSummary {
        bool has_eq = false;            ///< Any eq (explicit or default structural)
        bool has_hash = false;          ///< Any hash (explicit or default structural)
        bool has_lt = false;            ///< Any lt (explicit or default structural)
        bool has_explicit_eq = false;   ///< From [[operator(eq)]]
        bool has_explicit_hash = false; ///< From [[operator(hash)]]
        bool has_explicit_lt = false;   ///< From [[operator(lt)]]
    };
    absl::flat_hash_map<TypeID, OperatorAliasSummary> operator_alias_summary_;

    BaseVisitor* active_visitor_ = nullptr;
};

} // namespace nw::smalls
