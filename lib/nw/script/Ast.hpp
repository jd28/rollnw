#pragma once

#include "../util/Variant.hpp"
#include "Token.hpp"

#include <glm/glm.hpp>
#include <immer/map.hpp>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace nw::script {

struct Nss;
struct Ast;
struct Export;

struct FunctionDecl;
struct FunctionDefinition;
struct StructDecl;
struct VarDecl;

struct AssignExpression;
struct BinaryExpression;
struct CallExpression;
struct ComparisonExpression;
struct ConditionalExpression;
struct DotExpression;
struct GroupingExpression;
struct LiteralExpression;
struct LiteralVectorExpression;
struct LogicalExpression;
struct PostfixExpression;
struct UnaryExpression;
struct VariableExpression;

struct BlockStatement;
struct DeclStatement;
struct DoStatement;
struct EmptyStatement;
struct ExprStatement;
struct IfStatement;
struct ForStatement;
struct JumpStatement;
struct LabelStatement;
struct SwitchStatement;
struct WhileStatement;

struct BaseVisitor {
    virtual ~BaseVisitor() = default;

    virtual void visit(Ast* script) = 0;

    // Decls
    virtual void visit(FunctionDecl* decl) = 0;
    virtual void visit(FunctionDefinition* decl) = 0;
    virtual void visit(StructDecl* decl) = 0;
    virtual void visit(VarDecl* decl) = 0;

    // Expressions
    virtual void visit(AssignExpression* expr) = 0;
    virtual void visit(BinaryExpression* expr) = 0;
    virtual void visit(CallExpression* expr) = 0;
    virtual void visit(ComparisonExpression* expr) = 0;
    virtual void visit(ConditionalExpression* expr) = 0;
    virtual void visit(DotExpression* expr) = 0;
    virtual void visit(GroupingExpression* expr) = 0;
    virtual void visit(LiteralExpression* expr) = 0;
    virtual void visit(LiteralVectorExpression* expr) = 0;
    virtual void visit(LogicalExpression* expr) = 0;
    virtual void visit(PostfixExpression* expr) = 0;
    virtual void visit(UnaryExpression* expr) = 0;
    virtual void visit(VariableExpression* expr) = 0;

    // Statements
    virtual void visit(BlockStatement* stmt) = 0;
    virtual void visit(DeclStatement* stmt) = 0;
    virtual void visit(DoStatement* stmt) = 0;
    virtual void visit(EmptyStatement* stmt) = 0;
    virtual void visit(ExprStatement* stmt) = 0;
    virtual void visit(IfStatement* stmt) = 0;
    virtual void visit(ForStatement* stmt) = 0;
    virtual void visit(JumpStatement* stmt) = 0;
    virtual void visit(LabelStatement* stmt) = 0;
    virtual void visit(SwitchStatement* stmt) = 0;
    virtual void visit(WhileStatement* stmt) = 0;
};

constexpr size_t invalid_type_id = std::numeric_limits<size_t>::max();

struct AstNode {
    virtual ~AstNode() = default;
    virtual void accept(BaseVisitor* visitor) = 0;

    /// Find completions for this Ast Node
    /// @note This function does not traverse dependencies
    virtual std::vector<std::string> complete(const std::string& needle) const;

    size_t type_id_ = invalid_type_id;
    bool is_const_ = false;
    immer::map<std::string, Export> env;
};

#define DEFINE_ACCEPT_VISITOR                          \
    virtual void accept(BaseVisitor* visitor) override \
    {                                                  \
        visitor->visit(this);                          \
    }

// ---- Expression ------------------------------------------------------------

struct Expression : public AstNode {
    virtual ~Expression() = default;

    virtual SourceLocation extent() const = 0;
};

struct AssignExpression : Expression {
    AssignExpression(std::unique_ptr<Expression> lhs_, NssToken token, std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , op{token}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken op;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct BinaryExpression : Expression {
    BinaryExpression(
        std::unique_ptr<Expression> lhs_,
        NssToken token,
        std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , op{token}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken op;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct CallExpression : Expression {
    explicit CallExpression(std::unique_ptr<Expression> expr_)
        : expr{std::move(expr_)}
    {
    }

    std::unique_ptr<Expression> expr;
    std::vector<std::unique_ptr<Expression>> args;

    virtual SourceLocation extent() const override { return expr->extent(); };

    DEFINE_ACCEPT_VISITOR
};

struct ComparisonExpression : Expression {
    ComparisonExpression(
        std::unique_ptr<Expression> lhs_,
        NssToken token,
        std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , op{token}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken op;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct ConditionalExpression : Expression {
    ConditionalExpression(
        std::unique_ptr<Expression> expr_,
        std::unique_ptr<Expression> true_branch_,
        std::unique_ptr<Expression> false_branch_)
        : test{std::move(expr_)}
        , true_branch{std::move(true_branch_)}
        , false_branch{std::move(false_branch_)}
    {
    }

    std::unique_ptr<Expression> test;
    std::unique_ptr<Expression> true_branch;
    std::unique_ptr<Expression> false_branch;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(test->extent(), merge_source_location(true_branch->extent(), false_branch->extent()));
    };

    DEFINE_ACCEPT_VISITOR
};

struct DotExpression : Expression {
    DotExpression(
        std::unique_ptr<Expression> lhs_,
        NssToken token,
        std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , dot{token}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken dot;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override { return dot.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct GroupingExpression : Expression {
    explicit GroupingExpression(std::unique_ptr<Expression> expr_)
        : expr{std::move(expr_)}
    {
    }

    std::unique_ptr<Expression> expr;

    virtual SourceLocation extent() const override { return expr->extent(); };

    DEFINE_ACCEPT_VISITOR
};

struct LiteralExpression : Expression {
    explicit LiteralExpression(NssToken token)
        : literal{token}
    {
    }

    NssToken literal;
    Variant<int32_t, float, std::string> data;

    virtual SourceLocation extent() const override { return literal.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct LiteralVectorExpression : Expression {
    explicit LiteralVectorExpression(NssToken x_, NssToken y_, NssToken z_)
        : x{x_}
        , y{y_}
        , z{z_}
    {
    }

    NssToken x, y, z;
    glm::vec3 data;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(x.loc, merge_source_location(y.loc, z.loc));
    };

    DEFINE_ACCEPT_VISITOR
};

struct LogicalExpression : Expression {
    LogicalExpression(
        std::unique_ptr<Expression> lhs_,
        NssToken token,
        std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , op{token}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken op;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override { return op.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct PostfixExpression : Expression {
    PostfixExpression(std::unique_ptr<Expression> lhs_, NssToken token)
        : lhs{std::move(lhs_)}
        , op{token}
    {
    }

    std::unique_ptr<Expression> lhs;
    NssToken op;

    virtual SourceLocation extent() const override { return op.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct UnaryExpression : Expression {
    UnaryExpression(NssToken token, std::unique_ptr<Expression> rhs_)
        : op{token}
        , rhs{std::move(rhs_)}
    {
    }

    NssToken op;
    std::unique_ptr<Expression> rhs;

    virtual SourceLocation extent() const override { return op.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct VariableExpression : Expression {
    explicit VariableExpression(NssToken token)
        : var{token}
    {
    }

    NssToken var;

    virtual SourceLocation extent() const override { return var.loc; };

    DEFINE_ACCEPT_VISITOR
};

// ---- Statements ------------------------------------------------------------

struct Statement : public AstNode {
    virtual ~Statement() = default;
};

struct BlockStatement : public Statement {
    BlockStatement() = default;
    BlockStatement(BlockStatement&) = delete;
    BlockStatement& operator=(const BlockStatement&) = delete;

    std::vector<std::unique_ptr<Statement>> nodes;

    DEFINE_ACCEPT_VISITOR
};

/// @brief
struct DeclStatement : public Statement {
    std::vector<std::unique_ptr<VarDecl>> decls;

    DEFINE_ACCEPT_VISITOR
};

struct DoStatement : public Statement {
    std::unique_ptr<BlockStatement> block;
    std::unique_ptr<Expression> expr;

    DEFINE_ACCEPT_VISITOR
};

struct EmptyStatement : public Statement {
    DEFINE_ACCEPT_VISITOR
};

struct ExprStatement : public Statement {
    std::unique_ptr<Expression> expr;

    DEFINE_ACCEPT_VISITOR
};

struct IfStatement : public Statement {
    std::unique_ptr<Expression> expr;
    std::unique_ptr<Statement> if_branch;
    std::unique_ptr<Statement> else_branch = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ForStatement : public Statement {
    std::unique_ptr<AstNode> init = nullptr;
    std::unique_ptr<Expression> check = nullptr;
    std::unique_ptr<Expression> inc = nullptr;
    std::unique_ptr<Statement> block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct JumpStatement : public Statement {
    NssToken op;
    std::unique_ptr<Expression> expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct LabelStatement : public Statement {
    NssToken type;
    std::unique_ptr<Expression> expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct SwitchStatement : public Statement {
    std::unique_ptr<Expression> target;
    std::unique_ptr<Statement> block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct WhileStatement : public Statement {
    std::unique_ptr<Expression> check = nullptr;
    std::unique_ptr<Statement> block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct Type {
    NssToken type_qualifier;
    NssToken type_specifier;
    NssToken struct_id; // Only set if type_specifier if of type NssTokenType::STRUCT
};

struct Declaration : public Statement {
    Type type;
};

struct FunctionDecl : public Declaration {
    FunctionDecl() = default;
    FunctionDecl(FunctionDecl&) = delete;
    FunctionDecl& operator=(const FunctionDecl&) = delete;

    NssToken identifier;
    std::vector<std::unique_ptr<VarDecl>> params;

    DEFINE_ACCEPT_VISITOR
};

struct FunctionDefinition : public Declaration {
    std::unique_ptr<FunctionDecl> decl_inline;
    std::unique_ptr<BlockStatement> block;
    FunctionDecl* decl_external = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct StructDecl : public Declaration {
    std::vector<std::unique_ptr<VarDecl>> decls;

    DEFINE_ACCEPT_VISITOR
};

struct VarDecl : public Declaration {
    NssToken identifier;
    std::unique_ptr<Expression> init = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct Ast {
    Ast() = default;
    Ast(const Ast&) = delete;
    Ast(Ast&&) = default;
    Ast& operator=(const Ast&) = delete;
    Ast& operator=(Ast&&) = default;

    std::vector<std::unique_ptr<Statement>> decls;
    std::vector<std::string> include_resrefs;
    std::vector<Nss*> includes;
    std::vector<std::pair<std::string, std::string>> defines;

    void accept(BaseVisitor* visitor)
    {
        visitor->visit(this);
    }
};

} // namespace nw::script
