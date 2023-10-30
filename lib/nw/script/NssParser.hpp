#pragma once

#include "Ast.hpp"
#include "NssLexer.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nw::script {

struct parser_error : public std::runtime_error {
    parser_error(std::string_view msg)
        : std::runtime_error(std::string(msg))
    {
    }
};

struct Nss;

struct NssParser {
    explicit NssParser(std::string_view view, Context* ctx, Nss* parent = nullptr);

    Context* ctx_ = nullptr;
    Nss* parent_ = nullptr;
    std::string_view view_;
    NssLexer lexer_;

    std::vector<NssToken> tokens;
    size_t current_ = 0;

    /// Advances the token stream
    NssToken advance();

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note **Does not** advance the token stream
    bool check(std::initializer_list<NssTokenType> types) const;

    /// @brief Checks if next token matches a particular type
    /// @return True if there is a match
    /// @note **Does not** advance the token stream
    bool check_is_type() const;

    /// @brief Consumes a token
    /// @param type Type of token to consume
    /// @param error Error message if token type is not matched
    /// @return Matched token
    NssToken consume(NssTokenType type, std::string_view error);

    /// Log / Throw error
    /// @param msg Error message to report
    void error(std::string_view msg, NssToken token);

    /// Log warning
    /// @param msg Warning message to report
    void warn(std::string_view msg, NssToken token);

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
    std::unique_ptr<VarDecl> parse_decl_param();
    std::unique_ptr<StructDecl> parse_decl_struct();
    std::unique_ptr<VarDecl> parse_decl_struct_member();
    std::unique_ptr<FunctionDecl> parse_decl_function();
    std::unique_ptr<Declaration> parse_decl_function_def();
    std::unique_ptr<Statement> parse_decl_global_var();

    /// Parses script
    Ast parse_program();
};

} // namespace nw::script
