#pragma once

#include "NssLexer.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nw::script {

struct parser_error : public std::runtime_error {
    parser_error(std::string_view msg)
        : std::runtime_error(std::string(msg))
    {
    }
};

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

struct AstNode {
    virtual ~AstNode() = default;
    virtual void accept(BaseVisitor* visitor) = 0;
};

#define DEFINE_ACCEPT_VISITOR                          \
    virtual void accept(BaseVisitor* visitor) override \
    {                                                  \
        visitor->visit(this);                          \
    }

// ---- Expression ------------------------------------------------------------

struct Expression : public AstNode {
    virtual ~Expression() = default;
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

    DEFINE_ACCEPT_VISITOR
};

struct CallExpression : Expression {
    explicit CallExpression(std::unique_ptr<Expression> expr_)
        : expr{std::move(expr_)}
    {
    }

    std::unique_ptr<Expression> expr;
    std::vector<std::unique_ptr<Expression>> args;

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

    DEFINE_ACCEPT_VISITOR
};

struct DotExpression : Expression {
    DotExpression(
        std::unique_ptr<Expression> lhs_,
        std::unique_ptr<Expression> rhs_)
        : lhs{std::move(lhs_)}
        , rhs{std::move(rhs_)}
    {
    }

    std::unique_ptr<Expression> lhs;
    std::unique_ptr<Expression> rhs;

    DEFINE_ACCEPT_VISITOR
};

struct GroupingExpression : Expression {
    explicit GroupingExpression(std::unique_ptr<Expression> expr_)
        : expr{std::move(expr_)}
    {
    }

    std::unique_ptr<Expression> expr;

    DEFINE_ACCEPT_VISITOR
};

struct LiteralExpression : Expression {
    explicit LiteralExpression(NssToken token)
        : literal{token}
    {
    }

    NssToken literal;

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

    DEFINE_ACCEPT_VISITOR
};

struct VariableExpression : Expression {
    explicit VariableExpression(NssToken token)
        : var{token}
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
    FunctionDecl() = default;
    FunctionDecl(FunctionDecl&) = delete;
    FunctionDecl& operator=(const FunctionDecl&) = delete;

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
    Script() = default;
    Script(const Script&) = delete;
    Script(Script&&) = default;
    Script& operator=(const Script&) = delete;
    Script& operator=(Script&&) = default;

    std::vector<std::unique_ptr<Statement>> decls;
    std::vector<std::string> includes;
    std::vector<std::pair<std::string, std::string>> defines;

    void accept(BaseVisitor* visitor)
    {
        visitor->visit(this);
    }
};

struct NssParser {
    explicit NssParser(std::string_view view);

    std::string_view view_;
    NssLexer lexer_;
    std::function<void(std::string_view, NssToken)> error_callback_;
    std::function<void(std::string_view, NssToken)> warning_callback_;

    std::vector<NssToken> tokens;
    size_t current_ = 0;
    size_t errors_ = 0;
    size_t warnings_ = 0;

    /// Advances the token stream
    NssToken advance();

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note **Does not** advance the token stream
    bool check(std::initializer_list<NssTokenType> types) const;

    /// @brief Consumes a token
    /// @param type Type of token to consume
    /// @param error Error message if token type is not matched
    /// @return Matched token
    NssToken consume(NssTokenType type, std::string_view error);

    /// Log / Throw error
    /// @param msg Error message to report
    void error(std::string_view msg);

    /// Log warning
    /// @param msg Warning message to report
    void warn(std::string_view msg);

    /// Checks if at end of token stream
    bool is_end() const;

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note Advances the token stream
    bool match(std::initializer_list<NssTokenType> types);

    /// Looks ahead in the token stream
    /// @param index Index to look ahead to, from current token
    NssToken lookahead(size_t index) const;

    /// Next token in the token stream
    NssToken peek() const;

    /// Previous token in the token stream
    NssToken previous();

    /// Sets a callback when an error is encounter
    void set_error_callback(std::function<void(std::string_view message, NssToken)> cb);

    /// Sets a callback when an warning is encounter
    void set_warning_callback(std::function<void(std::string_view message, NssToken)> cb);

    /// Advances token stream after an error
    void synchronize();

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
    std::unique_ptr<Statement> parse_decl_external();
    std::unique_ptr<Declaration> parse_decl_param();
    std::unique_ptr<StructDecl> parse_decl_struct();
    std::unique_ptr<Declaration> parse_decl_struct_member();
    std::unique_ptr<FunctionDecl> parse_decl_function();
    std::unique_ptr<Declaration> parse_decl_function_def();
    std::unique_ptr<Statement> parse_decl_global_var();

    /// Parses script
    Script parse_program();
};

} // namespace nw::script
