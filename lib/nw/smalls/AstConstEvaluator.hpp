#pragma once

#include "../util/Variant.hpp"
#include "Smalls.hpp"
#include "Token.hpp"
#include "runtime.hpp"

#include <absl/strings/str_cat.h>

namespace nw::smalls {

// There is still a long way to go here, but it's a start.

struct AstConstEvaluator : public BaseVisitor {
    Script* parent_ = nullptr;
    Expression* node_ = nullptr;
    bool failed_ = false;
    Vector<Value> result_;

    AstConstEvaluator(Script* parent, Expression* node)
        : parent_{parent}
        , node_{node}
    {
        if (is_foldable_type(node->type_id_) && node->is_const_) {
            node->accept(this);
        } else {
            failed_ = true;
        }
    }

    // Helpers
    bool is_foldable_type(TypeID type) const noexcept
    {
        auto& rt = nw::kernel::runtime();
        if (type == rt.int_type() || type == rt.float_type()
            || type == rt.string_type() || type == rt.vec3_type()
            || type == rt.bool_type()) {
            return true;
        }

        const Type* t = rt.get_type(type);
        if (t && t->type_kind == TK_struct) {
            if (!t->type_params[0].is<StructID>()) {
                return false;
            }
            auto struct_id = t->type_params[0].as<StructID>();
            const StructDef* struct_def = rt.type_table_.get(struct_id);
            if (!struct_def) {
                return false;
            }

            for (uint32_t i = 0; i < struct_def->field_count; ++i) {
                if (!is_foldable_type(struct_def->fields[i].type_id)) {
                    return false;
                }
            }
            return true;
        }

        if (t && t->type_kind == TK_tuple) {
            if (!t->type_params[0].is<TupleID>()) {
                return false;
            }
            auto tuple_id = t->type_params[0].as<TupleID>();
            const TupleDef* tuple_def = rt.type_table_.get(tuple_id);
            if (!tuple_def) {
                return false;
            }

            for (uint32_t i = 0; i < tuple_def->element_count; ++i) {
                if (!is_foldable_type(tuple_def->element_types[i])) {
                    return false;
                }
            }
            return true;
        }

        return false;
    }

    void push(Value val) { result_.push_back(val); }

    Value pop()
    {
        Value val = result_.back();
        result_.pop_back();
        return val;
    }

    // Resolve symbol to original decl
    void resolve(IdentifierExpression* var)
    {
        auto s = String(var->ident.loc.view());
        auto local_decl = var->env_.find(s);
        if (local_decl) {
            if (auto vd = dynamic_cast<VarDecl*>(local_decl->decl)) {
                if (vd->init) {
                    vd->init->accept(this);
                    return;
                }
            }
        }

        // Try global scope
        for (auto* decl : parent_->ast().decls) {
            if (auto vd = dynamic_cast<VarDecl*>(decl)) {
                if (vd->identifier_.loc.view() == var->ident.loc.view() && vd->init) {
                    vd->init->accept(this);
                    return;
                }
            }
        }

        failed_ = true;
    }

    // Visitor

    // LCOV_EXCL_START
    virtual void visit(Ast*) override
    {
        // No op
    }

    // Decls
    virtual void visit(FunctionDefinition*) override
    {
        // No op
    }

    virtual void visit(StructDecl*) override
    {
        // No op
    }

    virtual void visit(VarDecl*) override
    {
        // No op
    }

    virtual void visit(TypeAlias*) override
    {
        // No op
    }

    virtual void visit(NewtypeDecl*) override
    {
        // No op
    }

    virtual void visit(SumDecl*) override
    {
        // No op
    }

    virtual void visit(VariantDecl*) override
    {
        // No op
    }

    virtual void visit(OpaqueTypeDecl*) override
    {
        // No op
    }

    virtual void visit(AliasedImportDecl*) override { }
    virtual void visit(SelectiveImportDecl*) override { }

    // Expressions
    virtual void visit(AssignExpression*) override
    {
        // No op
    }

    virtual void visit(BraceInitLiteral* expr) override
    {
        if (expr->init_type == BraceInitType::kv) {
            failed_ = true;
            return;
        }

        auto& rt = nw::kernel::runtime();
        const Type* type = rt.get_type(expr->type_id_);
        if (!type || type->type_kind != TK_struct) {
            failed_ = true;
            return;
        }

        if (!type->type_params[0].is<StructID>()) {
            failed_ = true;
            return;
        }
        auto struct_id = type->type_params[0].as<StructID>();
        const StructDef* struct_def = rt.type_table_.get(struct_id);
        if (!struct_def) {
            failed_ = true;
            return;
        }

        HeapPtr struct_ptr = rt.alloc_struct(expr->type_id_);
        if (struct_ptr.value == 0) {
            failed_ = true;
            return;
        }

        void* struct_data = rt.heap_.get_ptr(struct_ptr);
        if (!struct_data) {
            failed_ = true;
            return;
        }

        if (expr->init_type == BraceInitType::list) {
            if (expr->items.size() > struct_def->field_count) {
                failed_ = true;
                return;
            }

            for (size_t i = 0; i < expr->items.size(); ++i) {
                const auto& item = expr->items[i];
                if (!item.value) {
                    failed_ = true;
                    return;
                }

                const FieldDef* field = &struct_def->fields[i];

                item.value->accept(this);
                if (failed_ || result_.empty()) {
                    return;
                }

                Value field_value = pop();

                const Type* field_type = rt.get_type(field->type_id);
                if (!field_type) {
                    failed_ = true;
                    return;
                }

                uint32_t field_size = field_type->size;
                if (field->offset + field_size > type->size) {
                    failed_ = true;
                    return;
                }

                void* field_ptr = static_cast<char*>(struct_data) + field->offset;
                write_value_to_memory(field_value, field_ptr, field->type_id);
            }
        } else if (expr->init_type == BraceInitType::field) {
            for (const auto& item : expr->items) {
                if (!item.key || !item.value) {
                    failed_ = true;
                    return;
                }

                auto var_expr = dynamic_cast<IdentifierExpression*>(item.key);
                if (!var_expr) {
                    failed_ = true;
                    return;
                }
                String field_name(var_expr->ident.loc.view());

                const FieldDef* field = nullptr;
                for (uint32_t i = 0; i < struct_def->field_count; ++i) {
                    if (struct_def->fields[i].name.view() == field_name) {
                        field = &struct_def->fields[i];
                        break;
                    }
                }
                if (!field) {
                    failed_ = true;
                    return;
                }

                item.value->accept(this);
                if (failed_ || result_.empty()) {
                    return;
                }

                Value field_value = pop();

                const Type* field_type = rt.get_type(field->type_id);
                if (!field_type) {
                    failed_ = true;
                    return;
                }

                uint32_t field_size = field_type->size;
                if (field->offset + field_size > type->size) {
                    failed_ = true;
                    return;
                }

                void* field_ptr = static_cast<char*>(struct_data) + field->offset;
                write_value_to_memory(field_value, field_ptr, field->type_id);
            }
        }

        push(Value::make_heap(struct_ptr, expr->type_id_));
    }

    void write_value_to_memory(const Value& val, void* dest, TypeID type_id)
    {
        auto& rt = nw::kernel::runtime();
        if (type_id == rt.int_type()) {
            *static_cast<int32_t*>(dest) = val.data.ival;
        } else if (type_id == rt.float_type()) {
            *static_cast<float*>(dest) = val.data.fval;
        } else if (type_id == rt.bool_type()) {
            *static_cast<bool*>(dest) = val.data.bval;
        } else if (type_id == rt.string_type()) {
            *static_cast<HeapPtr*>(dest) = val.data.hptr;
        } else if (type_id == rt.object_type()) {
            *static_cast<ObjectHandle*>(dest) = val.data.oval;
        } else {
            // For struct/heap types, copy the HeapPtr
            *static_cast<HeapPtr*>(dest) = val.data.hptr;
        }
    }
    // LCOV_EXCL_STOP

    virtual void visit(BinaryExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (result_.size() < 2) {
            failed_ = true;
            return;
        }

        auto rhs = pop();
        auto lhs = pop();

        auto& rt = nw::kernel::runtime();
        auto result = rt.execute_binary_op(expr->op.type, lhs, rhs);

        if (result.type_id == invalid_type_id) {
            failed_ = true;
        } else {
            push(result);
        }
    }

    virtual void visit(CallExpression*) override
    {
        // No Op
    }

    virtual void visit(CastExpression* expr) override
    {
        if (!expr->expr) {
            failed_ = true;
            return;
        }

        expr->expr->accept(this);

        if (failed_ || result_.empty()) {
            failed_ = true;
            return;
        }

        Value val = pop();
        auto& rt = nw::kernel::runtime();

        if (expr->op.type == TokenType::IS) {
            TypeID target_type = expr->target_type->type_id_;
            bool matches = (val.type_id == target_type);
            push(Value::make_bool(matches));
            return;
        }

        const Type* source_type_info = rt.get_type(val.type_id);
        const Type* target_type_info = rt.get_type(expr->type_id_);

        if (!source_type_info || !target_type_info) {
            failed_ = true;
            return;
        }

        Value result_val;

        if (expr->type_id_ == rt.any_type() || val.type_id == rt.any_type()) {
            failed_ = true;
            return;
        } else if (val.type_id == rt.int_type() && expr->type_id_ == rt.float_type()) {
            result_val = Value::make_float(static_cast<float>(val.data.ival));
        } else if (val.type_id == rt.float_type() && expr->type_id_ == rt.int_type()) {
            result_val = Value::make_int(static_cast<int32_t>(val.data.fval));
        } else if (val.type_id == rt.int_type() && expr->type_id_ == rt.bool_type()) {
            result_val = Value::make_bool(val.data.ival != 0);
        } else if (val.type_id == rt.bool_type() && expr->type_id_ == rt.int_type()) {
            result_val = Value::make_int(val.data.bval ? 1 : 0);
        } else if (target_type_info->type_kind == TK_newtype || source_type_info->type_kind == TK_newtype) {
            result_val = val;
            result_val.type_id = expr->type_id_;
        } else {
            failed_ = true;
            return;
        }

        push(result_val);
    }

    virtual void visit(ComparisonExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (result_.size() < 2) {
            failed_ = true;
            return;
        }

        auto rhs = pop();
        auto lhs = pop();

        auto& rt = nw::kernel::runtime();
        auto result = rt.execute_binary_op(expr->op.type, lhs, rhs);

        if (result.type_id == invalid_type_id) {
            failed_ = true;
        } else {
            push(result);
        }
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        expr->test->accept(this);
        if (result_.size() < 1) {
            failed_ = true;
            return;
        }

        auto test = pop();
        if (test.data.bval) {
            expr->true_branch->accept(this);
        } else {
            expr->false_branch->accept(this);
        }
    }

    virtual void visit(PathExpression* expr) override
    {
        auto& rt = nw::kernel::runtime();
        if (expr->parts.empty()) {
            failed_ = true;
            return;
        }

        expr->parts[0]->accept(this);
        if (failed_ || result_.empty()) {
            failed_ = true;
            return;
        }

        Value current_value = pop();
        if (expr->parts.size() == 1) {
            push(current_value);
            return;
        }

        for (size_t i = 1; i < expr->parts.size(); ++i) {
            auto ident = dynamic_cast<IdentifierExpression*>(expr->parts[i]);
            if (!ident) {
                failed_ = true;
                return;
            }

            String field_name(ident->ident.loc.view());
            Value field_value = rt.read_struct_field(current_value.data.hptr, current_value.type_id, field_name);
            if (field_value.type_id == invalid_type_id) {
                failed_ = true;
                return;
            }
            current_value = field_value;
        }

        push(current_value);
    }

    // LCOV_EXCL_START
    virtual void visit(EmptyExpression*) override
    {
        // No Op
    }
    // LCOV_EXCL_STOP

    virtual void visit(GroupingExpression* expr) override
    {
        if (expr->expr) {
            expr->expr->accept(this);
        }
    }

    virtual void visit(TupleLiteral* expr) override
    {
        auto& rt = nw::kernel::runtime();
        const Type* type = rt.get_type(expr->type_id_);
        if (!type || type->type_kind != TK_tuple) {
            failed_ = true;
            return;
        }

        auto tuple_id = type->type_params[0].get<TupleID>();
        if (!tuple_id) {
            failed_ = true;
            return;
        }

        const TupleDef* tuple_def = rt.type_table_.get(*tuple_id);
        if (!tuple_def) {
            failed_ = true;
            return;
        }

        HeapPtr tuple_ptr = rt.alloc_tuple(expr->type_id_);
        if (tuple_ptr.value == 0) {
            failed_ = true;
            return;
        }

        void* tuple_data = rt.heap_.get_ptr(tuple_ptr);
        if (!tuple_data) {
            failed_ = true;
            return;
        }

        if (expr->elements.size() > tuple_def->element_count) {
            failed_ = true;
            return;
        }

        for (size_t i = 0; i < expr->elements.size(); ++i) {
            expr->elements[i]->accept(this);
            if (failed_ || result_.empty()) {
                return;
            }

            Value element_value = pop();
            TypeID element_type = tuple_def->element_types[i];
            uint32_t offset = tuple_def->offsets[i];

            void* element_ptr = static_cast<char*>(tuple_data) + offset;
            write_value_to_memory(element_value, element_ptr, element_type);
        }

        push(Value::make_heap(tuple_ptr, expr->type_id_));
    }

    virtual void visit(IndexExpression* expr) override
    {
        auto& rt = nw::kernel::runtime();

        if (!expr->target || !expr->index) {
            failed_ = true;
            return;
        }

        expr->target->accept(this);
        if (failed_ || result_.empty()) {
            failed_ = true;
            return;
        }
        Value target_value = pop();

        const Type* target_type = rt.get_type(target_value.type_id);
        if (!target_type || target_type->type_kind != TK_tuple) {
            failed_ = true;
            return;
        }

        auto index_lit = dynamic_cast<LiteralExpression*>(expr->index);
        if (!index_lit || !index_lit->data.is<int32_t>()) {
            failed_ = true;
            return;
        }

        int32_t index = index_lit->data.as<int32_t>();
        if (index < 0) {
            failed_ = true;
            return;
        }

        Value element_value = rt.read_tuple_element(target_value.data.hptr, target_value.type_id, static_cast<uint32_t>(index));
        if (element_value.type_id == invalid_type_id) {
            failed_ = true;
            return;
        }

        push(element_value);
    }

    virtual void visit(LiteralExpression* expr) override
    {
        auto& rt = nw::kernel::runtime();

        if (expr->literal.type == TokenType::BOOL_LITERAL_TRUE) {
            push(Value::make_bool(true));
        } else if (expr->literal.type == TokenType::BOOL_LITERAL_FALSE) {
            push(Value::make_bool(false));
        } else if (expr->data.is<int32_t>()) {
            auto ival = expr->data.as<int32_t>();
            if (expr->type_id_ == rt.bool_type()) {
                push(Value::make_bool(ival != 0));
            } else {
                push(Value::make_int(ival));
            }
        } else if (expr->data.is<float>()) {
            push(Value::make_float(expr->data.as<float>()));
        } else if (expr->data.is<PString>()) {
            auto str_ptr = rt.alloc_string(expr->data.as<PString>());
            push(Value::make_string(str_ptr));
        } else {
            failed_ = true;
        }
    }

    virtual void visit(FStringExpression* expr) override
    {
        auto& rt = nw::kernel::runtime();
        String result;

        for (size_t i = 0; i < expr->parts.size(); ++i) {
            absl::StrAppend(&result, StringView(expr->parts[i]));

            if (i < expr->expressions.size()) {
                expr->expressions[i]->accept(this);
                if (failed_ || result_.empty()) {
                    failed_ = true;
                    return;
                }

                Value val = pop();
                if (val.type_id == rt.int_type()) {
                    absl::StrAppend(&result, val.data.ival);
                } else if (val.type_id == rt.float_type()) {
                    absl::StrAppend(&result, val.data.fval);
                } else if (val.type_id == rt.bool_type()) {
                    absl::StrAppend(&result, val.data.bval ? "true" : "false");
                } else if (val.type_id == rt.string_type()) {
                    if (val.data.hptr.value != 0) {
                        absl::StrAppend(&result, rt.get_string_view(val.data.hptr));
                    }
                } else {
                    failed_ = true;
                    return;
                }
            }
        }

        auto str_ptr = rt.alloc_string(PString(result, rt.type_table_.allocator_));
        push(Value::make_string(str_ptr));
    }

    virtual void visit(LogicalExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (result_.size() < 2) {
            failed_ = true;
            return;
        }

        auto rhs = pop();
        auto lhs = pop();

        auto& rt = nw::kernel::runtime();
        auto result = rt.execute_binary_op(expr->op.type, lhs, rhs);

        if (result.type_id == invalid_type_id) {
            failed_ = true;
        } else {
            push(result);
        }
    }

    virtual void visit(UnaryExpression* expr) override
    {
        expr->rhs->accept(this);
        if (result_.size() < 1) {
            failed_ = true;
            return;
        }

        auto rhs = pop();

        auto& rt = nw::kernel::runtime();
        auto result = rt.execute_unary_op(expr->op.type, rhs);

        if (result.type_id == invalid_type_id) {
            failed_ = true;
        } else {
            push(result);
        }
    }

    virtual void visit(TypeExpression*) override
    {
        // No Op
    }

    virtual void visit(IdentifierExpression* expr) override
    {
        // Value of decl init will be at top of stack
        resolve(expr);
    }

    virtual void visit(LambdaExpression*) override
    {
        // Lambdas are not constant expressions
        failed_ = true;
    }

    // LCOV_EXCL_START
    // Statements
    virtual void visit(BlockStatement*) override
    {
        // No Op
    }

    virtual void visit(DeclList*) override
    {
        // No Op
    }

    virtual void visit(EmptyStatement*) override
    {
        // No Op
    }

    virtual void visit(ExprStatement*) override
    {
        // No Op
    }

    virtual void visit(IfStatement*) override
    {
        // No Op
    }

    virtual void visit(ForStatement*) override
    {
        // No Op
    }

    virtual void visit(ForEachStatement*) override
    {
        // No Op
    }

    virtual void visit(JumpStatement*) override
    {
        // No Op
    }

    virtual void visit(LabelStatement*) override
    {
        // No Op
    }

    virtual void visit(SwitchStatement*) override
    {
        // No Op
    }

    // LCOV_EXCL_STOP
};

} // namespace nw::smalls
