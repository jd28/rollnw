#pragma once

#include "../util/Variant.hpp"
#include "Token.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <immer/map.hpp>

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
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
struct DeclList;
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
    virtual void visit(DeclList* stmt) = 0;
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
    virtual void complete(const std::string& needle, std::vector<std::string>& out) const;

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
    AssignExpression(Expression* lhs_, NssToken token, Expression* rhs_)
        : lhs{lhs_}
        , op{token}
        , rhs{rhs_}
    {
    }

    Expression* lhs = nullptr;
    NssToken op;
    Expression* rhs = nullptr;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct BinaryExpression : Expression {
    BinaryExpression(
        Expression* lhs_,
        NssToken token,
        Expression* rhs_)
        : lhs{lhs_}
        , op{token}
        , rhs{rhs_}
    {
    }

    Expression* lhs = nullptr;
    NssToken op;
    Expression* rhs = nullptr;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct CallExpression : Expression {
    explicit CallExpression(Expression* expr_)
        : expr{expr_}
    {
    }

    Expression* expr = nullptr;
    std::vector<Expression*> args;

    virtual SourceLocation extent() const override { return expr->extent(); };

    DEFINE_ACCEPT_VISITOR
};

struct ComparisonExpression : Expression {
    ComparisonExpression(
        Expression* lhs_,
        NssToken token,
        Expression* rhs_)
        : lhs{lhs_}
        , op{token}
        , rhs{rhs_}
    {
    }

    Expression* lhs = nullptr;
    NssToken op;
    Expression* rhs = nullptr;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(lhs->extent(), rhs->extent());
    };

    DEFINE_ACCEPT_VISITOR
};

struct ConditionalExpression : Expression {
    ConditionalExpression(
        Expression* expr_,
        Expression* true_branch_,
        Expression* false_branch_)
        : test{expr_}
        , true_branch{true_branch_}
        , false_branch{false_branch_}
    {
    }

    Expression* test = nullptr;
    Expression* true_branch = nullptr;
    Expression* false_branch = nullptr;

    virtual SourceLocation extent() const override
    {
        return merge_source_location(test->extent(), merge_source_location(true_branch->extent(), false_branch->extent()));
    };

    DEFINE_ACCEPT_VISITOR
};

struct DotExpression : Expression {
    DotExpression(
        Expression* lhs_,
        NssToken token,
        Expression* rhs_)
        : lhs{lhs_}
        , dot{token}
        , rhs{rhs_}
    {
    }

    Expression* lhs = nullptr;
    NssToken dot;
    Expression* rhs = nullptr;

    virtual SourceLocation extent() const override { return dot.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct GroupingExpression : Expression {
    explicit GroupingExpression(Expression* expr_)
        : expr{expr_}
    {
    }

    Expression* expr = nullptr;

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
        Expression* lhs_,
        NssToken token,
        Expression* rhs_)
        : lhs{lhs_}
        , op{token}
        , rhs{rhs_}
    {
    }

    Expression* lhs = nullptr;
    NssToken op;
    Expression* rhs = nullptr;

    virtual SourceLocation extent() const override { return op.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct PostfixExpression : Expression {
    PostfixExpression(Expression* lhs_, NssToken token)
        : lhs{lhs_}
        , op{token}
    {
    }

    Expression* lhs = nullptr;
    NssToken op;

    virtual SourceLocation extent() const override { return op.loc; };

    DEFINE_ACCEPT_VISITOR
};

struct UnaryExpression : Expression {
    UnaryExpression(NssToken token, Expression* rhs_)
        : op{token}
        , rhs{rhs_}
    {
    }

    NssToken op;
    Expression* rhs = nullptr;

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

    SourceRange range;
    std::vector<Statement*> nodes;

    DEFINE_ACCEPT_VISITOR
};

struct DoStatement : public Statement {
    BlockStatement* block = nullptr;
    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct EmptyStatement : public Statement {
    DEFINE_ACCEPT_VISITOR
};

struct ExprStatement : public Statement {
    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct IfStatement : public Statement {
    Expression* expr = nullptr;
    Statement* if_branch = nullptr;
    Statement* else_branch = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct ForStatement : public Statement {
    AstNode* init = nullptr;
    Expression* check = nullptr;
    Expression* inc = nullptr;
    Statement* block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct JumpStatement : public Statement {
    NssToken op;
    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct LabelStatement : public Statement {
    NssToken type;
    Expression* expr = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct SwitchStatement : public Statement {
    Expression* target;
    Statement* block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct WhileStatement : public Statement {
    Expression* check = nullptr;
    Statement* block = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct Type {
    NssToken type_qualifier;
    NssToken type_specifier;
    NssToken struct_id; // Only set if type_specifier if of type NssTokenType::STRUCT
};

struct Declaration : public Statement {
    Type type;
    SourceRange range_;
    SourceRange range_selection_;
    std::string_view view;

    virtual SourceRange range() const noexcept;
    virtual SourceRange selection_range() const noexcept;
};

struct FunctionDecl : public Declaration {
    FunctionDecl() = default;
    FunctionDecl(FunctionDecl&) = delete;
    FunctionDecl& operator=(const FunctionDecl&) = delete;

    NssToken identifier;
    std::vector<VarDecl*> params;

    DEFINE_ACCEPT_VISITOR
};

struct FunctionDefinition : public Declaration {
    FunctionDecl* decl_inline = nullptr;
    BlockStatement* block = nullptr;
    FunctionDecl* decl_external = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct StructDecl : public Declaration {
    std::vector<VarDecl*> decls;

    DEFINE_ACCEPT_VISITOR
};

struct VarDecl : public Declaration {
    NssToken identifier;
    Expression* init = nullptr;

    DEFINE_ACCEPT_VISITOR
};

/// List of comma separated declarations
struct DeclList : public Declaration {
    std::vector<VarDecl*> decls;

    DEFINE_ACCEPT_VISITOR
};

/// Abstracts an script include
struct Include {
    std::string resref;      ///< Resref of included script
    SourceLocation location; ///< Source location in script
    Nss* script = nullptr;   ///< Loaded script
    int used = 0;            ///< Number of times include is used in script file
};

/// Abstracts a comment
struct Comment {

    void append(std::string_view comment, SourceLocation range)
    {
        if (comment_.empty()) {
            comment_ = std::string(comment);
            range_ = merge_source_location(range_, range);
        } else {
            comment_ = fmt::format("{}\n{}", comment_, comment);
            range_ = merge_source_location(range_, range);
        }
    }

    SourceLocation range_;
    std::string comment_;
};

struct Ast {
    Ast() = default;
    Ast(const Ast&) = delete;
    Ast(Ast&&) = default;
    Ast& operator=(const Ast&) = delete;
    Ast& operator=(Ast&&) = default;

    std::vector<Statement*> decls;
    std::vector<Include> includes;
    std::unordered_map<std::string, std::string> defines;
    std::vector<Comment> comments;
    std::vector<size_t> line_map;

    std::vector<std::unique_ptr<AstNode>> nodes_;

    template <typename T, typename... Args>
    T* create_node(Args&&... args)
    {
        // [NOTE] This is to abstract the ownership and allocation of AST nodes,
        // such that it could be replaced by say a slab allocator or something else
        // later.  This is just the most basic way of doing this.
        nodes_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T*>(nodes_.back().get());
    }

    void accept(BaseVisitor* visitor)
    {
        visitor->visit(this);
    }

    /// Finds first comment that the source range of which ends on ``line`` or ``line`` - 1
    std::string_view find_comment(size_t line) const noexcept;

    /// Find last declaration before the specified position
    const Declaration* find_last_declaration(size_t line, size_t character) const;
};

} // namespace nw::script
