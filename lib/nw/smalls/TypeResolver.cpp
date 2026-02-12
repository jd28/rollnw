#include "TypeResolver.hpp"

#include "Ast.hpp"
#include "Context.hpp"
#include "NullVisitor.hpp"
#include "Smalls.hpp"
#include "runtime.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <functional>

namespace nw::smalls {

namespace {

bool match_normalized_type_expr(const NormalizedTypeExpr& expected, TypeID actual,
    absl::flat_hash_map<uint16_t, TypeID>& generic_substitutions)
{
    Runtime& rt = nw::kernel::runtime();

    switch (expected.kind) {
    case NormalizedTypeExpr::Kind::unknown:
        return true;
    case NormalizedTypeExpr::Kind::concrete:
        if (expected.concrete_type == invalid_type_id) {
            return true;
        }
        return expected.concrete_type == actual;
    case NormalizedTypeExpr::Kind::generic_param: {
        auto it = generic_substitutions.find(expected.generic_param_index);
        if (it == generic_substitutions.end()) {
            generic_substitutions.insert({expected.generic_param_index, actual});
            return true;
        }
        return it->second == actual;
    }
    case NormalizedTypeExpr::Kind::applied:
        break;
    }

    if (expected.concrete_type == invalid_type_id) {
        return false;
    }

    const Type* expected_base = rt.get_type(expected.concrete_type);
    const Type* actual_type = rt.get_type(actual);
    if (!expected_base || !actual_type) {
        return false;
    }
    if (expected_base->type_kind != actual_type->type_kind) {
        return false;
    }

    for (size_t i = 0; i < expected.applied_args.size(); ++i) {
        if (i >= actual_type->type_params.size()) {
            return false;
        }
        const auto& param = actual_type->type_params[i];
        if (!param.is<TypeID>()) {
            if (expected.applied_args[i].kind == NormalizedTypeExpr::Kind::unknown) {
                continue;
            }
            return false;
        }
        if (!match_normalized_type_expr(expected.applied_args[i], param.as<TypeID>(), generic_substitutions)) {
            return false;
        }
    }

    return true;
}

TypeID instantiate_normalized_type_expr(const NormalizedTypeExpr& expr, const absl::flat_hash_map<uint16_t, TypeID>& generic_substitutions)
{
    Runtime& rt = nw::kernel::runtime();
    switch (expr.kind) {
    case NormalizedTypeExpr::Kind::unknown:
        return rt.any_type();
    case NormalizedTypeExpr::Kind::concrete:
        return expr.concrete_type != invalid_type_id ? expr.concrete_type : rt.any_type();
    case NormalizedTypeExpr::Kind::generic_param: {
        auto it = generic_substitutions.find(expr.generic_param_index);
        if (it != generic_substitutions.end()) {
            return it->second;
        }
        return rt.any_type();
    }
    case NormalizedTypeExpr::Kind::applied:
        break;
    }

    if (expr.concrete_type == invalid_type_id) {
        return rt.any_type();
    }

    Vector<TypeID> type_args;
    type_args.reserve(expr.applied_args.size());
    for (const auto& arg : expr.applied_args) {
        type_args.push_back(instantiate_normalized_type_expr(arg, generic_substitutions));
    }
    if (type_args.empty()) {
        return expr.concrete_type;
    }

    auto instantiated = rt.get_or_instantiate_type(expr.concrete_type, type_args);
    if (instantiated != invalid_type_id) {
        return instantiated;
    }
    return rt.any_type();
}

// Forward declarations
struct TypeCompatibility;

// == Helpers =================================================================

struct ExpectedTypeScope {
    AstResolver& ctx;
    bool active = false;
    ExpectedTypeScope(AstResolver& ctx, TypeID expected)
        : ctx(ctx)
        , active(true)
    {
        ctx.expected_type_stack_.push_back(expected);
    }
    ~ExpectedTypeScope()
    {
        if (active && !ctx.expected_type_stack_.empty()) {
            ctx.expected_type_stack_.pop_back();
        }
    }
};

struct TypeContext {
    AstResolver& ctx;
    explicit TypeContext(AstResolver& ctx)
        : ctx(ctx)
    {
    }

    TypeID expected() const
    {
        return ctx.current_expected_type();
    }

    template <typename Fn>
    decltype(auto) with_expected(TypeID expected, Fn&& fn)
    {
        ExpectedTypeScope scope(ctx, expected);
        return fn();
    }

    void accept(Expression* expr, TypeID expected)
    {
        if (!expr) { return; }
        with_expected(expected, [&]() {
            expr->accept(ctx.active_visitor_);
        });
    }
};

struct LoopScope {
    AstResolver& ctx;
    LoopScope(AstResolver& ctx)
        : ctx(ctx)
    {
        ++ctx.loop_stack_;
    }
    ~LoopScope()
    {
        --ctx.loop_stack_;
    }
};

bool is_type_param_name(StringView name) noexcept
{
    return !name.empty() && name[0] == '$';
}

void propagate_brace_init_type(Expression* expr, TypeID target_type)
{
    if (target_type == invalid_type_id) { return; }
    if (auto* brace_init = dynamic_cast<BraceInitLiteral*>(expr)) {
        if (brace_init->type.type == TokenType::INVALID) {
            brace_init->type_id_ = target_type;
        }
    }
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

IdentifierExpression* extract_tail_identifier(Expression* expr)
{
    if (auto ident = as_identifier(expr)) {
        return ident;
    } else if (auto path_expr = as_path(expr)) {
        return path_expr->last_identifier();
    }
    return nullptr;
}

template <typename Decl>
struct GenericTypeScope {
    AstResolver& ctx;
    Declaration* prev_generic;
    absl::flat_hash_set<String> prev_params;

    GenericTypeScope(AstResolver& ctx, Decl* decl)
        : ctx(ctx)
        , prev_generic(ctx.resolving_generic_type_)
        , prev_params(ctx.current_type_params_)
    {
        if (decl->type && !decl->type->params.empty()) {
            ctx.resolving_generic_type_ = decl;
            ctx.current_type_params_.clear();
            for (auto* param : decl->type->params) {
                if (auto type_expr = dynamic_cast<TypeExpression*>(param)) {
                    if (auto ident = extract_tail_identifier(type_expr->name)) {
                        auto name = ident->ident.loc.view();
                        if (is_type_param_name(name)) {
                            ctx.current_type_params_.insert(String(name));
                            decl->type_params.push_back(String(name));
                        }
                    }
                }
            }
        }
    }

    ~GenericTypeScope()
    {
        ctx.resolving_generic_type_ = prev_generic;
        ctx.current_type_params_ = prev_params;
    }
};

TypeID build_return_type(Runtime& rt, const PVector<Expression*>& exprs)
{
    if (exprs.size() == 1) {
        return exprs[0]->type_id_;
    }
    if (exprs.size() > 1) {
        Vector<TypeID> type_ids;
        type_ids.reserve(exprs.size());
        for (auto* expr : exprs) {
            type_ids.push_back(expr->type_id_);
        }
        return rt.register_tuple_type(type_ids);
    }
    return rt.void_type();
}

const FieldDef* find_struct_field(const StructDef* struct_def, StringView name)
{
    for (uint32_t i = 0; i < struct_def->field_count; ++i) {
        if (struct_def->fields[i].name.view() == name) {
            return &struct_def->fields[i];
        }
    }
    return nullptr;
}

void resolve_brace_init_element_type(Runtime& rt, BraceInitLiteral* expr, const BraceInitItem& item, size_t idx)
{
    const Type* type = rt.get_type(expr->type_id_);
    if (!type) { return; }

    if (expr->init_type == BraceInitType::field && type->type_kind == TK_struct) {
        auto var_expr = dynamic_cast<IdentifierExpression*>(item.key);
        if (!var_expr) { return; }
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = rt.type_table_.get(struct_id);
        if (!struct_def) { return; }
        if (auto* field = find_struct_field(struct_def, var_expr->ident.loc.view())) {
            propagate_brace_init_type(item.value, field->type_id);
        }
    } else if (expr->init_type == BraceInitType::list && type->type_kind == TK_struct) {
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = rt.type_table_.get(struct_id);
        if (struct_def && idx < struct_def->field_count) {
            propagate_brace_init_type(item.value, struct_def->fields[idx].type_id);
        }
    } else if (type->type_kind == TK_array || type->type_kind == TK_fixed_array) {
        propagate_brace_init_type(item.value, type->type_params[0].as<TypeID>());
    } else if (type->type_kind == TK_map) {
        propagate_brace_init_type(item.value, type->type_params[1].as<TypeID>());
    }
}

bool report_newtype_mismatch(AstResolver& ctx, TypeID expected, TypeID actual, SourceRange range)
{
    if (expected == invalid_type_id || actual == invalid_type_id || expected == actual) {
        return false;
    }

    auto& rt = nw::kernel::runtime();
    const Type* expected_type = rt.get_type(expected);
    const Type* actual_type = rt.get_type(actual);

    if (!expected_type || !actual_type) {
        return false;
    }

    bool expected_is_newtype = expected_type->type_kind == TK_newtype;
    bool actual_is_newtype = actual_type->type_kind == TK_newtype;
    if (!expected_is_newtype && !actual_is_newtype) {
        return false;
    }

    auto unwrap = [&](TypeID type_id) -> TypeID {
        const Type* type = rt.get_type(type_id);
        while (type && type->type_kind == TK_newtype) {
            TypeID wrapped = type->type_params[0].as<TypeID>();
            if (wrapped == invalid_type_id || wrapped == type_id) {
                return invalid_type_id;
            }
            type_id = wrapped;
            type = rt.get_type(type_id);
        }
        return type_id;
    };

    TypeID expected_base = unwrap(expected);
    TypeID actual_base = unwrap(actual);

    if (expected_base == invalid_type_id || actual_base == invalid_type_id || expected_base != actual_base) {
        return false;
    }

    ctx.errorf(range, "newtype mismatch: expected '{}', got '{}'", expected, actual);
    return true;
}

const SumDef* get_sum_def(const Type* type, Runtime& rt)
{
    if (!type || type->type_kind != TK_sum) {
        return nullptr;
    }
    auto sum_id = type->type_params[0].as<SumID>();
    return rt.type_table_.get(sum_id);
}

const SumDef* get_sum_def(TypeID type_id, Runtime& rt)
{
    return get_sum_def(rt.get_type(type_id), rt);
}

const TupleDef* get_tuple_def(const Type* type, Runtime& rt)
{
    if (!type || type->type_kind != TK_tuple) {
        return nullptr;
    }
    auto tuple_id = type->type_params[0].as<TupleID>();
    return rt.type_table_.get(tuple_id);
}

const TupleDef* get_tuple_def(TypeID type_id, Runtime& rt)
{
    return get_tuple_def(rt.get_type(type_id), rt);
}

bool is_sum_instantiation(TypeID candidate, TypeID generic_base)
{
    auto& rt = nw::kernel::runtime();
    const Type* candidate_type = rt.get_type(candidate);
    const Type* base_type = rt.get_type(generic_base);
    if (!candidate_type || !base_type) {
        return false;
    }
    if (candidate_type->type_kind != TK_sum || base_type->type_kind != TK_sum) {
        return false;
    }

    StringView candidate_name = candidate_type->name.view();
    StringView base_name = base_type->name.view();
    if (candidate_name.size() <= base_name.size() + 2) {
        return false;
    }
    if (candidate_name.compare(0, base_name.size(), base_name) != 0) {
        return false;
    }
    if (candidate_name[base_name.size()] != '!' || candidate_name[base_name.size() + 1] != '(') {
        return false;
    }

    return true;
}

enum class ArgCheckResult : uint8_t {
    ok,
    newtype_mismatch,
    type_mismatch,
};

struct TypeCompatibility {
    AstResolver& ctx;
    Runtime& rt;

    bool function_types_compatible(TypeID expected_type, TypeID actual_type) const
    {
        const Type* expected = rt.get_type(expected_type);
        const Type* actual = rt.get_type(actual_type);
        if (!expected || !actual || expected->type_kind != TK_function || actual->type_kind != TK_function) {
            return false;
        }

        const size_t expected_params = rt.get_function_param_count(expected_type);
        const size_t actual_params = rt.get_function_param_count(actual_type);
        if (expected_params != actual_params) {
            return false;
        }

        for (size_t i = 0; i < expected_params; ++i) {
            TypeID e = rt.get_function_param_type(expected_type, i);
            TypeID a = rt.get_function_param_type(actual_type, i);
            if (e != rt.any_type() && !rt.is_type_convertible(e, a)) {
                return false;
            }
        }

        TypeID e_ret = rt.get_function_return_type(expected_type);
        TypeID a_ret = rt.get_function_return_type(actual_type);
        if (e_ret != rt.any_type() && !rt.is_type_convertible(e_ret, a_ret)) {
            return false;
        }

        return true;
    }

    ArgCheckResult check_types_compatible(TypeID expected_type, TypeID actual_type, SourceRange range)
    {
        if (expected_type == invalid_type_id || actual_type == invalid_type_id) {
            return ArgCheckResult::type_mismatch;
        }

        const Type* expected = rt.get_type(expected_type);
        const Type* actual = rt.get_type(actual_type);

        if (expected && actual) {
            if (expected->type_kind == TK_any_array && actual->type_kind == TK_array) {
                return ArgCheckResult::ok;
            }
            if (expected->type_kind == TK_any_map && actual->type_kind == TK_map) {
                return ArgCheckResult::ok;
            }
            if (expected->type_kind == TK_function && actual->type_kind == TK_function) {
                if (function_types_compatible(expected_type, actual_type)) {
                    return ArgCheckResult::ok;
                }
            }
            if (expected->type_kind == TK_array && actual->type_kind == TK_array) {
                TypeID expected_elem = expected->type_params[0].as<TypeID>();
                if (expected_elem == rt.any_type()) {
                    return ArgCheckResult::ok;
                }
            }
            if (expected->type_kind == TK_map && actual->type_kind == TK_map) {
                TypeID expected_key = expected->type_params[0].as<TypeID>();
                TypeID expected_val = expected->type_params[1].as<TypeID>();
                if (expected_key == rt.any_type() && expected_val == rt.any_type()) {
                    return ArgCheckResult::ok;
                }
            }
        }

        if (report_newtype_mismatch(ctx, expected_type, actual_type, range)) {
            return ArgCheckResult::newtype_mismatch;
        }

        if (rt.is_type_convertible(expected_type, actual_type)) {
            return ArgCheckResult::ok;
        }

        return ArgCheckResult::type_mismatch;
    }

    ArgCheckResult check(size_t index, TypeID expected_type, TypeID actual_type, SourceRange range)
    {
        auto result = check_types_compatible(expected_type, actual_type, range);
        if (result == ArgCheckResult::type_mismatch
            && expected_type != invalid_type_id && actual_type != invalid_type_id) {
            ctx.errorf(range, "arg {}: expected '{}', got '{}'", index, expected_type, actual_type);
        }
        return result;
    }

    ArgCheckResult check_call(StringView func_name, StringView param_name, size_t index, TypeID expected_type, TypeID actual_type, SourceRange range)
    {
        auto result = check_types_compatible(expected_type, actual_type, range);
        if (result == ArgCheckResult::type_mismatch
            && expected_type != invalid_type_id && actual_type != invalid_type_id) {
            if (!param_name.empty()) {
                ctx.errorf(range, "call '{}': arg {} ('{}') expected '{}', got '{}'", func_name, index, param_name, expected_type, actual_type);
            } else {
                ctx.errorf(range, "call '{}': arg {} expected '{}', got '{}'", func_name, index, expected_type, actual_type);
            }
        }
        return result;
    }
};

struct ArgValidator {
    AstResolver& ctx;
    TypeContext& type_ctx;
    TypeCompatibility& compat;

    template <typename ExprVector>
    void validate(const ExprVector& args, const Vector<TypeID>& expected_types)
    {
        size_t count = std::min(args.size(), expected_types.size());
        for (size_t i = 0; i < count; ++i) {
            type_ctx.accept(args[i], expected_types[i]);

            if (dynamic_cast<EmptyExpression*>(args[i])) {
                ctx.error(args[i]->range_, "argument cannot be a null expression");
                continue;
            }

            compat.check(i, expected_types[i], args[i]->type_id_, args[i]->range_);
        }
    }

    void validate_call(const PVector<Expression*>& args, const FunctionDefinition* func_def, const Vector<TypeID>& expected_types, SourceRange call_range)
    {
        if (!func_def) {
            validate(args, expected_types);
            return;
        }

        size_t count = std::min(args.size(), expected_types.size());
        for (size_t i = 0; i < count; ++i) {
            type_ctx.accept(args[i], expected_types[i]);

            if (dynamic_cast<EmptyExpression*>(args[i])) {
                ctx.error(args[i]->range_, "argument cannot be a null expression");
                continue;
            }

            StringView param_name;
            if (i < func_def->params.size() && func_def->params[i]) {
                param_name = func_def->params[i]->identifier_.loc.view();
            }

            compat.check_call(func_def->identifier_.loc.view(), param_name, i, expected_types[i], args[i]->type_id_, args[i]->range_);
        }

        if (args.size() > expected_types.size()) {
            ctx.errorf(call_range, "call '{}': expected {} args, got {}", func_def->identifier_.loc.view(), expected_types.size(), args.size());
        }
    }

    void validate(Expression* arg, TypeID expected_type, size_t index = 0)
    {
        if (!arg) {
            return;
        }
        type_ctx.accept(arg, expected_type);
        if (dynamic_cast<EmptyExpression*>(arg)) {
            ctx.error(arg->range_, "argument cannot be a null expression");
            return;
        }
        compat.check(index, expected_type, arg->type_id_, arg->range_);
    }
};

struct GenericInference {
    AstResolver& ctx;
    Runtime& rt;

    struct Inferred {
        TypeID type = invalid_type_id;
        size_t arg_index = 0;
        StringView param_name;
        SourceRange range;
        bool conflict = false;
    };

    absl::flat_hash_map<String, Inferred> infer_from_args(const FunctionDefinition* func_def, const PVector<Expression*>& args)
    {
        absl::flat_hash_map<String, Inferred> inferred_types;

        auto infer_type_param = [&](StringView name, TypeID arg_type, size_t arg_index, StringView param_name, SourceRange range) {
            auto key = String(name);
            auto it = inferred_types.find(key);
            if (it == inferred_types.end()) {
                inferred_types.emplace(std::move(key), Inferred{arg_type, arg_index, param_name, range, false});
                return;
            }

            if (it->second.conflict) {
                return;
            }

            if (it->second.type != arg_type) {
                ctx.errorf(range,
                    "generic inference failed for '{}': inferred '{}' from arg {} (param '{}'), but '{}' from arg {} (param '{}')",
                    name,
                    it->second.type,
                    it->second.arg_index,
                    it->second.param_name,
                    arg_type,
                    arg_index,
                    param_name);
                it->second.conflict = true;
            }
        };

        for (size_t i = 0; i < args.size(); ++i) {
            auto* param_type_expr = func_def->params[i]->type;
            if (!param_type_expr) continue;

            StringView param_name = func_def->params[i]->identifier_.loc.view();

            auto param_type_name = param_type_expr->str();
            if (is_type_param_name(param_type_name)) {
                TypeID arg_type = args[i]->type_id_;
                infer_type_param(param_type_name, arg_type, i, param_name, args[i]->range_);
                continue;
            }

            if (param_type_expr->is_function_type) {
                TypeID arg_type = args[i]->type_id_;
                const Type* arg_type_info = rt.get_type(arg_type);
                if (!arg_type_info || arg_type_info->type_kind != TK_function) {
                    continue;
                }

                const size_t expected_param_count = param_type_expr->params.size();
                const size_t actual_param_count = rt.get_function_param_count(arg_type);
                if (expected_param_count != actual_param_count) {
                    continue;
                }

                for (size_t pi = 0; pi < expected_param_count; ++pi) {
                    auto* expected_param_type = dynamic_cast<TypeExpression*>(param_type_expr->params[pi]);
                    if (!expected_param_type) {
                        continue;
                    }

                    String expected_name = expected_param_type->str();
                    if (is_type_param_name(expected_name)) {
                        infer_type_param(expected_name, rt.get_function_param_type(arg_type, pi), i, param_name, args[i]->range_);
                    }
                }

                if (param_type_expr->fn_return_type) {
                    String expected_ret_name = param_type_expr->fn_return_type->str();
                    if (is_type_param_name(expected_ret_name)) {
                        infer_type_param(expected_ret_name, rt.get_function_return_type(arg_type), i, param_name, args[i]->range_);
                    }
                }

                continue;
            }

            auto base_name_opt = extract_identifier(param_type_expr->name);
            if (!base_name_opt) {
                continue;
            }

            if (*base_name_opt == "array" && param_type_expr->params.size() == 1) {
                auto* elem_type_expr = dynamic_cast<TypeExpression*>(param_type_expr->params[0]);
                if (!elem_type_expr) {
                    continue;
                }

                auto elem_name_opt = extract_identifier(elem_type_expr->name);
                if (!elem_name_opt || !is_type_param_name(*elem_name_opt)) {
                    continue;
                }

                TypeID arg_type = args[i]->type_id_;
                const Type* arg_type_info = rt.get_type(arg_type);
                if (!arg_type_info || arg_type_info->type_kind != TK_array) {
                    continue;
                }

                if (!arg_type_info->type_params[0].is<TypeID>()) {
                    continue;
                }

                TypeID elem_type = arg_type_info->type_params[0].as<TypeID>();
                infer_type_param(*elem_name_opt, elem_type, i, param_name, args[i]->range_);
            } else if (*base_name_opt == "map" && param_type_expr->params.size() == 2) {
                auto* key_type_expr = dynamic_cast<TypeExpression*>(param_type_expr->params[0]);
                auto* val_type_expr = dynamic_cast<TypeExpression*>(param_type_expr->params[1]);
                if (!key_type_expr || !val_type_expr) {
                    continue;
                }

                auto key_name_opt = extract_identifier(key_type_expr->name);
                auto val_name_opt = extract_identifier(val_type_expr->name);

                if ((!key_name_opt || !is_type_param_name(*key_name_opt))
                    && (!val_name_opt || !is_type_param_name(*val_name_opt))) {
                    continue;
                }

                TypeID arg_type = args[i]->type_id_;
                const Type* arg_type_info = rt.get_type(arg_type);
                if (!arg_type_info || arg_type_info->type_kind != TK_map) {
                    continue;
                }

                if (key_name_opt && is_type_param_name(*key_name_opt)
                    && arg_type_info->type_params[0].is<TypeID>()) {
                    infer_type_param(*key_name_opt, arg_type_info->type_params[0].as<TypeID>(), i, param_name, args[i]->range_);
                }

                if (val_name_opt && is_type_param_name(*val_name_opt)
                    && arg_type_info->type_params[1].is<TypeID>()) {
                    infer_type_param(*val_name_opt, arg_type_info->type_params[1].as<TypeID>(), i, param_name, args[i]->range_);
                }
            }
        }

        return inferred_types;
    }

    Vector<TypeID> build_type_args(const FunctionDefinition* func_def, const absl::flat_hash_map<String, Inferred>& inferred_types, SourceRange range)
    {
        Vector<TypeID> type_args;
        Vector<String> missing;
        for (const auto& tp : func_def->type_params) {
            auto it = inferred_types.find(tp);
            if (it != inferred_types.end()) {
                type_args.push_back(it->second.type);
            } else {
                type_args.push_back(invalid_type_id);
                missing.push_back(tp);
            }
        }

        if (!missing.empty()) {
            String msg = "could not infer type for parameter";
            msg += missing.size() == 1 ? " " : "s ";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) {
                    msg += (i + 1 == missing.size()) ? " and " : ", ";
                }
                msg += "'";
                msg += missing[i];
                msg += "'";

                bool in_params = false;
                for (auto* p : func_def->params) {
                    if (p && p->type && p->type->str().find(missing[i]) != String::npos) {
                        in_params = true;
                        break;
                    }
                }
                bool in_ret = func_def->return_type && func_def->return_type->str().find(missing[i]) != String::npos;
                if (!in_params && in_ret) {
                    msg += " (appears only in return type)";
                }
            }
            ctx.error(range, msg);
        }

        return type_args;
    }
};

struct TypeExpressionSnapshot : NullVisitor {
    struct State {
        TypeID type_id{};
        String qualified_name;
    };

    absl::flat_hash_map<TypeExpression*, State> states;

    void visit(TypeExpression* expr) override
    {
        if (!expr || states.contains(expr)) {
            return;
        }
        states[expr] = State{expr->type_id_, expr->qualified_name};

        if (expr->name) {
            expr->name->accept(this);
        }
        for (auto* p : expr->params) {
            if (p) {
                p->accept(this);
            }
        }
        if (expr->fn_return_type) {
            expr->fn_return_type->accept(this);
        }
    }

    void restore()
    {
        for (auto& [expr, st] : states) {
            expr->type_id_ = st.type_id;
            expr->qualified_name = st.qualified_name;
        }
    }
};

struct CallResolver {
    TypeResolver& resolver;
    Runtime& rt;
    ArgValidator& validator;
    GenericInference& inference;

    bool resolve_function_type_call(CallExpression* expr, TypeID func_type_id)
    {
        const Type* func_type = rt.get_type(func_type_id);
        if (!func_type || func_type->type_kind != TK_function) {
            return false;
        }

        size_t param_count = rt.get_function_param_count(func_type_id);
        if (expr->args.size() != param_count) {
            resolver.ctx.errorf(expr->range_, "expected {} args, got {}", param_count, expr->args.size());
            return true;
        }

        Vector<TypeID> expected_types;
        expected_types.reserve(expr->args.size());
        for (size_t i = 0; i < expr->args.size(); ++i) {
            expected_types.push_back(rt.get_function_param_type(func_type_id, i));
        }

        validator.validate(expr->args, expected_types);
        expr->type_id_ = rt.get_function_return_type(func_type_id);
        return true;
    }

    void resolve(CallExpression* expr)
    {
        expr->env_ = resolver.ctx.env_stack_.back();
        expr->inferred_type_args.clear();

        auto* path_expr = dynamic_cast<PathExpression*>(expr->expr);
        if (resolver.try_resolve_variant_call(expr, path_expr)) {
            return;
        }

        if (!path_expr) {
            expr->expr->accept(&resolver);
            if (!resolve_function_type_call(expr, expr->expr->type_id_)) {
                resolver.ctx.error(expr->expr->range_, "call target not a function");
            }
            return;
        }

        const Declaration* decl = nullptr;
        IdentifierExpression* ve = nullptr;
        String func_name;

        auto apply_function_abi = [&](const Export& exp, Script* provider, SourceRange err_range) -> bool {
            if (exp.kind != Export::Kind::function || !exp.function_abi) { return false; }

            const auto& abi = *exp.function_abi;
            if (expr->args.size() < abi.min_arity || expr->args.size() > abi.max_arity) {
                resolver.ctx.errorf(expr->range_,
                    "no matching function call '{}' expected {}..{} parameters",
                    func_name,
                    abi.min_arity,
                    abi.max_arity);
                return true;
            }

            for (auto* arg : expr->args) {
                if (arg) {
                    arg->accept(&resolver);
                }
            }

            absl::flat_hash_map<uint16_t, TypeID> generic_substitutions;
            for (size_t i = 0; i < expr->args.size() && i < abi.param_types.size(); ++i) {
                auto actual_type = expr->args[i] ? expr->args[i]->type_id_ : rt.any_type();
                if (!match_normalized_type_expr(abi.param_types[i], actual_type, generic_substitutions)) {
                    resolver.ctx.errorf(expr->args[i] ? expr->args[i]->range_ : err_range,
                        "argument type mismatch for '{}'", func_name);
                    return true;
                }
            }

            if (exp.is_generic) {
                expr->inferred_type_args.clear();
                expr->inferred_type_args.reserve(abi.generic_arity);
                for (uint16_t i = 0; i < abi.generic_arity; ++i) {
                    auto it = generic_substitutions.find(i);
                    if (it == generic_substitutions.end()) {
                        resolver.ctx.errorf(err_range,
                            "generic inference failed for '{}'",
                            func_name);
                        return true;
                    }
                    expr->inferred_type_args.push_back(it->second);
                }
            }

            expr->type_id_ = instantiate_normalized_type_expr(abi.return_type, generic_substitutions);
            expr->resolved_provider = provider;
            return true;
        };

        if (path_expr->parts.size() == 1) {
            ve = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
            if (!ve) {
                resolver.ctx.error(expr->expr->range_, "call target must be identifier");
                return;
            }

            ve->env_ = resolver.ctx.env_stack_.back();
            func_name = String(ve->ident.loc.view());
            decl = resolver.ctx.resolve(ve->ident.loc.view(), ve->ident.loc.range);
        } else if (path_expr->parts.size() == 2) {
            auto* lhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
            auto* rhs_ident = dynamic_cast<IdentifierExpression*>(path_expr->parts.back());
            if (!lhs_ident || !rhs_ident) {
                resolver.ctx.error(expr->expr->range_, "call target must be identifier");
                return;
            }

            lhs_ident->env_ = resolver.ctx.env_stack_.back();
            rhs_ident->env_ = resolver.ctx.env_stack_.back();

            auto* lhs_decl = resolver.ctx.resolve(lhs_ident->ident.loc.view(), lhs_ident->ident.loc.range);
            auto* alias_decl = dynamic_cast<const AliasedImportDecl*>(lhs_decl);
            Script* alias_module = alias_decl ? alias_decl->loaded_module : nullptr;
            StringView alias_module_path = alias_decl ? alias_decl->module_path : StringView{};

            if (!alias_module) {
                auto lhs_exp = resolver.ctx.env_stack_.back().find(String(lhs_ident->ident.loc.view()));
                if (lhs_exp && lhs_exp->kind == Export::Kind::module_alias && !lhs_exp->provider_module.empty()) {
                    alias_module = nw::kernel::runtime().get_module(lhs_exp->provider_module);
                    alias_module_path = lhs_exp->provider_module;
                }
            }

            if (!alias_module) {
                resolver.ctx.errorf(lhs_ident->range_, "'{}' is not an imported module alias", lhs_ident->ident.loc.view());
                return;
            }

            func_name = String(rhs_ident->ident.loc.view());
            auto export_ptr = alias_module->exports().find(func_name);
            if (!export_ptr) {
                resolver.ctx.errorf(rhs_ident->range_, "'{}' not found in module '{}'", func_name, alias_module_path);
                return;
            }

            if (!export_ptr->decl) {
                if (apply_function_abi(*export_ptr, alias_module, rhs_ident->range_)) {
                    return;
                }
                resolver.ctx.errorf(rhs_ident->range_,
                    "'{}' exists in module '{}' but semantic declaration data is unavailable in current debug level",
                    func_name,
                    alias_module_path);
                return;
            }

            // Keep declaration-backed flow for generic/intrinsic/newtype-aware paths.
            // Non-generic metadata-only calls return through apply_function_abi above.
            resolver.ctx.record_decl_provider(export_ptr->decl, alias_module);
            decl = export_ptr->decl;
            ve = rhs_ident;
        } else {
            resolver.ctx.error(expr->expr->range_, "call target must be identifier");
            return;
        }

        if (!decl && ve) {
            auto exp = expr->env_.find(func_name);
            if (exp) {
                Script* provider = nullptr;
                if (!exp->provider_module.empty()) {
                    provider = nw::kernel::runtime().get_module(exp->provider_module);
                }
                if (apply_function_abi(*exp, provider, expr->range_)) {
                    return;
                }
            }

            if (exp && exp->kind == Export::Kind::function && !exp->decl
                && expr->inferred_type_args.empty()) {
                resolver.ctx.errorf(expr->range_,
                    "'{}' exists but semantic declaration data is unavailable for generic instantiation in current debug level",
                    func_name);
                return;
            }
        }

        if (auto* newtype_decl = dynamic_cast<const NewtypeDecl*>(decl)) {
            TypeID newtype_id = newtype_decl->type_id_;
            const Type* newtype_type = rt.get_type(newtype_id);

            if (newtype_type && newtype_type->type_kind == TK_newtype) {
                TypeID wrapped_type_id = newtype_type->type_params[0].as<TypeID>();

                if (expr->args.size() != 1) {
                    resolver.ctx.errorf(expr->range_, "newtype '{}' expects 1 argument, got {}", func_name, expr->args.size());
                    return;
                }

                validator.validate(expr->args[0], wrapped_type_id);
                expr->type_id_ = newtype_id;
                return;
            }
        }

        // Handle str()/hash() as operator calls
        if (!decl && expr->args.size() == 1 && (func_name == "str" || func_name == "hash")) {
            expr->args[0]->accept(&resolver);
            TypeID arg_type = expr->args[0]->type_id_;
            if (func_name == "str") {
                if (rt.find_str_op(arg_type)) {
                    expr->type_id_ = rt.string_type();
                    return;
                }
            } else {
                if (rt.find_hash_op(arg_type)) {
                    expr->type_id_ = rt.int_type();
                    return;
                }
            }
        }

        const FunctionDefinition* func_def = dynamic_cast<const FunctionDefinition*>(decl);
        if (!func_def) {
            if (decl && decl->type_id_ != invalid_type_id) {
                expr->expr->type_id_ = decl->type_id_;
                if (resolve_function_type_call(expr, decl->type_id_)) {
                    return;
                }
            }

            if (ve) {
                auto suggestions = format_suggestions(func_name, resolver.ctx.collect_env_names(resolver.ctx.env_stack_.back()));
                resolver.ctx.errorf(ve->range_, "unable to resolve identifier '{}'{}", func_name, suggestions);
            } else {
                resolver.ctx.error(expr->expr->range_, fmt::format("unable to resolve identifier '{}'", func_name));
            }
            return;
        }

        expr->resolved_func = func_def;
        expr->resolved_provider = resolver.ctx.provider_for_decl(func_def);

        size_t req = 0;
        for (size_t i = 0; i < func_def->params.size(); ++i) {
            if (func_def->params[i]->init) { break; }
            ++req;
        }

        if (expr->args.size() < req || expr->args.size() > func_def->params.size()) {
            resolver.ctx.errorf(expr->range_, "no matching function call '{}' expected {} parameters",
                resolver.ctx.parent_->view_from_range(expr->range_), req);
            return;
        }

        Vector<TypeID> expected_types;
        expected_types.reserve(expr->args.size());
        for (size_t i = 0; i < expr->args.size(); ++i) {
            propagate_brace_init_type(expr->args[i], func_def->params[i]->type_id_);
            expected_types.push_back(func_def->params[i]->type_id_);
        }

        validator.validate_call(expr->args, func_def, expected_types, expr->range_);

        auto intrinsic_id = resolver.ctx.resolve_intrinsic_id(const_cast<FunctionDefinition*>(func_def), expr->range_, false);
        if (intrinsic_id) {
            expr->intrinsic_id = intrinsic_id;
        }

        if (func_def->is_generic()) {
            auto inferred_types = inference.infer_from_args(func_def, expr->args);
            expr->inferred_type_args = inference.build_type_args(func_def, inferred_types, expr->range_);

            auto* return_type_expr = func_def->return_type;
            if (!return_type_expr) {
                expr->type_id_ = rt.void_type();
                return;
            }

            auto return_type_name = return_type_expr->str();
            if (is_type_param_name(return_type_name)) {
                auto it = inferred_types.find(return_type_name);
                if (it != inferred_types.end()) {
                    expr->type_id_ = it->second.type;
                } else {
                    expr->type_id_ = rt.any_type();
                }
                return;
            }

            // Composite return types: resolve return type under concrete substitutions.
            absl::flat_hash_map<String, TypeID> subs;
            subs.reserve(func_def->type_params.size());
            for (size_t i = 0; i < func_def->type_params.size() && i < expr->inferred_type_args.size(); ++i) {
                subs[func_def->type_params[i]] = expr->inferred_type_args[i];
            }

            auto saved_subs = resolver.ctx.type_substitutions_;
            TypeExpressionSnapshot snap;
            return_type_expr->accept(&snap);

            resolver.ctx.set_type_substitutions(subs);
            TypeID instantiated = resolver.ctx.resolve_type(return_type_expr, expr->range_);
            resolver.ctx.set_type_substitutions(saved_subs);
            snap.restore();

            expr->type_id_ = (instantiated != invalid_type_id) ? instantiated : func_def->type_id_;
        } else {
            expr->type_id_ = func_def->type_id_;
        }
    }
};

void ensure_capture(LambdaExpression* lambda, const String& name, TypeID type, uint32_t declaring_depth, bool is_upvalue_in_parent)
{
    if (!lambda) { return; }
    for (const auto& cap : lambda->captures) {
        if (cap.name == name) { return; }
    }
    CapturedVar cap;
    cap.name = name;
    cap.declaring_depth = declaring_depth;
    cap.source_register = 0;
    cap.type = type;
    cap.is_upvalue_in_parent = is_upvalue_in_parent;
    lambda->captures.push_back(cap);
}

void validate_brace_init(AstResolver& ctx, BraceInitLiteral* expr)
{
    auto& rt = nw::kernel::runtime();
    String type_name(rt.type_name(expr->type_id_));

    const Type* type = rt.get_type(expr->type_id_);
    if (!type) {
        return;
    }

    if (type->type_kind == TK_struct) {
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = rt.type_table_.get(struct_id);
        if (!struct_def) { return; }

        if (expr->init_type == BraceInitType::field) {
            for (const auto& item : expr->items) {
                if (auto var_expr = dynamic_cast<IdentifierExpression*>(item.key)) {
                    StringView field_name = var_expr->ident.loc.view();
                    if (!find_struct_field(struct_def, field_name)) {
                        ctx.errorf(var_expr->ident.loc.range, "'{}' has no field named '{}'", type_name, field_name);
                    }
                }
            }
        } else if (expr->init_type == BraceInitType::list) {
            if (expr->items.size() > struct_def->field_count) {
                ctx.errorf(expr->range_, "too many initializers for '{}', expected {} but got {}",
                    type_name, struct_def->field_count, expr->items.size());
            }
        } else if (expr->init_type == BraceInitType::kv) {
            ctx.errorf(expr->range_, "key-value initializer syntax not valid for struct type '{}'", type_name);
        }
    } else if (type->type_kind == TK_fixed_array) {
        int32_t size = type->type_params[1].as<int32_t>();
        if (expr->init_type == BraceInitType::list) {
            if (expr->items.size() > static_cast<size_t>(size)) {
                ctx.errorf(expr->range_, "too many initializers for '{}', expected {} but got {}", type_name, size, expr->items.size());
            }
        } else {
            ctx.errorf(expr->range_, "initializer syntax not valid for fixed array type '{}'", type_name);
        }
    }
}

TokenType parse_operator_name(StringView name)
{
    if (name == "plus") return TokenType::PLUS;
    if (name == "minus") return TokenType::MINUS;
    if (name == "times") return TokenType::TIMES;
    if (name == "div") return TokenType::DIV;
    if (name == "eq") return TokenType::EQEQ;
    if (name == "lt") return TokenType::LT;
    if (name == "str") return TokenType::INVALID;
    if (name == "hash") return TokenType::INVALID;
    return TokenType::INVALID;
}

} // namespace

// == TypeResolver Implementation =============================================

TypeResolver::TypeResolver(AstResolver& ctx)
    : ctx(ctx)
{
}

bool TypeResolver::try_resolve_variant_call(CallExpression* expr, PathExpression* path_expr)
{
    if (!expr || !path_expr || path_expr->parts.size() != 2) {
        return false;
    }

    auto* lhs_var = dynamic_cast<IdentifierExpression*>(path_expr->parts.front());
    auto* rhs_var = dynamic_cast<IdentifierExpression*>(path_expr->parts.back());
    if (!lhs_var || !rhs_var) {
        return false;
    }

    lhs_var->env_ = ctx.env_stack_.back();
    rhs_var->env_ = ctx.env_stack_.back();

    auto* lhs_decl = ctx.resolve(lhs_var->ident.loc.view(), lhs_var->ident.loc.range);
    auto* sum_decl = dynamic_cast<const SumDecl*>(lhs_decl);
    if (!sum_decl) {
        return false;
    }

    TypeContext type_ctx(ctx);
    auto& rt = nw::kernel::runtime();

    TypeID base_sum_type_id = sum_decl->type_id_;
    if (base_sum_type_id == invalid_type_id) {
        return true;
    }

    const Type* base_sum_type = rt.get_type(base_sum_type_id);
    if (!base_sum_type || base_sum_type->type_kind != TK_sum) {
        return true;
    }

    StringView variant_name = rhs_var->ident.loc.view();
    const VariantDecl* variant_decl = sum_decl->locate_variant(variant_name);
    if (!variant_decl) {
        ctx.errorf(expr->range_, "variant '{}' not found in sum type '{}'", variant_name, base_sum_type_id);
        return true;
    }

    bool is_generic_sum = sum_decl->is_generic();
    TypeID sum_type_id = base_sum_type_id;
    TypeID expected_type = ctx.current_expected_type();
    if (is_generic_sum && expected_type != invalid_type_id
        && is_sum_instantiation(expected_type, base_sum_type_id)) {
        sum_type_id = expected_type;
    }

    const SumDef* sum_def = nullptr;
    if (sum_type_id != invalid_type_id) {
        sum_def = get_sum_def(sum_type_id, rt);
    }

    std::function<void(TypeExpression*, TypeID, absl::flat_hash_map<String, TypeID>&)> infer_from_payload;
    infer_from_payload = [&](TypeExpression* payload_expr,
                             TypeID arg_type,
                             absl::flat_hash_map<String, TypeID>& inferred) {
        if (!payload_expr) { return; }

        if (!payload_expr->name && !payload_expr->params.empty()) {
            const Type* arg_type_info = rt.get_type(arg_type);
            if (!arg_type_info || arg_type_info->type_kind != TK_tuple) {
                return;
            }
            auto tuple_id = arg_type_info->type_params[0].as<TupleID>();
            const TupleDef* tuple_def = rt.type_table_.get(tuple_id);
            if (!tuple_def || tuple_def->element_count != payload_expr->params.size()) {
                return;
            }
            for (size_t i = 0; i < payload_expr->params.size(); ++i) {
                auto* elem_expr = dynamic_cast<TypeExpression*>(payload_expr->params[i]);
                if (!elem_expr) { continue; }
                infer_from_payload(elem_expr, tuple_def->element_types[i], inferred);
            }
            return;
        }

        auto ident = extract_tail_identifier(payload_expr->name);
        if (!ident) { return; }
        auto name = ident->ident.loc.view();
        if (!is_type_param_name(name)) { return; }

        auto it = inferred.find(String(name));
        if (it == inferred.end()) {
            inferred[String(name)] = arg_type;
        } else if (it->second != arg_type) {
            ctx.errorf(expr->range_, "type parameter '{}' inferred as both '{}' and '{}'", name, it->second, arg_type);
        }
    };

    if (!variant_decl->payload) {
        if (!expr->args.empty()) {
            ctx.errorf(expr->range_, "unit variant '{}' does not take arguments", variant_name);
            return true;
        }
        if (is_generic_sum && sum_type_id == base_sum_type_id) {
            ctx.errorf(expr->range_, "cannot infer type arguments for generic sum '{}'", base_sum_type_id);
            return true;
        }

        if (sum_def) {
            const VariantDef* variant = sum_def->find_variant(variant_name);
            if (!variant) {
                ctx.errorf(expr->range_, "variant '{}' not found in sum type '{}'", variant_name, sum_type_id);
                return true;
            }
            if (variant->payload_type != invalid_type_id) {
                ctx.errorf(expr->range_, "payload variant '{}' must be called with arguments", variant_name);
                return true;
            }
        }

        expr->type_id_ = sum_type_id;
        return true;
    }

    size_t expected_arg_count = 1;
    if (auto* payload_type = dynamic_cast<TypeExpression*>(variant_decl->payload)) {
        if (!payload_type->name && !payload_type->params.empty()) {
            expected_arg_count = payload_type->params.size();
        }
    }

    if (expr->args.size() != expected_arg_count) {
        ctx.errorf(expr->range_, "expected {} args for '{}', got {}", expected_arg_count, variant_name, expr->args.size());
        return true;
    }

    const VariantDef* variant = nullptr;
    const TupleDef* tuple_def = nullptr;
    if (sum_def) {
        variant = sum_def->find_variant(variant_name);
        if (!variant) {
            ctx.errorf(expr->range_, "variant '{}' not found in sum type '{}'", variant_name, sum_type_id);
            return true;
        }
        if (variant->payload_type != invalid_type_id) {
            const Type* payload_type = rt.get_type(variant->payload_type);
            if (payload_type && payload_type->type_kind == TK_tuple) {
                auto tuple_id = payload_type->type_params[0].as<TupleID>();
                tuple_def = rt.type_table_.get(tuple_id);
            }
        }
    }

    for (size_t i = 0; i < expr->args.size(); ++i) {
        if (tuple_def && i < tuple_def->element_count) {
            type_ctx.accept(expr->args[i], tuple_def->element_types[i]);
        } else if (variant && variant->payload_type != invalid_type_id && !tuple_def) {
            type_ctx.accept(expr->args[i], variant->payload_type);
        } else {
            expr->args[i]->accept(this);
        }
    }

    if (is_generic_sum && sum_type_id == base_sum_type_id) {
        absl::flat_hash_map<String, TypeID> inferred;
        if (auto* payload_expr = dynamic_cast<TypeExpression*>(variant_decl->payload)) {
            if (!payload_expr->name && !payload_expr->params.empty()) {
                for (size_t i = 0; i < payload_expr->params.size() && i < expr->args.size(); ++i) {
                    auto* elem_expr = dynamic_cast<TypeExpression*>(payload_expr->params[i]);
                    if (!elem_expr) { continue; }
                    infer_from_payload(elem_expr, expr->args[i]->type_id_, inferred);
                }
            } else {
                infer_from_payload(payload_expr, expr->args[0]->type_id_, inferred);
            }
        }

        Vector<TypeID> type_args;
        bool complete = true;
        for (const auto& tp : sum_decl->type_params) {
            auto it = inferred.find(tp);
            if (it == inferred.end()) {
                complete = false;
                break;
            }
            type_args.push_back(it->second);
        }

        if (!complete) {
            ctx.errorf(expr->range_, "cannot infer type arguments for generic sum '{}'", base_sum_type_id);
            return true;
        }

        TypeID instantiated = ctx.get_or_instantiate_type(base_sum_type_id, type_args, expr->range_);
        if (instantiated == invalid_type_id) {
            ctx.errorf(expr->range_, "failed to instantiate generic sum '{}'", base_sum_type_id);
            return true;
        }
        sum_type_id = instantiated;
        sum_def = get_sum_def(sum_type_id, rt);
        if (sum_def) {
            variant = sum_def->find_variant(variant_name);
            tuple_def = nullptr;
            if (variant && variant->payload_type != invalid_type_id) {
                tuple_def = get_tuple_def(variant->payload_type, rt);
            }
        }
    }

    if (variant && variant->payload_type != invalid_type_id) {
        TypeCompatibility compat{ctx, rt};
        ArgValidator validator{ctx, type_ctx, compat};
        if (tuple_def) {
            if (expr->args.size() != tuple_def->element_count) {
                ctx.errorf(expr->range_, "expected {} args for '{}', got {}", tuple_def->element_count, variant_name, expr->args.size());
                return true;
            }
            Vector<TypeID> expected_types;
            expected_types.reserve(expr->args.size());
            for (size_t i = 0; i < expr->args.size(); ++i) {
                expected_types.push_back(tuple_def->element_types[i]);
            }
            validator.validate(expr->args, expected_types);
        } else {
            TypeID expected_payload = variant->payload_type;
            type_ctx.accept(expr->args[0], expected_payload);
            auto check = compat.check(0, expected_payload, expr->args[0]->type_id_, expr->args[0]->range_);
            if (check == ArgCheckResult::newtype_mismatch) {
                expr->type_id_ = sum_type_id;
                return true;
            }
        }
    }

    expr->type_id_ = sum_type_id;
    return true;
}

// Forward declarations of private helpers
static void resolve_function_signature(TypeResolver& resolver, FunctionDefinition* decl);
static void resolve_operator_alias(TypeResolver& resolver, FunctionDefinition* func);
static void resolve_function_body(TypeResolver& resolver, FunctionDefinition* decl);
static void resolve_struct_field(TypeResolver& resolver, VarDecl* decl);
static void validate_array_intrinsic_signature(TypeResolver& resolver, FunctionDefinition* decl, IntrinsicId id);
static void validate_map_intrinsic_signature(TypeResolver& resolver, FunctionDefinition* decl, IntrinsicId id);

void TypeResolver::visit(Ast* script)
{
    // Pass 1: Types & Signatures
    for (const auto& decl : script->decls) {
        if (auto struct_decl = dynamic_cast<StructDecl*>(decl)) {
            visit(struct_decl);
        } else if (dynamic_cast<TypeAlias*>(decl)
            || dynamic_cast<NewtypeDecl*>(decl)
            || dynamic_cast<SumDecl*>(decl)
            || dynamic_cast<OpaqueTypeDecl*>(decl)) {
            decl->accept(this);
        } else if (auto func = dynamic_cast<FunctionDefinition*>(decl)) {
            resolve_function_signature(*this, func);
            resolve_operator_alias(*this, func);
        } else if (auto var = dynamic_cast<VarDecl*>(decl)) {
            visit(var);
        } else if (auto decl_list = dynamic_cast<DeclList*>(decl)) {
            decl_list->accept(this);
        }
    }

    // Pass 2: Function Bodies
    for (const auto& decl : script->decls) {
        if (auto func = dynamic_cast<FunctionDefinition*>(decl)) {
            if (func->block) {
                resolve_function_body(*this, func);
            }
        }
    }
}

static void validate_array_intrinsic_signature(TypeResolver& resolver, FunctionDefinition* decl, IntrinsicId id)
{
    auto& rt = nw::kernel::runtime();
    auto& ctx = resolver.ctx;
    auto range = decl->range_;

    auto error = [&](StringView message) -> bool {
        ctx.error(range, message);
        return false;
    };

    auto is_array_intrinsic = [&](IntrinsicId candidate) {
        switch (candidate) {
        case IntrinsicId::ArrayPush:
        case IntrinsicId::ArrayPop:
        case IntrinsicId::ArrayLen:
        case IntrinsicId::ArrayClear:
        case IntrinsicId::ArrayReserve:
        case IntrinsicId::ArrayGet:
        case IntrinsicId::ArraySet:
            return true;
        default:
            return false;
        }
    };

    if (!is_array_intrinsic(id)) { return; }

    if (decl->params.empty()) {
        error("array intrinsic requires array parameter");
        return;
    }
    TypeExpression* array_type_expr = decl->params[0]->type;
    auto array_name = array_type_expr ? extract_identifier(array_type_expr->name) : std::nullopt;
    auto* elem_type_expr = (array_type_expr && array_type_expr->params.size() == 1)
        ? dynamic_cast<TypeExpression*>(array_type_expr->params[0])
        : nullptr;
    auto elem_name = elem_type_expr ? extract_identifier(elem_type_expr->name) : std::nullopt;

    if (!array_type_expr
        || !array_name || *array_name != "array"
        || array_type_expr->params.size() != 1
        || !elem_type_expr
        || !elem_name || !is_type_param_name(*elem_name)) {
        error("array intrinsic expects array!($T) parameter");
        return;
    }

    TypeID array_type_id = decl->params[0]->type->type_id_;
    const Type* array_type = rt.get_type(array_type_id);
    if (array_type && array_type->type_kind == TK_array && !array_type->type_params[1].empty()) {
        error("array intrinsic expects unsized array!($T)");
        return;
    }

    auto param_type = [&](size_t index) -> TypeID { return decl->params[index]->type->type_id_; };
    auto param_type_name = [&](size_t index) -> std::optional<StringView> {
        if (!decl->params[index]->type) { return std::nullopt; }
        return extract_identifier(decl->params[index]->type->name);
    };

    auto has_return = decl->return_type != nullptr;
    auto return_type = has_return ? decl->return_type->type_id_ : invalid_type_id;

    switch (id) {
    case IntrinsicId::ArrayPush:
        if (decl->params.size() != 2)
            error("array.push expects (array!($T), $T)");
        else if (param_type_name(1) != elem_name)
            error("array.push value type must match array element type");
        else if (has_return)
            error("array.push should not declare a return type");
        break;
    case IntrinsicId::ArrayPop:
        if (decl->params.size() != 1)
            error("array.pop expects (array!($T))");
        else if (!has_return || extract_identifier(decl->return_type->name) != elem_name)
            error("array.pop must return $T");
        break;
    case IntrinsicId::ArrayLen:
        if (decl->params.size() != 1)
            error("array.len expects (array!($T))");
        else if (!has_return || return_type != rt.int_type())
            error("array.len must return int");
        break;
    case IntrinsicId::ArrayClear:
        if (decl->params.size() != 1)
            error("array.clear expects (array!($T))");
        else if (has_return)
            error("array.clear should not declare a return type");
        break;
    case IntrinsicId::ArrayReserve:
        if (decl->params.size() != 2)
            error("array.reserve expects (array!($T), int)");
        else if (param_type(1) != rt.int_type())
            error("array.reserve expects int size argument");
        else if (has_return)
            error("array.reserve should not declare a return type");
        break;
    case IntrinsicId::ArrayGet:
        if (decl->params.size() != 2)
            error("array.get expects (array!($T), int)");
        else if (param_type(1) != rt.int_type())
            error("array.get expects int index argument");
        else if (!has_return || extract_identifier(decl->return_type->name) != elem_name)
            error("array.get must return $T");
        break;
    case IntrinsicId::ArraySet:
        if (decl->params.size() != 3)
            error("array.set expects (array!($T), int, $T)");
        else if (param_type(1) != rt.int_type())
            error("array.set expects int index argument");
        else if (param_type_name(2) != elem_name)
            error("array.set value type must match array element type");
        else if (has_return)
            error("array.set should not declare a return type");
        break;
    default:
        break;
    }
}

static void validate_map_intrinsic_signature(TypeResolver& resolver, FunctionDefinition* decl, IntrinsicId id)
{
    auto& rt = nw::kernel::runtime();
    auto& ctx = resolver.ctx;
    auto range = decl->range_;
    auto error = [&](StringView msg) { ctx.error(range, msg); };

    if (id == IntrinsicId::MapLen) {
        if (decl->params.size() != 1)
            error("map.len expects (map!($K, $V))");
        else if (decl->return_type && decl->return_type->type_id_ != rt.int_type())
            error("map.len must return int");
    }
}

static void resolve_function_signature(TypeResolver& resolver, FunctionDefinition* decl)
{
    auto& ctx = resolver.ctx;
    decl->env_ = ctx.env_stack_.back();

    TypeContext type_ctx(ctx);

    ctx.current_type_params_.clear();
    ctx.resolving_generic_func_ = decl;

    for (auto& p : decl->params) {
        if (p->type) {
            p->type->accept(&resolver);
            p->type_id_ = p->type->type_id_;
        }
        if (p->init && p->type_id_ != invalid_type_id) {
            propagate_brace_init_type(p->init, p->type_id_);
            type_ctx.accept(p->init, p->type_id_);
            if (!p->init->is_const_) {
                ctx.errorf(p->init->range_, "default parameter '{}' must be a constant expression",
                    p->identifier_.loc.view());
            }
        }
    }

    if (decl->return_type) {
        decl->return_type->accept(&resolver);
        decl->type_id_ = decl->return_type->type_id_;
    } else {
        decl->type_id_ = nw::kernel::runtime().void_type();
    }

    ctx.resolving_generic_func_ = nullptr;

    bool has_native = ctx.has_annotation(decl, "native");
    bool has_intrinsic = ctx.has_annotation(decl, "intrinsic");
    bool has_body = (decl->block != nullptr);

    if (has_native && has_intrinsic) {
        ctx.error(decl->range_, "Function cannot be both [[native]] and [[intrinsic]]");
        return;
    }

    if ((has_native || has_intrinsic) && has_body) {
        ctx.error(decl->range_, "Function with [[native]] or [[intrinsic]] annotation cannot have a body");
        return;
    } else if (!has_native && !has_intrinsic && !has_body) {
        ctx.error(decl->range_, "Function declaration without body must be annotated with [[native]] or [[intrinsic]]");
        return;
    }

    if (has_intrinsic) {
        auto intrinsic_id = ctx.resolve_intrinsic_id(decl, decl->range_, true);
        if (intrinsic_id) {
            validate_array_intrinsic_signature(resolver, decl, *intrinsic_id);
            validate_map_intrinsic_signature(resolver, decl, *intrinsic_id);
        }
    }

    if (has_native) {
        auto& rt = nw::kernel::runtime();
        const ModuleInterface* native_module = rt.get_native_module(ctx.parent_->name());

        if (!native_module) {
            ctx.errorf(decl->range_, "Module '{}' has [[native]] declarations but no C++ native module is registered",
                ctx.parent_->name());
            return;
        }

        // Generic native declarations are validated at instantiation time.
        if (decl->is_generic()) {
            return;
        }

        auto func_name = String(decl->identifier_.loc.view());
        auto func_it = std::find_if(native_module->functions.begin(), native_module->functions.end(),
            [&](const FunctionMetadata& f) { return f.name == func_name; });

        if (func_it == native_module->functions.end()) {
            ctx.errorf(decl->range_, "Native function '{}' is not registered in C++ module '{}'",
                func_name, ctx.parent_->name());
            return;
        }

        const auto& native_func = *func_it;
        if (!rt.native_types_compatible(native_func.return_type, decl->type_id_)) {
            ctx.errorf(decl->range_, "Native function '{}' return type mismatch: script has '{}', C++ has '{}'",
                func_name,
                decl->type_id_,
                native_func.return_type);
        }

        if (native_func.params.size() != decl->params.size()) {
            ctx.errorf(decl->range_, "Native function '{}' parameter count mismatch: script has {}, C++ has {}",
                func_name,
                decl->params.size(),
                native_func.params.size());
            return;
        }

        for (size_t i = 0; i < decl->params.size(); ++i) {
            TypeID script_type = decl->params[i]->type_id_;
            TypeID cpp_type = native_func.params[i].type_id;
            if (!rt.native_types_compatible(cpp_type, script_type)) {
                ctx.errorf(decl->range_, "Native function '{}' parameter {} type mismatch: script has '{}', C++ has '{}'",
                    func_name,
                    i,
                    script_type,
                    cpp_type);
            }
        }
    }
}

static void resolve_operator_alias(TypeResolver& resolver, FunctionDefinition* func)
{
    auto& ctx = resolver.ctx;
    auto* op_annot = ctx.get_annotation(func, "operator");
    if (!op_annot) {
        return;
    }

    if (op_annot->args.empty()) {
        ctx.error(op_annot->range_, "operator annotation requires operator name as first argument");
        return;
    }

    auto* ident_expr = extract_tail_identifier(op_annot->args[0].value);
    if (!ident_expr) {
        ctx.error(op_annot->range_, "operator annotation expects identifier as operator name");
        return;
    }

    StringView op_name = ident_expr->ident.loc.view();
    TokenType op_type = parse_operator_name(op_name);

    if (op_type == TokenType::INVALID && op_name != "str" && op_name != "hash") {
        ctx.errorf(ident_expr->ident.loc.range, "unknown operator name: '{}'", op_name);
        return;
    }

    bool is_commutative = false;
    if (op_annot->args.size() > 1) {
        if (auto* commut = extract_tail_identifier(op_annot->args[1].value)) {
            if (commut->ident.loc.view() == "commutative") {
                is_commutative = true;
            }
        }
    }

    if (func->params.empty() || func->params.size() > 2) {
        ctx.error(func->identifier_.loc.range, "operator functions must have 1 or 2 parameters");
        return;
    }

    if (is_commutative && func->params.size() != 2) {
        ctx.error(op_annot->range_, "commutative only valid for binary operators (2 parameters)");
        return;
    }

    String module_path = String(ctx.parent_->name());
    String function_name = String(func->identifier_.loc.view());

    TypeID left_type = func->params[0]->type_id_;
    TypeID right_type = (func->params.size() == 2) ? func->params[1]->type_id_ : invalid_type_id;
    TypeID result_type = func->type_id_;

    auto& rt = nw::kernel::runtime();

    bool signature_ok = true;
    if (left_type == invalid_type_id || (func->params.size() == 2 && right_type == invalid_type_id)) {
        signature_ok = false;
    }

    if (op_name == "eq" || op_name == "lt") {
        if (func->params.size() != 2) {
            ctx.errorf(func->identifier_.loc.range, "operator '{}' requires exactly 2 parameters", op_name);
            signature_ok = false;
        }

        if (left_type != right_type) {
            ctx.errorf(func->identifier_.loc.range, "operator '{}' requires both parameters to have the same type", op_name);
            signature_ok = false;
        }

        if (result_type != rt.bool_type()) {
            ctx.errorf(func->identifier_.loc.range, "operator '{}' must return bool", op_name);
            signature_ok = false;
        }
    } else if (op_name == "hash") {
        if (func->params.size() != 1) {
            ctx.error(func->identifier_.loc.range, "operator 'hash' requires exactly 1 parameter");
            signature_ok = false;
        }
        if (result_type != rt.int_type()) {
            ctx.error(func->identifier_.loc.range, "operator 'hash' must return int");
            signature_ok = false;
        }
    } else if (op_name == "str") {
        if (func->params.size() != 1) {
            ctx.error(func->identifier_.loc.range, "operator 'str' requires exactly 1 parameter");
            signature_ok = false;
        }
        if (result_type != rt.string_type()) {
            ctx.error(func->identifier_.loc.range, "operator 'str' must return string");
            signature_ok = false;
        }
    }

    if (!signature_ok) {
        return;
    }

    if (op_name == "eq" || op_name == "hash" || op_name == "lt") {
        rt.register_operator_alias_info(left_type, op_name);
        if (op_name == "eq" && func->params.size() == 2) {
            rt.register_operator_alias_info(right_type, op_name);
        }

        auto& summary = ctx.operator_alias_summary_[left_type];
        if (op_name == "eq") {
            summary.has_eq = true;
            summary.has_explicit_eq = true;
        } else if (op_name == "hash") {
            summary.has_hash = true;
            summary.has_explicit_hash = true;
        } else if (op_name == "lt") {
            summary.has_lt = true;
            summary.has_explicit_lt = true;
        }
    }

    if (op_name == "str") {
        rt.register_str_op(left_type, module_path, function_name);
        return;
    }

    if (op_name == "hash") {
        rt.register_hash_op(left_type, module_path, function_name);
        return;
    }

    if (func->params.size() == 1) {
        rt.register_script_unary_op(op_type, left_type, result_type,
            module_path, function_name);
    } else {
        rt.register_script_binary_op(op_type, left_type, right_type, result_type,
            module_path, function_name);
        if (is_commutative && left_type != right_type) {
            rt.register_script_binary_op(op_type, right_type, left_type, result_type,
                module_path, function_name);
        }
    }
}

static void resolve_function_body(TypeResolver& resolver, FunctionDefinition* decl)
{
    auto& ctx = resolver.ctx;
    ctx.func_def_stack_ = decl;
    ++ctx.function_context_depth_;
    ctx.return_context_stack_.push_back({
        .declared = decl->type_id_,
        .inferred = invalid_type_id,
        .infer = false,
    });

    uint32_t depth = static_cast<uint32_t>(ctx.function_stack_.size());
    ctx.function_stack_.push_back({decl, depth, ctx.scope_stack_.size()});

    ctx.begin_scope();
    for (auto& p : decl->params) {
        ctx.declare_local(p->identifier_, p);
        if (p->init) {
            p->init->accept(&resolver);
        }
    }

    decl->block->accept(&resolver);

    ctx.end_scope();
    ctx.function_stack_.pop_back();
    ctx.return_context_stack_.pop_back();
    --ctx.function_context_depth_;
    ctx.func_def_stack_ = nullptr;
}

void TypeResolver::visit(FunctionDefinition* decl)
{
    // Allow resolving a single function (used for generic instantiation)
    resolve_function_signature(*this, decl);
    if (decl->block) {
        resolve_function_body(*this, decl);
    }
}

static void resolve_struct_field(TypeResolver& resolver, VarDecl* decl)
{
    decl->env_ = resolver.ctx.env_stack_.back();
    if (decl->type && (decl->type->name || decl->type->is_function_type)) {
        decl->type->accept(&resolver);
        decl->type_id_ = decl->type->type_id_;
    }
}

void TypeResolver::visit(StructDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();
    auto* prev_struct = ctx.struct_decl_stack_;
    ctx.struct_decl_stack_ = decl;

    GenericTypeScope generic_scope(ctx, decl);

    for (auto& it : decl->decls) {
        if (auto vd = dynamic_cast<VarDecl*>(it)) {
            resolve_struct_field(*this, vd);
        } else if (auto vdl = dynamic_cast<DeclList*>(it)) {
            for (auto& d : vdl->decls)
                resolve_struct_field(*this, d);
        }
    }

    ctx.struct_decl_stack_ = prev_struct;

    auto& rt = nw::kernel::runtime();
    rt.type_table_.define(decl->type_id_, decl, ctx.parent_->name());

    bool has_native = ctx.has_annotation(decl, "native");
    bool has_propset = ctx.has_annotation(decl, "propset");

    if (has_propset) {
        if (decl->is_generic()) {
            ctx.errorf(decl->range_, "[[propset]] struct '{}' cannot be generic", decl->identifier());
        }

        const Type* type = rt.get_type(decl->type_id_);
        if (!type || type->type_kind != TK_struct) {
            ctx.errorf(decl->range_, "[[propset]] '{}' failed to resolve to a struct type", decl->identifier());
        } else if (type->contains_heap_refs) {
            ctx.errorf(decl->range_, "[[propset]] struct '{}' cannot contain heap references in current phase", decl->identifier());
        }
    }

    if (has_native) {
        const Type* type = rt.get_type(decl->type_id_);
        if (type && type->type_kind == TK_struct && type->type_params[0].is<StructID>()) {
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = rt.type_table_.get(struct_id);

            if (struct_def) {
                String qualified_name = String(ctx.parent_->name()) + "." + String(decl->identifier());
                if (!rt.validate_native_struct(qualified_name, struct_def)) {
                    ctx.errorf(decl->range_, "[[native]] struct '{}' layout does not match registered C++ struct",
                        decl->identifier());
                }
            }
        }
    }
}

void TypeResolver::visit(SumDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();
    GenericTypeScope generic_scope(ctx, decl);

    absl::flat_hash_set<StringView> variant_names;

    for (auto& variant : decl->variants) {
        auto variant_name = variant->identifier_.loc.view();
        if (!variant_names.insert(variant_name).second) {
            ctx.errorf(variant->identifier_.loc.range,
                "duplicate variant '{}' in sum type '{}'", variant_name, decl->identifier());
        }

        if (variant->payload) {
            variant->payload->accept(this);

            if (variant->payload->type_id_ == invalid_type_id && !decl->is_generic()) {
                ctx.errorf(variant->range_, "Unknown payload type for variant '{}' in sum type '{}'",
                    variant_name, decl->identifier());
            }
        }
    }

    nw::kernel::runtime().type_table_.define(decl->type_id_, decl, ctx.parent_->name());
}

void TypeResolver::visit(VariantDecl* decl) { }

void TypeResolver::visit(VarDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();
    if (ctx.in_function_context()) {
        ctx.declare_local(decl->identifier_, decl);
    }

    if (decl->type) {
        decl->type->accept(this);
        decl->type_id_ = decl->type->type_id_;
    } else {
        decl->type_id_ = invalid_type_id;
    }

    if (decl->init) {
        TypeContext tc(ctx);
        propagate_brace_init_type(decl->init, decl->type_id_);
        tc.accept(decl->init, decl->type_id_);
        if (decl->type_id_ == invalid_type_id) {
            decl->type_id_ = decl->init->type_id_;
        }
        if (decl->type_id_ != invalid_type_id && decl->init->type_id_ != invalid_type_id) {
            report_newtype_mismatch(ctx, decl->type_id_, decl->init->type_id_, decl->init->range_);
        }
    }

    if (!decl->init && decl->type_id_ != invalid_type_id) {
        const Type* decl_type = nw::kernel::runtime().get_type(decl->type_id_);
        if (decl_type && decl_type->type_kind == TK_sum) {
            ctx.errorf(decl->identifier_.loc.range,
                "sum type variable '{}' must have an initializer",
                decl->identifier());
        }
    }
}

void TypeResolver::visit(TypeAlias* decl)
{
    decl->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();
    TypeID aliased = rt.type_id(decl->aliased_type->str());
    rt.type_table_.define(decl->type_id_, decl, ctx.parent_->name(), aliased);
}

void TypeResolver::visit(NewtypeDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();
    TypeID wrapped = rt.type_id(decl->wrapped_type->str());
    rt.type_table_.define(decl->type_id_, decl, ctx.parent_->name(), wrapped);
}

void TypeResolver::visit(OpaqueTypeDecl* decl)
{
    decl->env_ = ctx.env_stack_.back();

    bool has_native = ctx.has_annotation(decl, "native");
    if (!has_native) {
        ctx.error(decl->range_, "Opaque type declarations must be annotated with [[native]]");
        return;
    }

    auto& rt = nw::kernel::runtime();
    const ModuleInterface* native_module = rt.get_native_module(ctx.parent_->name());
    if (!native_module) {
        ctx.errorf(decl->range_, "Module '{}' has [[native]] declarations but no C++ native module is registered",
            ctx.parent_->name());
        return;
    }

    auto type_name = String(decl->identifier());
    auto it = std::find(native_module->opaque_types.begin(), native_module->opaque_types.end(), type_name);
    if (it == native_module->opaque_types.end()) {
        ctx.errorf(decl->range_, "Native type '{}' is not registered in C++ module '{}'",
            type_name, ctx.parent_->name());
    }
}

void TypeResolver::visit(AliasedImportDecl* decl) { }
void TypeResolver::visit(SelectiveImportDecl* decl) { }

void TypeResolver::visit(AssignExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();
    if (auto tuple_lhs = dynamic_cast<TupleLiteral*>(expr->lhs)) {
        expr->rhs->accept(this);

        const Type* rhs_type = rt.get_type(expr->rhs->type_id_);
        if (!rhs_type || rhs_type->type_kind != TK_tuple) {
            ctx.errorf(expr->rhs->range_, "not a tuple: '{}'", expr->rhs->type_id_);
            expr->type_id_ = invalid_type_id;
            return;
        }

        size_t element_count = rt.get_tuple_element_count(expr->rhs->type_id_);
        if (element_count != tuple_lhs->elements.size()) {
            ctx.errorf(expr->range_, "tuple assign: expected {}, got {}", element_count, tuple_lhs->elements.size());
            expr->type_id_ = invalid_type_id;
            return;
        }

        for (size_t i = 0; i < tuple_lhs->elements.size(); ++i) {
            auto* target = tuple_lhs->elements[i];
            target->accept(this);

            if (!is_mutable_lvalue(target)) {
                ctx.errorf(target->range_, "cannot assign to const variable");
            }

            TypeID elem_type = rt.get_tuple_element_type(expr->rhs->type_id_, i);
            if (report_newtype_mismatch(ctx, target->type_id_, elem_type, target->range_)) {
                continue;
            }
            if (target->type_id_ != elem_type) {
                ctx.errorf(target->range_, "tuple assign: elem {} expected '{}', got '{}'", i, elem_type, target->type_id_);
            }
        }

        expr->type_id_ = expr->rhs->type_id_;
        return;
    }

    expr->lhs->accept(this);

    if (!is_mutable_lvalue(expr->lhs)) {
        ctx.errorf(expr->lhs->range_, "cannot assign to const variable");
    }

    propagate_brace_init_type(expr->rhs, expr->lhs->type_id_);
    TypeContext tc(ctx);
    tc.accept(expr->rhs, expr->lhs->type_id_);

    if (expr->lhs->type_id_ != invalid_type_id && expr->rhs->type_id_ != invalid_type_id) {
        if (report_newtype_mismatch(ctx, expr->lhs->type_id_, expr->rhs->type_id_, expr->rhs->range_)) {
            expr->type_id_ = invalid_type_id;
            return;
        }

        TypeID type_result = rt.type_check_binary_op(expr->op, expr->lhs->type_id_, expr->rhs->type_id_);
        if (type_result == invalid_type_id) {
            ctx.errorf(expr->op.loc.range, "invalid operands '{}' and '{}' for '{}'",
                expr->lhs->type_id_, expr->rhs->type_id_, expr->op.loc.view());
            expr->type_id_ = invalid_type_id;
            return;
        }

        expr->type_id_ = type_result;
    } else {
        expr->type_id_ = invalid_type_id;
    }
}

void TypeResolver::visit(BinaryExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->lhs->accept(this);
    expr->rhs->accept(this);
    expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;

    TypeID result = nw::kernel::runtime().type_check_binary_op(expr->op, expr->lhs->type_id_, expr->rhs->type_id_);
    if (result == invalid_type_id) {
        ctx.errorf(expr->op.loc.range, "invalid binary op types");
    }
    expr->type_id_ = result;
}

void TypeResolver::visit(BraceInitLiteral* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->is_const_ = true;
    if (expr->type.type != TokenType::INVALID) {
        expr->type_id_ = ctx.resolve_type(expr->type.loc.view(), expr->type.loc.range);
        if (expr->type_id_ == invalid_type_id) {
            ctx.errorf(expr->type.loc.range, "unknown type in struct literal '{}'", expr->type.loc.view());
            return;
        }
    }

    for (size_t idx = 0; idx < expr->items.size(); ++idx) {
        auto& item = expr->items[idx];
        if (item.key) {
            if (expr->init_type != BraceInitType::field) {
                item.key->accept(this);
                expr->is_const_ &= item.key->is_const_;
            }
        }
        if (item.value) {
            if (expr->type_id_ != invalid_type_id) {
                resolve_brace_init_element_type(nw::kernel::runtime(), expr, item, idx);
            }
            item.value->accept(this);
            expr->is_const_ &= item.value->is_const_;
        }
    }

    if (expr->type_id_ != invalid_type_id) {
        validate_brace_init(ctx, expr);
    }
}

void TypeResolver::visit(CallExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();
    TypeContext type_ctx(ctx);
    TypeCompatibility compat{ctx, rt};
    ArgValidator validator{ctx, type_ctx, compat};
    GenericInference inference{ctx, rt};
    CallResolver call_resolver{*this, rt, validator, inference};
    call_resolver.resolve(expr);
}

void TypeResolver::visit(CastExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    if (!expr->expr || !expr->target_type) {
        expr->type_id_ = invalid_type_id;
        expr->is_const_ = false;
        return;
    }

    expr->expr->accept(this);
    TypeID source_type = expr->expr->type_id_;
    TypeID target_type = ctx.resolve_type(expr->target_type, expr->target_type->range_);

    if (source_type == invalid_type_id || target_type == invalid_type_id) {
        expr->type_id_ = invalid_type_id;
        expr->is_const_ = false;
        return;
    }

    auto& rt = nw::kernel::runtime();

    if (expr->op.type == TokenType::IS) {
        expr->type_id_ = rt.bool_type();
        expr->is_const_ = true;
        return;
    }

    const Type* source_info = rt.get_type(source_type);
    const Type* target_info = rt.get_type(target_type);
    if (!source_info || !target_info) {
        expr->type_id_ = invalid_type_id;
        expr->is_const_ = false;
        return;
    }

    bool valid_cast = false;
    bool reported = false;

    if (source_type == rt.any_type() || target_type == rt.any_type()) {
        valid_cast = true;
    } else if (source_type == rt.int_type() && target_type == rt.float_type()) {
        valid_cast = true;
    } else if (source_type == rt.float_type() && target_type == rt.int_type()) {
        valid_cast = true;
    } else if (source_type == rt.int_type() && target_type == rt.bool_type()) {
        valid_cast = true;
    } else if (source_type == rt.bool_type() && target_type == rt.int_type()) {
        valid_cast = true;
    } else if (target_info->type_kind == TK_newtype) {
        TypeID wrapped = target_info->type_params[0].as<TypeID>();
        if (source_type == wrapped) {
            ctx.errorf(expr->range_, "cannot cast from '{}' to '{}'. Use explicit constructor: {}(value)",
                source_type, target_type, target_type);
            reported = true;
            valid_cast = false;
        }
    } else if (source_info->type_kind == TK_newtype) {
        TypeID wrapped = source_info->type_params[0].as<TypeID>();
        if (target_type == wrapped) {
            valid_cast = true;
        }
    }

    if (!valid_cast) {
        if (!reported) {
            ctx.errorf(expr->range_, "invalid cast from '{}' to '{}'", source_type, target_type);
        }
        expr->type_id_ = invalid_type_id;
        expr->is_const_ = false;
    } else {
        expr->type_id_ = target_type;
        expr->is_const_ = expr->expr->is_const_;
    }
}

void TypeResolver::visit(ComparisonExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->lhs->accept(this);
    expr->rhs->accept(this);
    expr->type_id_ = nw::kernel::runtime().bool_type();
    expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;
}

void TypeResolver::visit(ConditionalExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->test->accept(this);
    expr->true_branch->accept(this);
    expr->false_branch->accept(this);
    expr->type_id_ = expr->true_branch->type_id_;
}

void TypeResolver::visit(PathExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();
    TypeContext type_ctx(ctx);

    if (expr->parts.empty()) {
        ctx.error(expr->range_, "empty path expression");
        expr->type_id_ = invalid_type_id;
        return;
    }

    auto resolve_struct_member = [&rt](IdentifierExpression* ident, TypeID struct_type_id) -> bool {
        const Type* type = rt.get_type(struct_type_id);
        if (!type || type->type_kind != TK_struct) {
            return false;
        }
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = rt.type_table_.get(struct_id);
        if (!struct_def) { return false; }
        if (auto* field = find_struct_field(struct_def, ident->ident.loc.view())) {
            ident->type_id_ = field->type_id;
            return true;
        }
        return false;
    };

    expr->parts[0]->accept(this);
    TypeID current_type = expr->parts[0]->type_id_;
    expr->is_const_ = expr->parts[0]->is_const_;

    if (expr->parts.size() == 1) {
        expr->type_id_ = current_type;
        return;
    }

    const Declaration* base_decl = nullptr;
    if (auto base_ident = dynamic_cast<IdentifierExpression*>(expr->parts[0])) {
        base_decl = ctx.resolve(base_ident->ident.loc.view(), base_ident->ident.loc.range);
    }

    for (size_t i = 1; i < expr->parts.size(); ++i) {
        auto* part = expr->parts[i];
        auto* ident = dynamic_cast<IdentifierExpression*>(part);
        if (!ident) {
            ctx.error(part->range_, "path segment must be an identifier");
            expr->type_id_ = invalid_type_id;
            return;
        }

        if (base_decl && i == 1) {
            if (auto import_decl = dynamic_cast<const AliasedImportDecl*>(base_decl)) {
                if (import_decl->loaded_module) {
                    auto exports = import_decl->loaded_module->exports();
                    auto rhs_name = String(ident->ident.loc.view());
                    auto export_ptr = exports.find(rhs_name);
                    if (export_ptr && export_ptr->type_id != invalid_type_id) {
                        ident->env_ = ctx.env_stack_.back();
                        ident->type_id_ = export_ptr->type_id;
                        current_type = export_ptr->type_id;
                        continue;
                    }
                    if (export_ptr) {
                        ctx.errorf(part->range_,
                            "'{}' exists in module '{}' but semantic declaration data is unavailable in current debug level",
                            rhs_name,
                            import_decl->module_path);
                        expr->type_id_ = invalid_type_id;
                        return;
                    }
                    ctx.errorf(part->range_, "'{}' not found in module '{}'", rhs_name, import_decl->module_path);
                    expr->type_id_ = invalid_type_id;
                    return;
                }
            }
        }

        const Type* type = rt.get_type(current_type);
        if (!type) {
            ctx.error(part->range_, "path expression with invalid type");
            expr->type_id_ = invalid_type_id;
            return;
        }

        if (current_type == rt.vec3_type()) {
            auto name = ident->ident.loc.view();
            if (name == "x" || name == "y" || name == "z") {
                current_type = rt.float_type();
                ident->type_id_ = current_type;
                continue;
            }
        }

        if (type->type_kind == TK_sum && i == expr->parts.size() - 1 && expr->parts.size() == 2) {
            if (auto sum_decl = dynamic_cast<const SumDecl*>(base_decl)) {
                TypeID sum_type_id = current_type;
                if (sum_decl->is_generic()) {
                    TypeID expected = ctx.current_expected_type();
                    if (expected != invalid_type_id && is_sum_instantiation(expected, sum_type_id)) {
                        sum_type_id = expected;
                    } else {
                        ctx.errorf(part->range_, "cannot infer type arguments for generic sum '{}'", sum_type_id);
                        expr->type_id_ = invalid_type_id;
                        return;
                    }
                }

                const Type* sum_type = rt.get_type(sum_type_id);
                if (sum_type && sum_type->type_kind == TK_sum) {
                    auto sum_id = sum_type->type_params[0].as<SumID>();
                    const SumDef* sum_def = rt.type_table_.get(sum_id);
                    if (sum_def) {
                        StringView variant_name = ident->ident.loc.view();
                        const VariantDef* variant = sum_def->find_variant(variant_name);
                        if (!variant) {
                            ctx.errorf(part->range_, "variant '{}' not found in sum type '{}'", variant_name, sum_type_id);
                            expr->type_id_ = invalid_type_id;
                            return;
                        }
                        if (variant->payload_type != invalid_type_id) {
                            ctx.errorf(part->range_, "payload variant '{}' must be called with arguments", variant_name);
                            expr->type_id_ = invalid_type_id;
                            return;
                        }
                        ident->type_id_ = sum_type_id;
                        expr->type_id_ = sum_type_id;
                        return;
                    }
                }
            }
        }

        if (!resolve_struct_member(ident, current_type)) {
            ctx.errorf(part->range_, "request for member '{}' in '{}', which is of non-struct type",
                ident->ident.loc.view(), current_type);
            expr->type_id_ = invalid_type_id;
            return;
        }

        current_type = ident->type_id_;
    }

    expr->type_id_ = current_type;
}

void TypeResolver::visit(EmptyExpression* expr) { }

void TypeResolver::visit(GroupingExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    if (expr->expr) {
        expr->expr->accept(this);
        expr->type_id_ = expr->expr->type_id_;
        expr->is_const_ = expr->expr->is_const_;
    }
}

void TypeResolver::visit(TupleLiteral* expr)
{
    expr->env_ = ctx.env_stack_.back();
    Vector<TypeID> types;
    expr->is_const_ = true;
    for (auto* e : expr->elements) {
        e->accept(this);
        types.push_back(e->type_id_);
        expr->is_const_ &= e->is_const_;
    }
    expr->type_id_ = nw::kernel::runtime().register_tuple_type(types);
}

void TypeResolver::visit(IndexExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    if (expr->target) expr->target->accept(this);
    if (expr->index) expr->index->accept(this);
    auto& rt = nw::kernel::runtime();
    const Type* target_type = rt.get_type(expr->target->type_id_);
    expr->is_const_ = expr->target && expr->target->is_const_
        && (!expr->index || expr->index->is_const_);
    if (!target_type) {
        expr->type_id_ = invalid_type_id;
        return;
    }

    if (target_type->type_kind == TK_array
        || target_type->type_kind == TK_fixed_array) {
        if (!expr->index) {
            ctx.error(expr->range_, "array index missing");
            expr->type_id_ = invalid_type_id;
            return;
        }
        if (expr->index->type_id_ != rt.int_type()) {
            ctx.error(expr->range_, "array index must be int");
            expr->type_id_ = invalid_type_id;
            return;
        }
        expr->type_id_ = target_type->type_params[0].as<TypeID>();
        return;
    }

    if (target_type->type_kind == TK_map) {
        if (!expr->index) {
            ctx.error(expr->range_, "map key missing");
            expr->type_id_ = invalid_type_id;
            return;
        }
        TypeID key_type = target_type->type_params[0].as<TypeID>();
        if (expr->index->type_id_ != key_type && expr->index->type_id_ != rt.any_type()) {
            ctx.error(expr->range_, "map key type does not match");
            expr->type_id_ = invalid_type_id;
            return;
        }
        expr->type_id_ = target_type->type_params[1].as<TypeID>();
        return;
    }

    if (target_type->type_kind == TK_tuple) {
        if (!expr->index) {
            ctx.error(expr->range_, "tuple index missing");
            expr->type_id_ = invalid_type_id;
            return;
        }
        auto index_val = ctx.eval_const_int(expr->index, expr->index->range_);
        if (!index_val) {
            ctx.error(expr->index->range_, "tuple index must be const");
            expr->type_id_ = invalid_type_id;
            return;
        }
        if (*index_val < 0) {
            ctx.errorf(expr->index->range_, "tuple index {} out of bounds", *index_val);
            expr->type_id_ = invalid_type_id;
            return;
        }
        size_t element_count = rt.get_tuple_element_count(expr->target->type_id_);
        if (static_cast<size_t>(*index_val) >= element_count) {
            ctx.errorf(expr->index->range_, "tuple index {} out of bounds for tuple of size {}", *index_val, element_count);
            expr->type_id_ = invalid_type_id;
            return;
        }
        expr->type_id_ = rt.get_tuple_element_type(expr->target->type_id_, static_cast<size_t>(*index_val));
        return;
    }

    ctx.error(expr->range_, "index needs array/map/tuple");
    expr->type_id_ = invalid_type_id;
}

void TypeResolver::visit(LiteralExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->is_const_ = true;
    auto& rt = nw::kernel::runtime();
    switch (expr->literal.type) {
    case TokenType::BOOL_LITERAL_TRUE:
    case TokenType::BOOL_LITERAL_FALSE:
        expr->type_id_ = rt.bool_type();
        break;
    case TokenType::INTEGER_LITERAL:
        expr->type_id_ = rt.int_type();
        break;
    case TokenType::FLOAT_LITERAL:
        expr->type_id_ = rt.float_type();
        break;
    case TokenType::STRING_LITERAL:
    case TokenType::STRING_RAW_LITERAL:
        expr->type_id_ = rt.string_type();
        break;
    default:
        expr->type_id_ = rt.void_type();
        break;
    }
}

void TypeResolver::visit(FStringExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->is_const_ = true;
    for (auto* s : expr->expressions) {
        s->accept(this);
        expr->is_const_ &= s->is_const_;
    }
    expr->type_id_ = nw::kernel::runtime().string_type();
}

void TypeResolver::visit(LogicalExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->lhs->accept(this);
    expr->rhs->accept(this);
    expr->type_id_ = nw::kernel::runtime().bool_type();
    expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;
}

void TypeResolver::visit(TypeExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->type_id_ = ctx.resolve_type(expr, expr->range_);
}

void TypeResolver::visit(UnaryExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    expr->rhs->accept(this);

    auto& rt = nw::kernel::runtime();
    expr->type_id_ = expr->rhs->type_id_;
    if (expr->op.type == TokenType::NOT) {
        const Type* rhs_type = rt.get_type(expr->rhs->type_id_);
        if (rhs_type && rhs_type->type_kind == TK_function) {
            expr->type_id_ = rt.bool_type();
        }
    }

    expr->is_const_ = expr->rhs->is_const_;
}

void TypeResolver::visit(IdentifierExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    auto* decl = ctx.resolve(expr->ident.loc.view(), expr->ident.loc.range);
    if (decl) {
        expr->type_id_ = decl->type_id_;
        expr->is_const_ = decl->is_const_;

        if (ctx.current_lambda_ && ctx.function_stack_.size() > 1) {
            String name(expr->ident.loc.view());
            bool is_module_global = ctx.global_decls_.contains(name)
                && (dynamic_cast<const VarDecl*>(decl) || dynamic_cast<const DeclList*>(decl));

            if (!is_module_global) {
                auto& current_func = ctx.function_stack_.back();
                size_t decl_scope_idx = ctx.find_declaration_scope(name);

                if (decl_scope_idx != SIZE_MAX && decl_scope_idx < current_func.scope_base) {
                    uint32_t declaring_depth = 0;
                    size_t declaring_func_index = 0;

                    for (size_t i = ctx.function_stack_.size() - 1; i > 0; --i) {
                        if (decl_scope_idx >= ctx.function_stack_[i - 1].scope_base) {
                            declaring_depth = ctx.function_stack_[i - 1].depth;
                            declaring_func_index = i - 1;
                            break;
                        }
                    }

                    for (size_t i = declaring_func_index + 1; i < ctx.function_stack_.size(); ++i) {
                        auto* lambda = dynamic_cast<LambdaExpression*>(ctx.function_stack_[i].node);
                        if (!lambda) {
                            continue;
                        }
                        ensure_capture(lambda, name, decl->type_id_, declaring_depth, false);
                    }

                    bool is_upvalue_in_parent = false;
                    if (ctx.function_stack_.size() >= 2) {
                        auto* parent_lambda = dynamic_cast<LambdaExpression*>(ctx.function_stack_[ctx.function_stack_.size() - 2].node);
                        if (parent_lambda) {
                            for (const auto& cap : parent_lambda->captures) {
                                if (cap.name == name) {
                                    is_upvalue_in_parent = true;
                                    break;
                                }
                            }
                        }
                    }

                    ensure_capture(ctx.current_lambda_, name, decl->type_id_, declaring_depth, is_upvalue_in_parent);
                }
            }
        }
    } else {
        auto exp = expr->env_.find(String(expr->ident.loc.view()));
        if (exp && exp->type_id != invalid_type_id) {
            expr->type_id_ = exp->type_id;
            expr->is_const_ = exp->is_const;
        } else {
            auto suggestions = format_suggestions(expr->ident.loc.view(), ctx.collect_env_names(ctx.env_stack_.back()));
            ctx.errorf(expr->range_, "unable to resolve identifier '{}'{}", expr->ident.loc.view(), suggestions);
        }
    }

    for (auto* param : expr->type_params) {
        if (param) {
            param->accept(this);
        }
    }
}

void TypeResolver::visit(LambdaExpression* expr)
{
    expr->env_ = ctx.env_stack_.back();
    auto& rt = nw::kernel::runtime();

    Vector<TypeID> param_types;
    for (auto* param : expr->params) {
        if (param->type) {
            param->type->accept(this);
            param->type_id_ = param->type->type_id_;
        }
        param_types.push_back(param->type_id_);
    }

    TypeID return_type = invalid_type_id;
    bool infer_return = expr->return_type == nullptr;
    if (expr->return_type) {
        expr->return_type->accept(this);
        return_type = expr->return_type->type_id_;
    }

    uint32_t depth = static_cast<uint32_t>(ctx.function_stack_.size());
    ctx.function_stack_.push_back({expr, depth, ctx.scope_stack_.size()});
    LambdaExpression* prev_lambda = ctx.current_lambda_;
    ctx.current_lambda_ = expr;
    ++ctx.function_context_depth_;
    ctx.return_context_stack_.push_back({
        .declared = return_type,
        .inferred = invalid_type_id,
        .infer = infer_return,
    });

    ctx.begin_scope();

    for (auto* param : expr->params) {
        ctx.declare_local(param->identifier_, param);
    }

    if (expr->body) {
        expr->body->accept(this);
    }

    TypeID resolved_return = return_type;
    if (infer_return) {
        resolved_return = ctx.return_context_stack_.back().inferred;
        if (resolved_return == invalid_type_id) {
            resolved_return = rt.void_type();
        }
    }

    ctx.end_scope();
    ctx.function_stack_.pop_back();
    ctx.return_context_stack_.pop_back();
    --ctx.function_context_depth_;
    ctx.current_lambda_ = prev_lambda;

    TypeID func_type = rt.register_function_type(param_types, resolved_return);
    expr->type_id_ = func_type;
}

static void resolve_basic_switch(TypeResolver& resolver, SwitchStatement* stmt)
{
    auto& ctx = resolver.ctx;

    if (!stmt || !stmt->target || !stmt->block) {
        return;
    }

    ctx.begin_scope();
    stmt->block->accept(&resolver);
    ctx.end_scope();
}

static bool resolve_sumtype_switch(TypeResolver& resolver, SwitchStatement* stmt)
{
    auto& ctx = resolver.ctx;

    if (!stmt || !stmt->target || !stmt->block) { return false; }

    auto& rt = nw::kernel::runtime();
    const Type* target_type = rt.type_table_.get(stmt->target->type_id_);
    if (!target_type || target_type->type_kind != TypeKind::TK_sum) { return false; }

    auto sum_id = target_type->type_params[0].as<SumID>();
    const SumDef* sum_def = rt.type_table_.get(sum_id);
    if (!sum_def) { return false; }

    ctx.begin_scope();

    absl::flat_hash_set<StringView> matched_variants;
    bool has_default = false;

    for (auto* node : stmt->block->nodes) {
        auto* label = dynamic_cast<LabelStatement*>(node);
        if (!label) {
            continue;
        }

        if (label->type.type == TokenType::DEFAULT) {
            has_default = true;
            continue;
        }

        if (!label->is_pattern_match) {
            ctx.error(label->type.loc.range,
                "switch on sum type requires pattern matching (use 'case VariantName:' or 'case VariantName(bindings):')");
            continue;
        }

        StringView variant_name;
        if (auto* ident_expr = extract_tail_identifier(label->expr)) {
            variant_name = ident_expr->ident.loc.view();
        } else {
            ctx.error(label->expr ? label->expr->range_ : label->type.loc.range,
                "pattern match case must be a variant name");
            continue;
        }

        const VariantDef* variant_def = sum_def->find_variant(variant_name);
        if (!variant_def) {
            ctx.errorf(label->expr->range_, "'{}' is not a variant of sum type", variant_name);
            continue;
        }

        matched_variants.insert(variant_name);

        if (variant_def->payload_type == invalid_type_id) {
            if (!label->bindings.empty()) {
                ctx.errorf(label->type.loc.range, "variant '{}' has no payload, but {} binding(s) provided",
                    variant_name, label->bindings.size());
            }
        } else {
            const Type* payload_type = rt.type_table_.get(variant_def->payload_type);
            if (payload_type && payload_type->type_kind == TypeKind::TK_tuple) {
                auto tuple_id = payload_type->type_params[0].as<TupleID>();
                auto* tuple_def = rt.type_table_.get(tuple_id);
                if (tuple_def) {
                    size_t expected_bindings = tuple_def->element_count;
                    if (label->bindings.size() != expected_bindings) {
                        ctx.errorf(label->type.loc.range,
                            "variant '{}' expects {} binding(s), but {} provided",
                            variant_name, expected_bindings, label->bindings.size());
                    } else {
                        for (size_t i = 0; i < label->bindings.size(); ++i) {
                            label->bindings[i]->type_id_ = tuple_def->element_types[i];
                            ctx.declare_local(label->bindings[i]->identifier_, label->bindings[i]);
                        }
                    }
                }
            } else {
                if (label->bindings.size() != 1) {
                    ctx.errorf(label->type.loc.range,
                        "variant '{}' expects 1 binding, but {} provided",
                        variant_name, label->bindings.size());
                } else {
                    label->bindings[0]->type_id_ = variant_def->payload_type;
                    ctx.declare_local(label->bindings[0]->identifier_, label->bindings[0]);
                }
            }
        }

        if (label->guard) {
            label->guard->accept(&resolver);
            if (label->guard->type_id_ != rt.bool_type()) {
                ctx.error(label->guard->range_, "pattern guard must be a boolean expression");
            }
        }
    }

    stmt->block->accept(&resolver);
    ctx.end_scope();

    return true;
}

void TypeResolver::visit(BlockStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();

    ctx.begin_scope();
    for (auto* n : stmt->nodes) {
        n->accept(this);
    }
    ctx.end_scope();
}

void TypeResolver::visit(DeclList* stmt)
{
    stmt->env_ = ctx.env_stack_.back();

    if (stmt->decls.empty()) { return; }

    bool is_tuple_destructure = stmt->decls.size() > 1
        && stmt->decls[0]->init != nullptr
        && stmt->decls[0]->init == stmt->decls[1]->init;

    if (is_tuple_destructure) {
        auto* init_expr = stmt->decls[0]->init;
        init_expr->accept(this);

        auto& rt = nw::kernel::runtime();
        const Type* init_type = rt.get_type(init_expr->type_id_);
        if (init_type && init_type->type_kind == TK_tuple) {
            size_t element_count = rt.get_tuple_element_count(init_expr->type_id_);
            if (element_count != stmt->decls.size()) {
                ctx.errorf(stmt->range_, "tuple destructure: expected {}, got {}", element_count, stmt->decls.size());
            } else {
                for (size_t i = 0; i < stmt->decls.size(); ++i) {
                    auto* vd = stmt->decls[i];
                    vd->env_ = ctx.env_stack_.back();

                    if (ctx.in_function_context()) {
                        ctx.declare_local(vd->identifier_, vd);
                    } else {
                        ctx.declare_global(vd->identifier_.loc.view(), vd);
                    }

                    TypeID elem_type = rt.get_tuple_element_type(init_expr->type_id_, i);

                    if (vd->type_id_ != invalid_type_id && vd->type_id_ != elem_type) {
                        ctx.errorf(vd->identifier_.loc.range, "tuple destructure: '{}' type '{}' vs elem {} '{}'",
                            vd->identifier_.loc.view(), vd->type_id_, i, elem_type);
                    } else {
                        vd->type_id_ = elem_type;
                    }
                }
            }
        } else {
            ctx.errorf(init_expr->range_, "not a tuple: '{}'", init_expr->type_id_);
        }
    } else {
        for (auto* d : stmt->decls) {
            d->accept(this);
        }
    }
}

void TypeResolver::visit(EmptyStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();
}

void TypeResolver::visit(ExprStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();
    stmt->expr->accept(this);
}

void TypeResolver::visit(IfStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();
    stmt->expr->accept(this);

    ctx.begin_scope();
    stmt->if_branch->accept(this);
    ctx.end_scope();

    if (stmt->else_branch) {
        ctx.begin_scope();
        stmt->else_branch->accept(this);
        ctx.end_scope();
    }
}

void TypeResolver::visit(ForStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();
    LoopScope guard(ctx);
    ctx.begin_scope();

    if (stmt->init) { stmt->init->accept(this); }
    if (stmt->check) { stmt->check->accept(this); }
    if (stmt->inc) { stmt->inc->accept(this); }

    stmt->block->accept(this);

    ctx.end_scope();
}

void TypeResolver::visit(ForEachStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();

    LoopScope guard(ctx);
    ctx.begin_scope();
    stmt->collection->accept(this);

    const Type* coll_type = nw::kernel::runtime().get_type(stmt->collection->type_id_);
    if (coll_type) {
        if (coll_type->type_kind == TK_array) {
            stmt->is_map_iteration = false;
            stmt->element_type = coll_type->type_params[0].as<TypeID>();
            if (stmt->var) {
                stmt->var->type_id_ = stmt->element_type;
                ctx.declare_local(stmt->var->identifier_, stmt->var);
            }
        } else if (coll_type->type_kind == TK_map) {
            stmt->is_map_iteration = true;
            stmt->key_type = coll_type->type_params[0].as<TypeID>();
            stmt->value_type = coll_type->type_params[1].as<TypeID>();
            if (stmt->key_var) {
                stmt->key_var->type_id_ = stmt->key_type;
                ctx.declare_local(stmt->key_var->identifier_, stmt->key_var);
            }

            if (stmt->value_var) {
                stmt->value_var->type_id_ = stmt->value_type;
                ctx.declare_local(stmt->value_var->identifier_, stmt->value_var);
            }
        }
    }

    stmt->block->accept(this);

    ctx.end_scope();
}

void TypeResolver::visit(JumpStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();

    if (stmt->op.type == TokenType::RETURN && ctx.func_def_stack_ && ctx.func_def_stack_->type_id_ != invalid_type_id) {
        for (auto* expr : stmt->exprs) {
            propagate_brace_init_type(expr, ctx.func_def_stack_->type_id_);
        }
    }

    TypeContext tc(ctx);
    bool propagate_return_type = stmt->exprs.size() == 1
        && ctx.func_def_stack_ && ctx.func_def_stack_->type_id_ != invalid_type_id;
    for (auto* e : stmt->exprs) {
        if (propagate_return_type) {
            tc.accept(e, ctx.func_def_stack_->type_id_);
        } else if (e) {
            e->accept(this);
        }
    }

    auto& rt = nw::kernel::runtime();

    if (stmt->op.type == TokenType::RETURN) {
        if (!ctx.in_function_context()) {
            return;
        }
        if (ctx.infer_return_enabled()) {
            ctx.merge_inferred_return(build_return_type(rt, stmt->exprs), stmt->op.loc.range);
            return;
        }

        TypeID expected_type_id = ctx.current_return_type();
        if (expected_type_id == invalid_type_id) { return; }

        if (stmt->exprs.empty()) {
            if (expected_type_id != rt.void_type()) {
                ctx.errorf(stmt->op.loc.range, "return: expected '{}', got void", expected_type_id);
            }
            return;
        }

        TypeID actual_return = build_return_type(rt, stmt->exprs);
        if (!rt.is_type_convertible(expected_type_id, actual_return)) {
            SourceRange error_range = (stmt->exprs.size() == 1)
                ? stmt->exprs[0]->range_
                : stmt->op.loc.range;
            ctx.errorf(error_range, "return: expected '{}', got '{}'",
                expected_type_id, actual_return);
        }
        return;
    }
}

void TypeResolver::visit(LabelStatement* stmt)
{
    stmt->env_ = ctx.env_stack_.back();

    if (stmt->type.type == TokenType::DEFAULT) { return; }
    if (stmt->is_pattern_match) { return; }

    if (!stmt->expr) { return; }
    stmt->expr->accept(this);

    auto& rt = nw::kernel::runtime();
    if (stmt->expr->type_id_ != rt.int_type() && stmt->expr->type_id_ != rt.string_type()) {
        ctx.errorf(stmt->expr->range_, "could not convert value to integer or string");
    }
}

bool is_expression_sumtype(Expression* expr) noexcept
{
    auto& rt = nw::kernel::runtime();
    const Type* target_type = rt.type_table_.get(expr->type_id_);
    return target_type && target_type->type_kind == TypeKind::TK_sum;
}

void TypeResolver::visit(SwitchStatement* stmt)
{
    if (!stmt || !stmt->target) { return; }
    stmt->env_ = ctx.env_stack_.back();

    ++ctx.switch_stack_;

    stmt->target->accept(this);
    auto& rt = nw::kernel::runtime();
    if (is_expression_sumtype(stmt->target)) {
        resolve_sumtype_switch(*this, stmt);
    } else if (stmt->target->type_id_ == rt.int_type()
        || stmt->target->type_id_ == rt.string_type()) {
        resolve_basic_switch(*this, stmt);
    } else {
        ctx.errorf(stmt->target->range_, "switch quantity must be an integer, string, or sum type");
    }

    --ctx.switch_stack_;
}

} // namespace nw::smalls
