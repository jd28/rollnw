#pragma once

#include "NssParser.hpp"

#include "../log.hpp"

#include <sstream>
#include <string>

namespace nw::script {

struct NssAstPrinter : BaseVisitor {
    ~NssAstPrinter() = default;
    std::stringstream ss;
    int depth = 0;

    virtual void visit(Script* script) override
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(decl " << decl->identifier.id << " : ";

        if (decl->type.type_qualifier.id.size()) {
            ss << decl->type.type_qualifier.id << " ";
        }
        ss << decl->type.type_specifier.id;
        if (decl->type.type_specifier.type == NssTokenType::STRUCT) {
            ss << " " << decl->type.struct_id.id;
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(fun ";

        ++depth;
        decl->decl->accept(this);
        if (decl->block)
            decl->block->accept(this);
        --depth;

        ss << ")";
    }

    virtual void visit(StructDecl* decl) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(struct " << decl->type.struct_id.id;
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(decl " << decl->identifier.id << " : ";
        if (decl->type.type_qualifier.id.size()) {
            ss << decl->type.type_qualifier.id << " ";
        }
        ss << decl->type.type_specifier.id;
        if (decl->type.type_specifier.type == NssTokenType::STRUCT) {
            ss << " " << decl->type.struct_id.id;
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(assign " << expr->op.id;
        ++depth;
        expr->left->accept(this);
        expr->right->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(BinaryExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(binary " << expr->op.id;
        ++depth;
        expr->left->accept(this);
        expr->right->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(CallExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(call ";
        ++depth;
        expr->expr->accept(this);

        if (expr->args.size()) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ')
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

    virtual void visit(ConditionalExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(cond";
        ++depth;
        expr->expr->accept(this);

        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(then";
        ++depth;
        expr->true_branch->accept(this);
        --depth;
        ss << ")";

        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(else";
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(.";

        ++depth;
        expr->left->accept(this);
        expr->right->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(GroupingExpression* expr) override
    {
        expr->expr->accept(this);
    }

    virtual void visit(LiteralExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(literal " << expr->literal.id << ")";
    }

    virtual void visit(LiteralVectorExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ')
           << "(vector " << expr->x << ", " << expr->y << ", " << expr->z << ")";
    }

    virtual void visit(LogicalExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(logical " << expr->op.id << " ";
        ++depth;
        expr->left->accept(this);
        ss << " ";
        expr->right->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(PostfixExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(postfix " << expr->op.id;
        ++depth;
        if (expr->left) expr->left->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(UnaryExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(unary " << expr->op.id;
        ++depth;
        expr->right->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(VariableExpression* expr) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ')
           << "(var "
           << expr->var.id
           << ")";
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ')
           << "(block";

        ++depth;
        for (const auto& n : stmt->nodes) {
            n->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(DoStatement* stmt) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ')
           << "(do";
        ++depth;
        stmt->block->accept(this);
        --depth;
        ++depth;
        if (stmt->expr) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ')
               << "(check";
            ++depth;
            stmt->expr->accept(this);
            --depth;
            ss << ")";
        }
        --depth;
        ss << ")";
    }

    virtual void visit(ExprStatement* stmt) override
    {
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt) override
    {
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(if";
        ++depth;
        stmt->expr->accept(this);

        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(then";
        ++depth;
        stmt->if_branch->accept(this);
        --depth;
        ss << ")";

        if (stmt->else_branch) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ')
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
           << std::string(static_cast<size_t>(depth * 2), ' ')
           << "(for";
        ++depth;
        if (stmt->init) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ') << "(init";
            ++depth;
            stmt->init->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->check) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ') << "(check";
            ++depth;
            stmt->check->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->inc) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ')
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(" << stmt->op.id;
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(" << stmt->type.id;
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(switch";
        ++depth;
        ss << '\n'
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(target";
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
           << std::string(static_cast<size_t>(depth * 2), ' ') << "(while";
        ++depth;
        if (stmt->check) {
            ss << '\n'
               << std::string(static_cast<size_t>(depth * 2), ' ') << "(check";
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
