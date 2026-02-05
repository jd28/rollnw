#include "Validator.hpp"

#include "AstConstEvaluator.hpp"
#include "runtime.hpp"

#include <absl/container/flat_hash_set.h>

namespace nw::smalls {

namespace {

// -- Control flow analysis ----------------------------------------------------

bool contains_top_level_break(const Statement* stmt, int loop_depth, int switch_depth)
{
    if (!stmt) { return false; }

    if (auto jump = dynamic_cast<const JumpStatement*>(stmt)) {
        if (jump->op.type == TokenType::BREAK) {
            return loop_depth == 0 && switch_depth == 0;
        }
        return false;
    }

    if (auto block = dynamic_cast<const BlockStatement*>(stmt)) {
        for (auto* node : block->nodes) {
            if (contains_top_level_break(node, loop_depth, switch_depth)) {
                return true;
            }
        }
        return false;
    }

    if (auto ifs = dynamic_cast<const IfStatement*>(stmt)) {
        if (ifs->if_branch && contains_top_level_break(ifs->if_branch, loop_depth, switch_depth)) {
            return true;
        }
        if (ifs->else_branch && contains_top_level_break(ifs->else_branch, loop_depth, switch_depth)) {
            return true;
        }
        return false;
    }

    if (auto loop = dynamic_cast<const ForStatement*>(stmt)) {
        if (loop->block) {
            return contains_top_level_break(loop->block, loop_depth + 1, switch_depth);
        }
        return false;
    }

    if (auto sw = dynamic_cast<const SwitchStatement*>(stmt)) {
        if (sw->block) {
            return contains_top_level_break(sw->block, loop_depth, switch_depth + 1);
        }
        return false;
    }

    return false;
}

bool switch_all_paths_return(const SwitchStatement* stmt);

bool all_control_flow_paths_return(const AstNode* node)
{
    if (!node) return false;
    if (auto block = dynamic_cast<const BlockStatement*>(node)) {
        for (const auto& decl : block->nodes) {
            if (all_control_flow_paths_return(decl)) return true;
        }
    } else if (auto jump = dynamic_cast<const JumpStatement*>(node)) {
        if (jump->op.type == TokenType::RETURN) return true;
    } else if (auto ifs = dynamic_cast<const IfStatement*>(node)) {
        return ifs->if_branch && ifs->else_branch
            && all_control_flow_paths_return(ifs->if_branch)
            && all_control_flow_paths_return(ifs->else_branch);
    } else if (auto sw = dynamic_cast<const SwitchStatement*>(node)) {
        return switch_all_paths_return(sw);
    }

    return false;
}

IdentifierExpression* extract_tail_identifier(Expression* expr)
{
    if (auto ident = dynamic_cast<IdentifierExpression*>(expr)) {
        return ident;
    } else if (auto path_expr = dynamic_cast<PathExpression*>(expr)) {
        return path_expr->last_identifier();
    }
    return nullptr;
}

bool switch_all_paths_return(const SwitchStatement* stmt)
{
    if (!stmt || !stmt->block) { return false; }
    const SumDef* sum_def = nullptr;

    if (stmt->target) {
        auto& rt = nw::kernel::runtime();
        const Type* target_type = rt.get_type(stmt->target->type_id_);
        if (target_type && target_type->type_kind == TypeKind::TK_sum) {
            auto sum_id = target_type->type_params[0].as<SumID>();
            sum_def = rt.type_table_.get(sum_id);
        }
    }

    Vector<size_t> labels;
    bool has_default = false;
    absl::flat_hash_set<StringView> sum_variants;

    for (size_t i = 0; i < stmt->block->nodes.size(); ++i) {
        if (auto lbl = dynamic_cast<const LabelStatement*>(stmt->block->nodes[i])) {
            labels.push_back(i);
            if (lbl->type.type == TokenType::DEFAULT) {
                has_default = true;
            } else if (sum_def && lbl->is_pattern_match && lbl->expr) {
                if (auto ident = extract_tail_identifier(lbl->expr)) {
                    sum_variants.insert(ident->ident.loc.view());
                }
            }
        }
    }

    if (labels.empty()) { return false; }

    bool covers_sum = false;
    if (sum_def && sum_def->variant_count > 0) {
        if (sum_variants.size() == sum_def->variant_count) {
            covers_sum = true;
        }
    }

    if (!has_default && !covers_sum) {
        return false;
    }

    Vector<bool> segment_returns(labels.size(), false);

    for (size_t idx = labels.size(); idx-- > 0;) {
        size_t start = labels[idx] + 1;
        size_t end = (idx + 1 < labels.size()) ? labels[idx + 1] : stmt->block->nodes.size();
        bool has_break = false;
        bool direct_return = false;

        for (size_t i = start; i < end; ++i) {
            auto* stmt_node = stmt->block->nodes[i];
            if (!stmt_node) { continue; }

            if (contains_top_level_break(stmt_node, 0, 0)) {
                has_break = true;
            }

            if (!direct_return && all_control_flow_paths_return(stmt_node)) {
                direct_return = true;
            }
        }

        if (direct_return) {
            segment_returns[idx] = true;
        } else if (!has_break && idx + 1 < labels.size()) {
            segment_returns[idx] = segment_returns[idx + 1];
        } else {
            segment_returns[idx] = false;
        }
    }

    for (bool returns : segment_returns) {
        if (!returns) { return false; }
    }

    return true;
}

} // namespace

// == Validator ================================================================

Validator::Validator(AstResolver& ctx)
    : ctx(ctx)
{
}

void Validator::visit(Ast* script)
{
    for (auto* decl : script->decls) {
        if (auto func = dynamic_cast<FunctionDefinition*>(decl)) {
            if (func->block) {
                visit(func);
            }
        }
    }

    validate_operator_consistency();
    validate_map_key_types();
}

void Validator::visit(FunctionDefinition* decl)
{
    ++function_depth_;
    decl->block->accept(this);

    if (decl->type_id_ != nw::kernel::runtime().void_type()
        && !all_control_flow_paths_return(decl->block)) {
        ctx.error(decl->identifier_.loc.range, "missing return");
    }

    --function_depth_;
}

void Validator::visit(BlockStatement* stmt)
{
    for (auto* n : stmt->nodes) {
        n->accept(this);
    }
}

void Validator::visit(IfStatement* stmt)
{
    if (stmt->if_branch) { stmt->if_branch->accept(this); }
    if (stmt->else_branch) { stmt->else_branch->accept(this); }
}

void Validator::visit(ForStatement* stmt)
{
    ++loop_depth_;
    if (stmt->block) {
        stmt->block->accept(this);
    }
    --loop_depth_;
}

void Validator::visit(ForEachStatement* stmt)
{
    ++loop_depth_;
    if (stmt->block) {
        stmt->block->accept(this);
    }
    --loop_depth_;
}

void Validator::visit(SwitchStatement* stmt)
{
    ++switch_depth_;

    if (stmt->target) {
        auto& rt = nw::kernel::runtime();
        const Type* target_type = rt.get_type(stmt->target->type_id_);
        if (target_type && target_type->type_kind == TypeKind::TK_sum) {
            validate_sumtype_exhaustiveness(stmt);
        } else if (stmt->target->type_id_ == rt.int_type()
            || stmt->target->type_id_ == rt.string_type()) {
            validate_basic_switch_cases(stmt);
        }
    }

    if (stmt->block) {
        stmt->block->accept(this);
    }

    --switch_depth_;
}

void Validator::visit(JumpStatement* stmt)
{
    if (stmt->op.type == TokenType::RETURN) {
        if (function_depth_ == 0) {
            ctx.error(stmt->op.loc.range, "return outside function");
        }
    } else if (stmt->op.type == TokenType::CONTINUE && loop_depth_ == 0) {
        ctx.error(stmt->op.loc.range, "continue outside loop");
    } else if (stmt->op.type == TokenType::BREAK && (loop_depth_ == 0 && switch_depth_ == 0)) {
        ctx.error(stmt->op.loc.range, "break outside loop/switch");
    }
}

void Validator::visit(LabelStatement* stmt)
{
    if ((stmt->type.type == TokenType::CASE || stmt->type.type == TokenType::DEFAULT)
        && switch_depth_ == 0) {
        ctx.error(stmt->type.loc.range, "label statement not within switch");
    }

    if (stmt->type.type == TokenType::DEFAULT) { return; }
    if (stmt->is_pattern_match) { return; }

    if (stmt->expr && !stmt->expr->is_const_) {
        ctx.error(stmt->expr->range_, "case must be constant");
    }
}

// -- Sum type exhaustiveness --------------------------------------------------

void Validator::validate_sumtype_exhaustiveness(SwitchStatement* stmt)
{
    if (!stmt || !stmt->target || !stmt->block) { return; }

    auto& rt = nw::kernel::runtime();
    const Type* target_type = rt.type_table_.get(stmt->target->type_id_);
    if (!target_type || target_type->type_kind != TypeKind::TK_sum) { return; }

    auto sum_id = target_type->type_params[0].as<SumID>();
    const SumDef* sum_def = rt.type_table_.get(sum_id);
    if (!sum_def) { return; }

    absl::flat_hash_set<StringView> matched_variants;
    bool has_default = false;

    for (auto* node : stmt->block->nodes) {
        auto* label = dynamic_cast<LabelStatement*>(node);
        if (!label) { continue; }

        if (label->type.type == TokenType::DEFAULT) {
            has_default = true;
            continue;
        }

        if (!label->is_pattern_match) { continue; }

        if (auto* ident_expr = extract_tail_identifier(label->expr)) {
            matched_variants.insert(ident_expr->ident.loc.view());
        }
    }

    if (!has_default && matched_variants.size() < sum_def->variant_count) {
        std::vector<StringView> missing_variants;
        for (uint32_t i = 0; i < sum_def->variant_count; ++i) {
            if (!matched_variants.contains(sum_def->variants[i].name.view())) {
                missing_variants.push_back(sum_def->variants[i].name.view());
            }
        }

        if (!missing_variants.empty()) {
            std::string missing_list;
            for (size_t i = 0; i < missing_variants.size(); ++i) {
                if (i > 0) { missing_list += ", "; }
                missing_list += "'";
                missing_list += std::string(missing_variants[i]);
                missing_list += "'";
            }
            ctx.errorf(stmt->range_, "non-exhaustive pattern matching; missing variants: {}", missing_list);
        }
    }
}

// -- Basic switch case validation ---------------------------------------------

void Validator::validate_basic_switch_cases(SwitchStatement* stmt)
{
    if (!stmt || !stmt->target || !stmt->block) { return; }

    TypeID switch_type = invalid_type_id;
    size_t label_count = 0;
    absl::flat_hash_set<int32_t> integers;
    absl::flat_hash_set<String> strings;

    for (auto* node : stmt->block->nodes) {
        auto* lbl = dynamic_cast<LabelStatement*>(node);
        if (!lbl) { continue; }

        ++label_count;
        if (!lbl->expr) { continue; }

        if (switch_type == invalid_type_id) {
            switch_type = lbl->expr->type_id_;
        } else if (switch_type != lbl->expr->type_id_) {
            ctx.errorf(lbl->expr->range_,
                "mismatched case types in switch statement expected type of '{}' not type of '{}'",
                switch_type, lbl->expr->type_id_);
        }

        AstConstEvaluator eval{ctx.parent_, lbl->expr};
        auto& rt = nw::kernel::runtime();

        if (lbl->expr->type_id_ == rt.int_type()
            && !eval.failed_
            && !eval.result_.empty()
            && eval.result_.back().type_id == rt.int_type()) {
            int32_t value = eval.result_.back().data.ival;
            if (!integers.insert(value).second) {
                ctx.errorf(lbl->expr->range_, "duplicate case statement value: '{}'", value);
            }
        } else if (lbl->expr->type_id_ == rt.string_type()
            && !eval.failed_
            && !eval.result_.empty()
            && eval.result_.back().type_id == rt.string_type()) {
            if (eval.result_.back().data.hptr.value != 0) {
                String value(rt.get_string_view(eval.result_.back().data.hptr));
                if (!strings.insert(value).second) {
                    ctx.errorf(lbl->expr->range_, "duplicate case statement value: '{}'", value);
                }
            }
        }
    }

    if (label_count == 0) {
        ctx.error(stmt->range_, "switch statement must have at least one label statemement");
    }
}

// -- Operator / map key validation --------------------------------------------

void Validator::validate_operator_consistency()
{
    auto& rt = nw::kernel::runtime();

    for (const auto& [type_id, summary] : ctx.operator_alias_summary_) {
        if (type_id == invalid_type_id || type_id == rt.any_type()) {
            continue;
        }
        if (rt.is_handle_type(type_id)) {
            continue;
        }

        const Type* type = rt.get_type(type_id);
        if (!type) {
            continue;
        }

        switch (type->type_kind) {
        case TK_primitive:
        case TK_type_param:
        case TK_any_array:
        case TK_any_map:
            continue;
        default:
            break;
        }

        if (summary.has_explicit_hash && !summary.has_explicit_eq) {
            ctx.errorf(SourceRange{}, "operator 'hash' defined without 'eq' for type '{}'", type_id);
        }

        if (summary.has_explicit_lt && !summary.has_explicit_eq) {
            ctx.errorf(SourceRange{}, "operator 'lt' defined without 'eq' for type '{}'", type_id);
        }
    }
}

void Validator::validate_map_key_type(TypeID key_type, SourceRange range)
{
    auto& rt = nw::kernel::runtime();
    if (rt.supports_map_key_type(key_type)) {
        return;
    }

    const Type* type = rt.get_type(key_type);
    if (key_type == rt.float_type()) {
        ctx.errorf(range, "map key type '{}' is not allowed; keys must be int/string or newtypes over int/string", key_type);
        return;
    }
    if (key_type == rt.bool_type()) {
        ctx.errorf(range, "map key type '{}' is not allowed; keys must be int/string or newtypes over int/string", key_type);
        return;
    }

    if (type && rt.is_handle_type(key_type)) {
        ctx.errorf(range, "map key type '{}' is not allowed; handle keys are disabled", key_type);
        return;
    }

    ctx.errorf(range, "map key type '{}' must be int/string or a newtype over int/string", key_type);
}

void Validator::validate_map_key_types()
{
    for (const auto& check : ctx.map_key_checks_) {
        validate_map_key_type(check.key_type, check.range);
    }
}

} // namespace nw::smalls
