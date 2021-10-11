#pragma once

#include "NssLexer.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nw {

struct Script;

struct FunctionDecl;
struct FunctionDefinition;
struct StructDecl;
struct VarDecl;

struct AssignExpression;
struct BinaryExpression;
struct CallExpression;
struct ConditionalExpression;
struct DotExpression;
struct GroupingExpression;
struct LiteralExpression;
struct LogicalExpression;
struct PostfixExpression;
struct UnaryExpression;
struct VariableExpression;

struct BlockStatement;
struct DoStatement;
struct ExprStatement;
struct IfStatement;
struct ForStatement;
struct JumpStatement;
struct LabelStatement;
struct SwitchStatement;
struct WhileStatement;

struct BaseVisitor {
    virtual ~BaseVisitor() = default;

    virtual void visit(Script* script) = 0;

    // Decls
    virtual void visit(FunctionDecl* decl) = 0;
    virtual void visit(FunctionDefinition* decl) = 0;
    virtual void visit(StructDecl* decl) = 0;
    virtual void visit(VarDecl* decl) = 0;

    // Expressions
    virtual void visit(AssignExpression* expr) = 0;
    virtual void visit(BinaryExpression* expr) = 0;
    virtual void visit(CallExpression* expr) = 0;
    virtual void visit(ConditionalExpression* expr) = 0;
    virtual void visit(DotExpression* expr) = 0;
    virtual void visit(GroupingExpression* expr) = 0;
    virtual void visit(LiteralExpression* expr) = 0;
    virtual void visit(LogicalExpression* expr) = 0;
    virtual void visit(PostfixExpression* expr) = 0;
    virtual void visit(UnaryExpression* expr) = 0;
    virtual void visit(VariableExpression* expr) = 0;

    // Statements
    virtual void visit(BlockStatement* stmt) = 0;
    virtual void visit(DoStatement* stmt) = 0;
    virtual void visit(ExprStatement* stmt) = 0;
    virtual void visit(IfStatement* stmt) = 0;
    virtual void visit(ForStatement* stmt) = 0;
    virtual void visit(JumpStatement* stmt) = 0;
    virtual void visit(LabelStatement* stmt) = 0;
    virtual void visit(SwitchStatement* stmt) = 0;
    virtual void visit(WhileStatement* stmt) = 0;
};

struct AstNode {
    virtual ~AstNode() = default;
    virtual void accept(BaseVisitor* visitor) = 0;
};

#define DEFINE_ACCEPT_VISITOR \
    virtual void accept(BaseVisitor* visitor) override { visitor->visit(this); }

// ---- Expression ------------------------------------------------------------

struct Expression : public AstNode {
    virtual ~Expression() = default;
};

struct AssignExpression : Expression {
    AssignExpression(std::unique_ptr<Expression> left, NssToken op, std::unique_ptr<Expression> right)
        : left{std::move(left)}
        , op{op}
        , right{std::move(right)}
    {
    }

    std::unique_ptr<Expression> left;
    NssToken op;
    std::unique_ptr<Expression> right;

    DEFINE_ACCEPT_VISITOR
};

struct BinaryExpression : Expression {
    BinaryExpression(
        std::unique_ptr<Expression> left,
        NssToken op,
        std::unique_ptr<Expression> right)
        : left{std::move(left)}
        , op{op}
        , right{std::move(right)}
    {
    }

    std::unique_ptr<Expression> left;
    NssToken op;
    std::unique_ptr<Expression> right;

    DEFINE_ACCEPT_VISITOR
};

struct CallExpression : Expression {
    explicit CallExpression(std::unique_ptr<Expression> expr)
        : expr{std::move(expr)}
    {
    }

    std::unique_ptr<Expression> expr;
    std::vector<std::unique_ptr<Expression>> args;

    DEFINE_ACCEPT_VISITOR
};

struct ConditionalExpression : Expression {
    ConditionalExpression(
        std::unique_ptr<Expression> expr,
        std::unique_ptr<Expression> true_branch,
        std::unique_ptr<Expression> false_branch)
        : expr{std::move(expr)}
        , true_branch{std::move(true_branch)}
        , false_branch{std::move(false_branch)}
    {
    }

    std::unique_ptr<Expression> expr;
    std::unique_ptr<Expression> true_branch;
    std::unique_ptr<Expression> false_branch;

    DEFINE_ACCEPT_VISITOR
};

struct DotExpression : Expression {
    DotExpression(
        std::unique_ptr<Expression> left,
        std::unique_ptr<Expression> right)
        : left{std::move(left)}
        , right{std::move(right)}
    {
    }

    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    DEFINE_ACCEPT_VISITOR
};

struct GroupingExpression : Expression {
    explicit GroupingExpression(std::unique_ptr<Expression> expr)
        : expr{std::move(expr)}
    {
    }

    std::unique_ptr<Expression> expr;

    DEFINE_ACCEPT_VISITOR
};

struct LiteralExpression : Expression {
    explicit LiteralExpression(NssToken literal)
        : literal{literal}
    {
    }

    NssToken literal;

    DEFINE_ACCEPT_VISITOR
};

struct LogicalExpression : Expression {
    LogicalExpression(
        std::unique_ptr<Expression> left,
        NssToken op,
        std::unique_ptr<Expression> right)
        : left{std::move(left)}
        , op{op}
        , right{std::move(right)}
    {
    }

    std::unique_ptr<Expression> left;
    NssToken op;
    std::unique_ptr<Expression> right;

    DEFINE_ACCEPT_VISITOR
};

struct PostfixExpression : Expression {
    PostfixExpression(std::unique_ptr<Expression> left, NssToken op)
        : left{std::move(left)}
        , op{op}
    {
    }

    std::unique_ptr<Expression> left;
    NssToken op;

    DEFINE_ACCEPT_VISITOR
};

struct UnaryExpression : Expression {
    UnaryExpression(NssToken op, std::unique_ptr<Expression> right)
        : op{op}
        , right{std::move(right)}
    {
    }

    NssToken op;
    std::unique_ptr<Expression> right;

    DEFINE_ACCEPT_VISITOR
};

struct VariableExpression : Expression {
    explicit VariableExpression(NssToken var)
        : var{var}
    {
    }

    NssToken var;

    DEFINE_ACCEPT_VISITOR
};

// ---- Statements ------------------------------------------------------------

struct Statement : public AstNode {
    virtual ~Statement() = default;
};

struct BlockStatement : public Statement {
    std::vector<std::unique_ptr<Statement>> nodes;

    DEFINE_ACCEPT_VISITOR
};

struct DoStatement : public Statement {
    std::unique_ptr<BlockStatement> block;
    std::unique_ptr<Expression> expr;

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
    std::unique_ptr<Expression> init = nullptr;
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
    NssToken identifier;
    std::vector<std::unique_ptr<Declaration>> params;

    DEFINE_ACCEPT_VISITOR
};

struct FunctionDefinition : public Declaration {
    std::unique_ptr<FunctionDecl> decl;
    std::unique_ptr<BlockStatement> block;

    DEFINE_ACCEPT_VISITOR
};

struct StructDecl : public Declaration {
    std::vector<std::unique_ptr<Declaration>> decls;

    DEFINE_ACCEPT_VISITOR
};

struct VarDecl : public Declaration {
    NssToken identifier;
    std::unique_ptr<Expression> init = nullptr;

    DEFINE_ACCEPT_VISITOR
};

struct Script {
    std::vector<std::unique_ptr<Declaration>> decls;

    void accept(BaseVisitor* visitor)
    {
        visitor->visit(this);
    }
};

struct NssParser {
    explicit NssParser(std::string_view view);

    std::string_view view_;
    NssLexer lexer_;

    std::vector<NssToken> tokens;
    size_t current_ = 0;

    NssToken advance();
    bool check(std::initializer_list<NssTokenType> types) const;
    NssToken consume(NssTokenType type, const std::string& error);
    void error(const std::string& msg) const;
    bool is_end() const;
    bool match(std::initializer_list<NssTokenType> types);
    NssToken lookahead(size_t index) const;
    NssToken peek() const;
    NssToken previous();

    // Expression functions are basically listed from lowest precedence
    // to highest
    std::unique_ptr<Expression> parse_expr();
    std::unique_ptr<Expression> parse_expr_assign();
    std::unique_ptr<Expression> parse_expr_conditional();
    std::unique_ptr<Expression> parse_expr_or();
    std::unique_ptr<Expression> parse_expr_and();
    std::unique_ptr<Expression> parse_expr_bitwise();
    std::unique_ptr<Expression> parse_expr_equality();
    std::unique_ptr<Expression> parse_expr_relational();
    std::unique_ptr<Expression> parse_expr_shift();
    std::unique_ptr<Expression> parse_expr_additive();
    std::unique_ptr<Expression> parse_expr_multiplicative();
    std::unique_ptr<Expression> parse_expr_unary();
    std::unique_ptr<Expression> parse_expr_postfix();
    std::unique_ptr<Expression> parse_expr_primary();
    std::unique_ptr<Expression> parse_expr_group();

    std::unique_ptr<Statement> parse_stmt();
    std::unique_ptr<BlockStatement> parse_stmt_block();
    std::unique_ptr<DoStatement> parse_stmt_do();
    std::unique_ptr<ExprStatement> parse_stmt_expr();
    std::unique_ptr<IfStatement> parse_stmt_if();
    std::unique_ptr<ForStatement> parse_stmt_for();
    std::unique_ptr<LabelStatement> parse_stmt_label();
    std::unique_ptr<JumpStatement> parse_stmt_jump();
    std::unique_ptr<SwitchStatement> parse_stmt_switch();
    std::unique_ptr<WhileStatement> parse_stmt_while();

    Type parse_type();
    std::unique_ptr<Statement> parse_decl();
    std::unique_ptr<Declaration> parse_decl_external();
    std::unique_ptr<Declaration> parse_decl_param();
    std::unique_ptr<StructDecl> parse_decl_struct();
    std::unique_ptr<Declaration> parse_decl_struct_member();
    std::unique_ptr<FunctionDecl> parse_decl_function();
    std::unique_ptr<FunctionDefinition> parse_decl_function_def();
    std::unique_ptr<VarDecl> parse_decl_global_var();

    Script parse_program();
};

} // namespace nw
