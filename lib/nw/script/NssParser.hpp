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
    Ast ast_;

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

    /// Report diagnostic
    /// @param msg Message to report
    void diagnostic(std::string_view msg, NssToken token, bool is_warning = false);

    /// Checks if at end of token stream
    bool is_end() const;

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note Advances the token stream
    bool match(std::initializer_list<NssTokenType> types);

    /// Lexes the file
    void lex();

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
    Expression* parse_expr();
    Expression* parse_expr_assign();
    Expression* parse_expr_conditional();
    Expression* parse_expr_or();
    Expression* parse_expr_and();
    Expression* parse_expr_bitwise();
    Expression* parse_expr_equality();
    Expression* parse_expr_relational();
    Expression* parse_expr_shift();
    Expression* parse_expr_additive();
    Expression* parse_expr_multiplicative();
    Expression* parse_expr_unary();
    Expression* parse_expr_postfix();
    Expression* parse_expr_primary();
    Expression* parse_expr_group();

    Statement* parse_stmt();
    BlockStatement* parse_stmt_block();
    DoStatement* parse_stmt_do();
    ExprStatement* parse_stmt_expr();
    IfStatement* parse_stmt_if();
    ForStatement* parse_stmt_for();
    LabelStatement* parse_stmt_label();
    JumpStatement* parse_stmt_jump();
    SwitchStatement* parse_stmt_switch();
    WhileStatement* parse_stmt_while();

    Type parse_type();
    Statement* parse_decl();
    StructDecl* parse_decl_struct();
    Declaration* parse_decl_function_def();
    FunctionDecl* parse_decl_function();
    VarDecl* parse_decl_param();

    /// Parses script
    Ast parse_program();
};

} // namespace nw::script
