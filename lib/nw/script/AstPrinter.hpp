#pragma once

#include "Ast.hpp"

#include <sstream>
#include <string>

namespace nw::script {

struct AstPrinter : BaseVisitor {
    ~AstPrinter() = default;
    std::stringstream ss;
    int depth = 0;

    virtual void visit(Ast* script) override
    {
        ss << '\n'
           << "(script ";
        ++depth;
        for (const auto& d : script->decls) {
            d->accept(this);
        }
        --depth;
        ss << ")";
    }

    // Decls
    virtual void visit(FunctionDecl* decl) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(decl " << decl->identifier_.loc.view() << " : ";

        if (decl->type.type_qualifier.loc.view().size()) {
            ss << decl->type.type_qualifier.loc.view() << " ";
        }
        ss << decl->type.type_specifier.loc.view();
        if (decl->type.type_specifier.type == NssTokenType::STRUCT) {
            ss << " " << decl->type.struct_id.loc.view();
        }

        ++depth;
        for (const auto& p : decl->params) {
            p->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(FunctionDefinition* decl) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(fun ";

        ++depth;
        decl->decl_inline->accept(this);
        if (decl->block)
            decl->block->accept(this);
        --depth;

        ss << ")";
    }

    virtual void visit(StructDecl* decl) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(struct " << decl->type.struct_id.loc.view();
        ++depth;
        for (const auto& d : decl->decls) {
            d->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(VarDecl* decl) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(decl " << decl->identifier_.loc.view() << " : ";
        if (decl->type.type_qualifier.loc.view().size()) {
            ss << decl->type.type_qualifier.loc.view() << " ";
        }
        ss << decl->type.type_specifier.loc.view();
        if (decl->type.type_specifier.type == NssTokenType::STRUCT) {
            ss << " " << decl->type.struct_id.loc.view();
        }

        if (decl->init) {
            ss << " =";
            ++depth;
            decl->init->accept(this);
            --depth;
        }

        ss << ")";
    }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(assign " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(BinaryExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(binary " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(CallExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(call ";
        ++depth;
        expr->expr->accept(this);

        if (expr->args.size()) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ')
               << "(params";
            ++depth;
            for (const auto& arg : expr->args) {
                arg->accept(this);
            }
            --depth;
        }
        ss << ")";
        --depth;
    }

    virtual void visit(ComparisonExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(binary " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(cond";
        ++depth;
        expr->test->accept(this);

        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(then";
        ++depth;
        expr->true_branch->accept(this);
        --depth;
        ss << ")";

        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(else";
        ++depth;
        expr->false_branch->accept(this);
        --depth;
        ss << ")";
        --depth;
        ss << ")";
    }

    virtual void visit(DotExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(.";

        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(EmptyExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "<empty expr>";
    }

    virtual void visit(GroupingExpression* expr) override
    {
        expr->expr->accept(this);
    }

    virtual void visit(LiteralExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(literal " << expr->literal.loc.view() << ")";
    }

    virtual void visit(LiteralVectorExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(vector " << expr->x.loc.view() << ", " << expr->y.loc.view() << ", " << expr->z.loc.view() << ")";
    }

    virtual void visit(LogicalExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(logical " << expr->op.loc.view() << " ";
        ++depth;
        expr->lhs->accept(this);
        ss << " ";
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(PostfixExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(postfix " << expr->op.loc.view();
        ++depth;
        if (expr->lhs) expr->lhs->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(UnaryExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(unary " << expr->op.loc.view();
        ++depth;
        expr->rhs->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(VariableExpression* expr) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(var "
           << expr->var.loc.view()
           << ")";
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(block";

        ++depth;
        for (const auto& n : stmt->nodes) {
            n->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(DeclList* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(decls";
        ++depth;
        for (const auto& decl : stmt->decls) {
            decl->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(DoStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(do";
        ++depth;
        stmt->block->accept(this);
        --depth;
        ++depth;
        if (stmt->expr) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ')
               << "(check";
            ++depth;
            stmt->expr->accept(this);
            --depth;
            ss << ")";
        }
        --depth;
        ss << ")";
    }

    virtual void visit(EmptyStatement*) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(EmptySatement)";
    }

    virtual void visit(ExprStatement* stmt) override
    {
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(if";
        ++depth;
        stmt->expr->accept(this);

        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(then";
        ++depth;
        stmt->if_branch->accept(this);
        --depth;
        ss << ")";

        if (stmt->else_branch) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ')
               << "(else";
            ++depth;
            stmt->else_branch->accept(this);
            --depth;
            ss << ")";
        }
        --depth;
        ss << ")";
    }

    virtual void visit(ForStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ')
           << "(for";
        ++depth;
        if (stmt->init) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ') << "(init";
            ++depth;
            stmt->init->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->check) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ') << "(check";
            ++depth;
            stmt->check->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->inc) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ')
               << "(increment";
            ++depth;
            stmt->inc->accept(this);
            --depth;
            ss << ")";
        }

        stmt->block->accept(this);
        --depth;
    }

    virtual void visit(JumpStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(" << stmt->op.loc.view();
        ++depth;
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(LabelStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(" << stmt->type.loc.view();
        ++depth;
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(switch";
        ++depth;
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(target";
        ++depth;
        stmt->target->accept(this);
        --depth;
        ss << ")";

        stmt->block->accept(this);

        --depth;
    }

    virtual void visit(WhileStatement* stmt) override
    {
        ss << '\n'
           << std::string(size_t(depth * 2), ' ') << "(while";
        ++depth;
        if (stmt->check) {
            ss << '\n'
               << std::string(size_t(depth * 2), ' ') << "(check";
            ++depth;
            stmt->check->accept(this);
            --depth;
            ss << ")";
        }
        ++depth;
        stmt->block->accept(this);
        --depth;
        --depth;
        ss << ")";
    }
};

} // namespace nw::script
