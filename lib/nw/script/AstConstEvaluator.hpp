#include "Nss.hpp"

#include "../util/Variant.hpp"
#include "glm/vec3.hpp"

#include <stack>
#include <string>

namespace nw::script {

// There is still a long way to go here, but it's a start.

struct AstConstEvaluator : public BaseVisitor {
    using variant = Variant<int32_t, float, String, ObjectID, glm::vec3>;

    Nss* parent_ = nullptr;
    Expression* node_ = nullptr;
    bool failed_ = false;
    std::stack<variant> result_;

    AstConstEvaluator(Nss* parent, Expression* node)
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
    bool is_foldable_type(size_t type) const noexcept
    {
        return parent_->ctx()->type_name(type) == "int"
            || parent_->ctx()->type_name(type) == "float"
            || parent_->ctx()->type_name(type) == "string"
            || parent_->ctx()->type_name(type) == "vector";
    }

    // Resolve symbol to original decl
    void resolve(VariableExpression* var)
    {
        auto s = String(var->var.loc.view());
        auto local_decl = var->env_.find(s);
        if (local_decl) {
            if (auto vd = dynamic_cast<VarDecl*>(local_decl->decl)) {
                vd->init->accept(this);
                return;
            }
        }

        // Next look through all dependencies
        for (auto it : reverse(parent_->ctx()->preprocessed_)) {
            if (it.resref == parent_->name()) { break; }
            if (!it.script) { continue; }
            auto symbol = it.script->locate_export(s, false);
            if (auto vd = dynamic_cast<const VarDecl*>(symbol.decl)) {
                AstConstEvaluator eval{it.script, vd->init};
                if (!eval.failed_ && eval.result_.size() > 0) {
                    result_.push(eval.result_.top());
                    return;
                }
            }
        }

        if (!parent_->is_command_script() && parent_->ctx()->command_script_) {
            auto sym = parent_->ctx()->command_script_->locate_export(s, false);
            if (auto vd = dynamic_cast<const VarDecl*>(sym.decl)) {
                AstConstEvaluator eval{parent_->ctx()->command_script_, vd->init};
                if (!eval.failed_ && eval.result_.size() > 0) {
                    result_.push(eval.result_.top());
                    return;
                }
            }
        }
    }

    // Visitor

    // LCOV_EXCL_START
    virtual void visit(Ast*) override
    {
        // No op
    }

    // Decls
    virtual void visit(FunctionDecl*) override
    {
        // No op
    }

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

    // Expressions
    virtual void visit(AssignExpression*) override
    {
        // No op
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

        auto rhs_var = result_.top();
        result_.pop();
        auto lhs_var = result_.top();
        result_.pop();

        if (expr->op.type == NssTokenType::MINUS) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() - rhs_var.as<int32_t>());
            } else if (lhs_var.is<float>() && rhs_var.is<float>()) {
                result_.push(lhs_var.as<float>() - rhs_var.as<float>());
            } else if (lhs_var.is<float>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<float>() - float(rhs_var.as<int32_t>()));
            } else if (lhs_var.is<int32_t>() && rhs_var.is<float>()) {
                result_.push(float(lhs_var.as<int32_t>()) - rhs_var.as<float>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::PLUS) {
            if (lhs_var.is<String>() && rhs_var.is<String>()) {
                result_.push(lhs_var.as<String>() + rhs_var.as<String>());
            } else if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() + rhs_var.as<int32_t>());
            } else if (lhs_var.is<float>() && rhs_var.is<float>()) {
                result_.push(lhs_var.as<float>() + rhs_var.as<float>());
            } else if (lhs_var.is<float>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<float>() + float(rhs_var.as<int32_t>()));
            } else if (lhs_var.is<int32_t>() && rhs_var.is<float>()) {
                result_.push(float(lhs_var.as<int32_t>()) + rhs_var.as<float>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::DIV) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() / rhs_var.as<int32_t>());
            } else if (lhs_var.is<float>() && rhs_var.is<float>()) {
                result_.push(lhs_var.as<float>() / rhs_var.as<float>());
            } else if (lhs_var.is<float>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<float>() / float(rhs_var.as<int32_t>()));
            } else if (lhs_var.is<int32_t>() && rhs_var.is<float>()) {
                result_.push(float(lhs_var.as<int32_t>()) / rhs_var.as<float>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::TIMES) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() * rhs_var.as<int32_t>());
            } else if (lhs_var.is<float>() && rhs_var.is<float>()) {
                result_.push(lhs_var.as<float>() * rhs_var.as<float>());
            } else if (lhs_var.is<float>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<float>() * float(rhs_var.as<int32_t>()));
            } else if (lhs_var.is<int32_t>() && rhs_var.is<float>()) {
                result_.push(float(lhs_var.as<int32_t>()) * rhs_var.as<float>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::MOD) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() % rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::AND) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() & rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::OR) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() | rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::XOR) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() ^ rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::SR) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() >> rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::SL) {
            if (lhs_var.is<int32_t>() && rhs_var.is<int32_t>()) {
                result_.push(lhs_var.as<int32_t>() << rhs_var.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::USR) {
            failed_ = true;
        } else {
            failed_ = true;
        }
    }

    virtual void visit(CallExpression*) override
    {
        // No Op
    }

    virtual void visit(ComparisonExpression*) override
    {
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        expr->test->accept(this);
        if (result_.size() < 1) {
            failed_ = true;
            return;
        }

        auto test = result_.top();
        result_.pop();
        if (!!test.as<int32_t>()) {
            expr->true_branch->accept(this);
        } else {
            expr->false_branch->accept(this);
        }
    }

    // LCOV_EXCL_START
    virtual void visit(DotExpression*) override
    {
        // No Op
    }

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

    virtual void visit(LiteralExpression* expr) override
    {
        if (expr->data.is<int32_t>()) {
            result_.push(expr->data.as<int32_t>());
        } else if (expr->data.is<float>()) {
            result_.push(expr->data.as<float>());
        } else if (expr->data.is<String>()) {
            result_.push(expr->data.as<String>());
        } else {
            failed_ = true;
        }
    }

    virtual void visit(LiteralVectorExpression* expr) override
    {
        result_.push(expr->data);
    }

    virtual void visit(LogicalExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (result_.size() < 2) {
            failed_ = true;
            return;
        }

        auto rhs = result_.top();
        result_.pop();
        auto lhs = result_.top();
        result_.pop();

        if (expr->op.type == NssTokenType::ANDAND) {
            result_.push(int32_t(lhs.as<int32_t>() && rhs.as<int32_t>()));
        } else if (expr->op.type == NssTokenType::OROR) {
            result_.push(int32_t(lhs.as<int32_t>() || rhs.as<int32_t>()));
        } else {
            failed_ = true;
        }
    }

    virtual void visit(PostfixExpression* expr) override
    {
        expr->lhs->accept(this);
        if (result_.size() < 1) {
            failed_ = true;
            return;
        }

        auto lhs = result_.top();
        result_.pop();

        if (expr->op.type == NssTokenType::PLUSPLUS) {
            result_.push(lhs.as<int32_t>() + 1);
        } else if (expr->op.type == NssTokenType::MINUSMINUS) {
            result_.push(lhs.as<int32_t>() - 1);
        } else {
            failed_ = true;
        }
    }

    virtual void visit(UnaryExpression* expr) override
    {
        expr->rhs->accept(this);
        if (result_.size() < 1) {
            failed_ = true;
            return;
        }

        auto rhs = result_.top();
        result_.pop();

        if (expr->op.type == NssTokenType::NOT) {
            if (rhs.is<int32_t>()) {
                result_.push(!rhs.as<int32_t>());
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::MINUS) {
            if (rhs.is<int32_t>()) {
                result_.push(-rhs.as<int32_t>());
            } else if (rhs.is<float>()) {
                result_.push(-rhs.as<float>());
            }
        } else if (expr->op.type == NssTokenType::MINUSMINUS) {
            if (rhs.is<int32_t>()) {
                result_.push(!rhs.as<int32_t>() - 1);
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::PLUS) {
            // No op
        } else if (expr->op.type == NssTokenType::PLUSPLUS) {
            if (rhs.is<int32_t>()) {
                result_.push(rhs.as<int32_t>() + 1);
            } else {
                failed_ = true;
            }
        } else if (expr->op.type == NssTokenType::TILDE) {
            if (rhs.is<int32_t>()) {
                result_.push(~rhs.as<int32_t>());
            } else {
                failed_ = true;
            }
        }
    }

    virtual void visit(VariableExpression* expr) override
    {
        // Value of decl init will be at top of stack
        resolve(expr);
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

    virtual void visit(DoStatement*) override
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

    virtual void visit(WhileStatement*) override
    {
        // No Op
    }
    // LCOV_EXCL_STOP
};

} // namespace nw::script
