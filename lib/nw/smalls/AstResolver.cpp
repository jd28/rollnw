#include "AstResolver.hpp"

#include "AstConstEvaluator.hpp"
#include "NameResolver.hpp"
#include "TypeResolver.hpp"
#include "Validator.hpp"

namespace nw::smalls {

namespace {

Export::Kind export_kind_for_decl(const Declaration* decl)
{
    if (!decl) {
        return Export::Kind::unknown;
    }
    if (dynamic_cast<const FunctionDefinition*>(decl)) {
        return Export::Kind::function;
    }
    if (dynamic_cast<const StructDecl*>(decl)
        || dynamic_cast<const SumDecl*>(decl)
        || dynamic_cast<const TypeAlias*>(decl)
        || dynamic_cast<const NewtypeDecl*>(decl)
        || dynamic_cast<const OpaqueTypeDecl*>(decl)) {
        return Export::Kind::type;
    }
    if (dynamic_cast<const AliasedImportDecl*>(decl)) {
        return Export::Kind::module_alias;
    }
    return Export::Kind::variable;
}

std::optional<StringView> extract_identifier(Expression* expr)
{
    if (auto ident = as_identifier(expr)) {
        return ident->ident.loc.view();
    }
    if (auto path = as_path(expr)) {
        if (auto last_ident = path->last_identifier()) {
            return last_ident->ident.loc.view();
        }
    }
    return std::nullopt;
}

Vector<StringView> extract_qualified_path(Expression* expr)
{
    Vector<StringView> path;
    if (auto ident = dynamic_cast<IdentifierExpression*>(expr)) {
        path.push_back(ident->ident.loc.view());
    } else if (auto path_expr = dynamic_cast<PathExpression*>(expr)) {
        for (auto* part : path_expr->parts) {
            auto ident_part = dynamic_cast<IdentifierExpression*>(part);
            if (!ident_part) {
                return {};
            }
            path.push_back(ident_part->ident.loc.view());
        }
    }
    return path;
}

} // namespace

AstResolver::AstResolver(Script* parent, Context* ctx)
    : parent_{parent}
    , ctx_{ctx}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    env_stack_.push_back({});

    auto& rt = nw::kernel::runtime();
    if (parent_ && parent_->name() == "core.prelude") {
        core_prelude_ = parent_;
    } else {
        core_prelude_ = rt.core_prelude();
    }
    user_prelude_ = rt.user_prelude();
}

void AstResolver::resolve(Ast* script)
{
    NameResolver name_resolver{*this};
    set_active_visitor(&name_resolver);
    name_resolver.visit(script);

    TypeResolver type_resolver{*this};
    set_active_visitor(&type_resolver);
    type_resolver.visit(script);

    nw::kernel::runtime().register_default_struct_operators();
    sync_operator_alias_summaries();

    Validator validator{*this};
    set_active_visitor(&validator);
    validator.visit(script);

    set_active_visitor(nullptr);
}

void AstResolver::sync_operator_alias_summaries()
{
    auto& rt = nw::kernel::runtime();
    const size_t type_count = rt.type_table_.types_.size();
    for (size_t i = 0; i < type_count; ++i) {
        TypeID type_id{static_cast<int32_t>(i + 1)};
        auto info = rt.get_operator_alias_info(type_id);
        if (info.has_eq || info.has_lt || info.has_hash) {
            auto& summary = operator_alias_summary_[type_id];
            summary.has_eq |= info.has_eq;
            summary.has_lt |= info.has_lt;
            summary.has_hash |= info.has_hash;
            summary.has_explicit_eq |= info.has_explicit_eq;
            summary.has_explicit_lt |= info.has_explicit_lt;
            summary.has_explicit_hash |= info.has_explicit_hash;
        }
    }
}

void AstResolver::report(SourceRange range, StringView message, bool is_warning) const
{
    if (!ctx_) { return; }
    ctx_->semantic_diagnostic(parent_, message, is_warning, range);
}

void AstResolver::error(SourceRange range, StringView message) const
{
    report(range, message, false);
}

void AstResolver::warn(SourceRange range, StringView message) const
{
    report(range, message, true);
}

bool AstResolver::in_function_context() const noexcept
{
    return function_context_depth_ > 0;
}

TypeID AstResolver::current_return_type() const
{
    if (return_context_stack_.empty()) { return invalid_type_id; }
    return return_context_stack_.back().declared;
}

bool AstResolver::infer_return_enabled() const
{
    return !return_context_stack_.empty() && return_context_stack_.back().infer;
}

void AstResolver::merge_inferred_return(TypeID actual_return, SourceRange range)
{
    if (return_context_stack_.empty()) {
        return;
    }

    TypeID& inferred = return_context_stack_.back().inferred;
    if (inferred == invalid_type_id) {
        inferred = actual_return;
        return;
    }

    auto& rt = nw::kernel::runtime();
    if (rt.is_type_convertible(inferred, actual_return)) {
        return;
    }
    if (rt.is_type_convertible(actual_return, inferred)) {
        inferred = actual_return;
        return;
    }

    errorf(range, "returning type '{}' does not match inferred return type '{}'", actual_return, inferred);
}

bool AstResolver::has_annotation(Declaration* decl, StringView annotation_name) const
{
    for (const auto& ann : decl->annotations_) {
        if (ann.name.loc.view() == annotation_name) { return true; }
    }
    return false;
}

const Annotation* AstResolver::get_annotation(Declaration* decl, StringView annotation_name) const
{
    for (const auto& ann : decl->annotations_) {
        if (ann.name.loc.view() == annotation_name) {
            return &ann;
        }
    }
    return nullptr;
}

std::optional<IntrinsicId> AstResolver::resolve_intrinsic_id(FunctionDefinition* decl, SourceRange range, bool report_error)
{
    const Annotation* annotation = get_annotation(decl, "intrinsic");
    if (!annotation) { return std::nullopt; }

    if (annotation->args.size() != 1 || annotation->args[0].is_named) {
        if (report_error) { error(range, "[[intrinsic]] requires a single string argument"); }
        return std::nullopt;
    }

    auto* literal = dynamic_cast<LiteralExpression*>(annotation->args[0].value);
    if (!literal || (literal->literal.type != TokenType::STRING_LITERAL && literal->literal.type != TokenType::STRING_RAW_LITERAL)) {
        if (report_error) { error(range, "[[intrinsic]] requires a string literal argument"); }
        return std::nullopt;
    }

    auto name = literal->literal.loc.view();
    auto intrinsic_id = intrinsic_id_from_string(name);
    if (!intrinsic_id) {
        if (report_error) { errorf(range, "unknown intrinsic '{}'", name); }
        return std::nullopt;
    }

    return intrinsic_id;
}

void AstResolver::set_type_substitutions(const absl::flat_hash_map<String, TypeID>& subs)
{
    type_substitutions_ = subs;
}

void AstResolver::clear_type_substitutions()
{
    type_substitutions_.clear();
}

void AstResolver::record_map_key_check(TypeID key_type, SourceRange range)
{
    map_key_checks_.push_back({key_type, range});
}

TypeID AstResolver::current_expected_type() const
{
    return expected_type_stack_.empty() ? invalid_type_id : expected_type_stack_.back();
}

TypeID AstResolver::handle_type_param(StringView name, SourceRange range)
{
    auto sub_it = type_substitutions_.find(name);
    if (sub_it != type_substitutions_.end()) {
        return sub_it->second;
    }

    if (!resolving_generic_func_ && !resolving_generic_type_) {
        errorf(range, "type parameter '{}' used outside of generic context", name);
        return invalid_type_id;
    }

    if (current_type_params_.find(name) == current_type_params_.end()) {
        current_type_params_.insert(String(name));
        if (resolving_generic_func_) {
            resolving_generic_func_->type_params.push_back(String(name));
        } else if (auto sum = dynamic_cast<SumDecl*>(resolving_generic_type_)) {
            sum->type_params.push_back(String(name));
        } else if (auto st = dynamic_cast<StructDecl*>(resolving_generic_type_)) {
            st->type_params.push_back(String(name));
        } else if (auto ta = dynamic_cast<TypeAlias*>(resolving_generic_type_)) {
            ta->type_params.push_back(String(name));
        }
    }

    return nw::kernel::runtime().any_type();
}

Vector<StringView> AstResolver::collect_env_names(const immer::map<String, Export>& env) const
{
    Vector<StringView> names;
    for (const auto& [name, _] : env) {
        names.push_back(name);
    }
    return names;
}

Vector<StringView> AstResolver::collect_module_exports(const Script* module) const
{
    Vector<StringView> names;
    if (!module) { return names; }
    for (const auto& [name, _] : module->exports()) {
        names.push_back(name);
    }
    return names;
}

void AstResolver::begin_scope()
{
    scope_stack_.push_back(ScopeMap{});
    env_stack_.push_back(env_stack_.back());
}

void AstResolver::end_scope()
{
    scope_stack_.pop_back();
    env_stack_.pop_back();
}

void AstResolver::declare_global(StringView name, Declaration* decl)
{
    auto s = String(name);
    auto it = global_decls_.find(s);
    if (it != global_decls_.end()) {
        errorf(SourceRange{}, "declaring '{}' twice at module level", name);
        return;
    }
    global_decls_.insert({s, decl});

    record_decl_provider(decl, parent_);

    auto& env = env_stack_.back();
    Export exp;
    exp.decl = decl;
    exp.kind = export_kind_for_decl(decl);
    exp.provider_module = parent_ ? String(parent_->name()) : String{};
    exp.type_id = decl ? decl->type_id_ : invalid_type_id;
    env_stack_.back() = env.set(s, exp);
}

void AstResolver::record_decl_provider(const Declaration* decl, Script* provider)
{
    if (!decl || !provider) {
        return;
    }
    if (decl_providers_.find(decl) != decl_providers_.end()) {
        return;
    }
    decl_providers_.insert({decl, provider});
}

Script* AstResolver::provider_for_decl(const Declaration* decl) const
{
    if (!decl) {
        return nullptr;
    }
    auto it = decl_providers_.find(decl);
    if (it != decl_providers_.end()) {
        return it->second;
    }
    return parent_;
}

void AstResolver::declare_local(Token token, Declaration* decl)
{
    auto s = String(token.loc.view());
    auto it = scope_stack_.back().find(s);
    if (it != std::end(scope_stack_.back())) {
        errorf(token.loc.range, "declaring '{}' in the same scope twice", token.loc.view());
        return;
    }
    scope_stack_.back().insert({s, {true, decl}});

    auto& env = env_stack_.back();
    Export exp;
    exp.decl = decl;
    exp.kind = export_kind_for_decl(decl);
    exp.provider_module = parent_ ? String(parent_->name()) : String{};
    exp.type_id = decl ? decl->type_id_ : invalid_type_id;
    env_stack_.back() = env.set(s, exp);
}

immer::map<String, Export> AstResolver::symbol_table() const
{
    return env_stack_[0];
}

const Declaration* AstResolver::resolve(StringView token, SourceRange range)
{
    auto s = String(token);

    for (const auto& map : reverse(scope_stack_)) {
        auto it = map.find(s);
        if (it != std::end(map)) {
            return it->second.decl;
        }
    }

    auto git = global_decls_.find(s);
    if (git != global_decls_.end()) {
        if (struct_decl_stack_ && git->second == struct_decl_stack_) {
            errorf(range, "recursive use of type '{}' in declaration", token);
        }
        return git->second;
    }

    if (user_prelude_ && user_prelude_ != parent_) {
        auto exp = user_prelude_->exports().find(s);
        if (exp && exp->decl) {
            record_decl_provider(exp->decl, user_prelude_);
            return exp->decl;
        }
    }

    if (core_prelude_ && core_prelude_ != parent_) {
        auto exp = core_prelude_->exports().find(s);
        if (exp && exp->decl) {
            record_decl_provider(exp->decl, core_prelude_);
            return exp->decl;
        }
    }

    return nullptr;
}

size_t AstResolver::find_declaration_scope(const String& name) const
{
    for (size_t i = scope_stack_.size(); i > 0; --i) {
        const auto& scope = scope_stack_[i - 1];
        if (scope.find(name) != scope.end()) {
            return i - 1;
        }
    }
    return SIZE_MAX;
}

TypeID AstResolver::resolve_type(StringView type_name, SourceRange range)
{
    if (!type_name.empty() && type_name[0] == '$') {
        return handle_type_param(type_name, range);
    }

    auto type_decl = resolve(type_name, range);
    if (type_decl) {
        return type_decl->type_id_;
    }

    if (!env_stack_.empty()) {
        auto exp = env_stack_.back().find(String(type_name));
        if (exp && exp->kind == Export::Kind::type) {
            TypeID tid = exp->type_id;
            if (tid == invalid_type_id && exp->decl) {
                tid = exp->decl->type_id_;
            }
            if (tid != invalid_type_id) {
                return tid;
            }
        }
    }

    return nw::kernel::runtime().type_id(type_name);
}

TypeID AstResolver::get_or_instantiate_type(TypeID generic_type_id, const Vector<TypeID>& type_args, SourceRange range)
{
    uint32_t limit = ctx_ ? ctx_->limits.max_type_instantiations : 0;
    if (limit != 0 && type_instantiation_count_ + 1 > limit) {
        error(range, "type instantiation limit exceeded");
        return invalid_type_id;
    }
    ++type_instantiation_count_;
    return nw::kernel::runtime().get_or_instantiate_type(generic_type_id, type_args);
}

std::optional<int32_t> AstResolver::eval_const_int(Expression* expr, SourceRange range)
{
    if (!expr) {
        return std::nullopt;
    }

    if (!active_visitor_) {
        return std::nullopt;
    }

    expr->accept(active_visitor_);

    AstConstEvaluator evaluator(parent_, expr);
    if (evaluator.failed_ || evaluator.result_.empty()) {
        return std::nullopt;
    }

    auto& result = evaluator.result_.back();
    if (result.type_id == nw::kernel::runtime().int_type()) {
        return result.data.ival;
    }

    return std::nullopt;
}

TypeID AstResolver::resolve_type(TypeExpression* type_name, SourceRange range)
{
    if (!type_name) {
        error(range, "invalid type name");
        return invalid_type_id;
    }

    if (type_name->is_function_type) {
        auto& rt = nw::kernel::runtime();

        Vector<TypeID> param_types;
        for (auto* param : type_name->params) {
            auto* param_type = dynamic_cast<TypeExpression*>(param);
            if (!param_type) {
                error(range, "function parameter must be a type");
                return invalid_type_id;
            }
            TypeID param_tid = resolve_type(param_type, param->range_);
            if (param_tid == invalid_type_id) {
                return invalid_type_id;
            }
            param_types.push_back(param_tid);
        }

        TypeID return_type = rt.void_type();
        if (type_name->fn_return_type) {
            return_type = resolve_type(type_name->fn_return_type, type_name->fn_return_type->range_);
            if (return_type == invalid_type_id) {
                return invalid_type_id;
            }
        }

        TypeID result = rt.register_function_type(param_types, return_type);
        type_name->type_id_ = result;
        return result;
    }

    if (!type_name->name && !type_name->params.empty()) {
        Vector<TypeID> element_types;
        for (auto* param : type_name->params) {
            auto* param_type = dynamic_cast<TypeExpression*>(param);
            if (!param_type) {
                error(range, "tuple element must be a type");
                return invalid_type_id;
            }
            TypeID elem_tid = resolve_type(param_type, param->range_);
            if (elem_tid == invalid_type_id) {
                return invalid_type_id;
            }
            element_types.push_back(elem_tid);
        }
        return nw::kernel::runtime().register_tuple_type(element_types);
    }

    if (!type_name->name) {
        error(range, "invalid type name");
        return invalid_type_id;
    }

    if (type_name->is_fixed_array) {
        if (type_name->params.size() != 1) {
            error(range, "fixed array must have exactly one size parameter");
            return invalid_type_id;
        }

        auto& rt = nw::kernel::runtime();

        auto* elem_type_expr = dynamic_cast<TypeExpression*>(type_name->name);
        if (!elem_type_expr) {
            error(range, "fixed array element must be a type");
            return invalid_type_id;
        }

        TypeID element_type = resolve_type(elem_type_expr, range);
        if (element_type == invalid_type_id) {
            return invalid_type_id;
        }

        const Type* elem_type_obj = rt.get_type(element_type);
        if (!elem_type_obj) {
            error(range, "invalid fixed array element type");
            return invalid_type_id;
        }

        auto array_size_opt = eval_const_int(type_name->params[0], range);
        if (!array_size_opt) {
            error(range, "fixed array size must be a constant integer expression");
            return invalid_type_id;
        }

        int32_t array_size = *array_size_opt;
        if (array_size <= 0) {
            errorf(range, "fixed array size must be positive, got {}", array_size);
            return invalid_type_id;
        }

        Type fixed_array_type{
            .name = nw::kernel::strings().intern(fmt::format("{}[{}]",
                elem_type_obj->name.view(), array_size)),
            .type_params = {element_type, array_size},
            .type_kind = TK_fixed_array,
            .size = static_cast<uint32_t>(elem_type_obj->size * array_size),
            .alignment = elem_type_obj->alignment,
            // Fixed arrays are inline value types. They contain heap refs if their element is a heap
            // reference type (e.g., string) or a value type that contains heap refs (e.g., value_type
            // structs, nested fixed arrays).
            .contains_heap_refs = rt.type_table_.is_heap_type(element_type) || elem_type_obj->contains_heap_refs,
        };

        TypeID result = rt.register_type(fixed_array_type.name.view(), fixed_array_type);
        type_name->type_id_ = result;
        type_name->qualified_name = String(fixed_array_type.name.view());
        return result;
    }

    if (!type_name->params.empty()) {
        auto base_name_opt = extract_identifier(type_name->name);
        if (!base_name_opt) {
            error(range, "expected identifier for generic type");
            return invalid_type_id;
        }
        auto base_name = String(*base_name_opt);
        auto& rt = nw::kernel::runtime();

        if (base_name == "array") {
            if (type_name->params.size() < 1 || type_name->params.size() > 2) {
                errorf(range, "array requires 1 or 2 type parameters, got {}", type_name->params.size());
                return invalid_type_id;
            }

            auto* type_param = dynamic_cast<TypeExpression*>(type_name->params[0]);
            if (!type_param) {
                error(range, "array element type must be a type");
                return invalid_type_id;
            }

            TypeID element_type = resolve_type(type_param, range);
            if (element_type == invalid_type_id) {
                return invalid_type_id;
            }

            const Type* elem_type_obj = rt.get_type(element_type);
            if (!elem_type_obj) {
                error(range, "invalid array element type");
                return invalid_type_id;
            }

            if (type_name->params.size() == 2) {
                auto array_size_opt = eval_const_int(type_name->params[1], range);
                if (!array_size_opt) {
                    error(range, "array size must be a constant integer expression");
                    return invalid_type_id;
                }

                int32_t array_size = *array_size_opt;
                if (array_size <= 0) {
                    errorf(range, "array size must be positive, got {}", array_size);
                    return invalid_type_id;
                }

                Type array_type{
                    .name = nw::kernel::strings().intern(fmt::format("array!({}, {})",
                        elem_type_obj->name.view(), array_size)),
                    .type_params = {element_type, array_size},
                    .type_kind = TK_array,
                    .size = static_cast<uint32_t>(elem_type_obj->size * array_size),
                    .alignment = elem_type_obj->alignment,
                };

                TypeID result = rt.register_type(array_type.name.view(), array_type);
                type_name->type_id_ = result;
                type_name->qualified_name = String(array_type.name.view());
                return result;
            } else {
                Type array_type{
                    .name = nw::kernel::strings().intern(fmt::format("array!({})", elem_type_obj->name.view())),
                    .type_params = {element_type, TypeParam{}},
                    .type_kind = TK_array,
                    .size = sizeof(HeapPtr),
                    .alignment = alignof(HeapPtr),
                };

                TypeID result = rt.register_type(array_type.name.view(), array_type);
                type_name->type_id_ = result;
                type_name->qualified_name = String(array_type.name.view());
                return result;
            }
        } else if (base_name == "map") {
            if (type_name->params.size() != 2) {
                error(range, "map requires exactly 2 type parameters");
                return invalid_type_id;
            }

            auto* key_param = dynamic_cast<TypeExpression*>(type_name->params[0]);
            auto* val_param = dynamic_cast<TypeExpression*>(type_name->params[1]);

            if (!key_param || !val_param) {
                error(range, "map key and value types must be types");
                return invalid_type_id;
            }

            TypeID key_type = resolve_type(key_param, range);
            TypeID value_type = resolve_type(val_param, range);

            if (key_type == invalid_type_id || value_type == invalid_type_id) {
                return invalid_type_id;
            }

            record_map_key_check(key_type, key_param->range_);

            const Type* key_type_obj = rt.get_type(key_type);
            const Type* val_type_obj = rt.get_type(value_type);

            if (!key_type_obj || !val_type_obj) {
                error(range, "invalid map key or value type");
                return invalid_type_id;
            }

            Type map_type{
                .name = nw::kernel::strings().intern(fmt::format("map!({}, {})",
                    key_type_obj->name.view(), val_type_obj->name.view())),
                .type_params = {key_type, value_type},
                .type_kind = TK_map,
                .size = sizeof(HeapPtr),
                .alignment = alignof(HeapPtr),
            };

            TypeID result = rt.register_type(map_type.name.view(), map_type);
            type_name->type_id_ = result;
            type_name->qualified_name = String(map_type.name.view());
            return result;
        } else {
            TypeID base_type_id = resolve_type(base_name, range);
            if (base_type_id == invalid_type_id) {
                errorf(range, "unknown type '{}'", base_name);
                return invalid_type_id;
            }

            const Type* base_type = rt.get_type(base_type_id);
            if (!base_type) {
                return invalid_type_id;
            }

            bool is_generic = false;
            if (base_type->type_kind == TK_sum) {
                auto sum_id = base_type->type_params[0].as<SumID>();
                const SumDef* sum_def = rt.type_table_.get(sum_id);
                if (sum_def && sum_def->generic_param_count > 0) {
                    is_generic = true;
                }
            } else if (base_type->type_kind == TK_struct) {
                auto struct_id = base_type->type_params[0].as<StructID>();
                const StructDef* struct_def = rt.type_table_.get(struct_id);
                if (struct_def && struct_def->generic_param_count > 0) {
                    is_generic = true;
                }
            }

            if (!is_generic) {
                errorf(range, "type '{}' is not a generic type", base_name);
                return invalid_type_id;
            }

            Vector<TypeID> type_args;
            for (auto* param : type_name->params) {
                auto* type_param = dynamic_cast<TypeExpression*>(param);
                if (!type_param) {
                    error(range, "type argument must be a type");
                    return invalid_type_id;
                }
                TypeID arg_type = resolve_type(type_param, range);
                if (arg_type == invalid_type_id) {
                    return invalid_type_id;
                }
                type_args.push_back(arg_type);
            }

            TypeID result = get_or_instantiate_type(base_type_id, type_args, range);
            if (result == invalid_type_id) {
                errorf(range, "failed to instantiate generic type '{}'", base_name);
                return invalid_type_id;
            }

            type_name->type_id_ = result;
            const Type* instantiated = rt.get_type(result);
            if (instantiated) {
                type_name->qualified_name = String(instantiated->name.view());
            }
            return result;
        }
    }

    auto path = extract_qualified_path(type_name->name);
    if (path.empty()) {
        error(range, "invalid type name expression");
        return invalid_type_id;
    }

    if (path.size() == 1) {
        TypeID result = resolve_type(path[0], range);
        type_name->type_id_ = result;
        if (result != invalid_type_id) {
            const Type* resolved_type = nw::kernel::runtime().get_type(result);
            if (resolved_type) {
                type_name->qualified_name = String(resolved_type->name.view());
            }
        }
        return result;
    }

    if (path.size() > 2) {
        error(range, "multi-level qualified type names not supported with current import system");
        return invalid_type_id;
    }

    auto alias_name = path[0];
    auto import_decl = resolve(alias_name, range);
    auto aliased_import = dynamic_cast<const AliasedImportDecl*>(import_decl);

    if (!aliased_import || !aliased_import->loaded_module) {
        errorf(range, "'{}' is not an imported module alias", alias_name);
        return invalid_type_id;
    }

    auto type_str = String(path[1]);
    auto export_ptr = aliased_import->loaded_module->exports().find(type_str);

    if (!export_ptr) {
        auto suggestions = format_suggestions(type_str, collect_module_exports(aliased_import->loaded_module));
        errorf(range, "type '{}' not found in module '{}'{}", type_str, aliased_import->module_path, suggestions);
        return invalid_type_id;
    }

    TypeID result = export_ptr->type_id;
    if (result == invalid_type_id && export_ptr->decl) {
        result = export_ptr->decl->type_id_;
    }
    if (result == invalid_type_id) {
        errorf(range,
            "type '{}' exists in module '{}' but semantic type metadata is unavailable in current debug level",
            type_str,
            aliased_import->module_path);
        return invalid_type_id;
    }
    type_name->type_id_ = result;
    if (result != invalid_type_id) {
        type_name->qualified_name = fmt::format("{}.{}", aliased_import->module_path, type_str);
    }
    return result;
}

} // namespace nw::smalls
