#pragma once

#include "Ast.hpp"
#include "Lexer.hpp"

#include <stdexcept>

namespace nw::smalls {

struct parser_error : public std::runtime_error {
    parser_error(StringView msg)
        : std::runtime_error(String(msg))
    {
    }
};

struct Script;

struct Parser {
    explicit Parser(StringView view, Context* ctx, Script* parent = nullptr);

    Context* ctx_ = nullptr;
    Script* parent_ = nullptr;
    StringView view_;
    Ast ast_;

    Vector<Token> tokens;
    size_t current_ = 0;
    size_t depth_ = 0;
    size_t error_count_ = 0;
    size_t error_limit_ = 20;
    bool error_limit_reported_ = false;

    /// Advances the token stream
    Token advance();

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note **Does not** advance the token stream
    bool check(std::initializer_list<TokenType> types) const;

    /// @brief Checks if next token matches the start of a declaration
    /// @return True if there is a match
    /// @note **Does not** advance the token stream
    bool check_is_decl() const;

    /// @brief Consumes a token
    /// @param type Type of token to consume
    /// @param error Error message if token type is not matched
    /// @return Matched token
    Token consume(TokenType type, StringView error);

    /// Report diagnostic
    /// @param msg Message to report
    void diagnostic(StringView msg, Token token, bool is_warning = false);

    /// Checks if at end of token stream
    bool is_end() const;

    /// @brief Checks if next token matches a particular type
    /// @param types An initializer list of token types
    /// @return True if there is a match
    /// @note Advances the token stream
    bool match(std::initializer_list<TokenType> types);

    /// Lexes the file
    void lex();

    /// Looks ahead in the token stream
    /// @param index Index to look ahead to, from current token
    Token lookahead(size_t index) const;

    /// Next token in the token stream
    Token peek() const;

    /// Previous token in the token stream
    Token previous();

    /// Advances token stream after an error
    void synchronize(bool allow_rbrace = false);

    // Expression functions are basically listed from lowest precedence
    // to highest
    Expression* parse_expr();
    Expression* parse_expr_assign();
    Expression* parse_expr_conditional();
    Expression* parse_expr_or();
    Expression* parse_expr_and();
    Expression* parse_expr_equality();
    Expression* parse_expr_relational();
    Expression* parse_expr_shift();
    Expression* parse_expr_additive();
    Expression* parse_expr_multiplicative();
    Expression* parse_expr_unary();
    Expression* parse_expr_postfix();
    Expression* parse_expr_primary();
    IdentifierExpression* parse_identifier_expression(Token ident_token);
    BraceInitLiteral* parse_brace_init_literal();
    FStringExpression* parse_fstring(Token fstring_token);

    Statement* parse_stmt();
    BlockStatement* parse_stmt_block();
    ExprStatement* parse_stmt_expr();
    IfStatement* parse_stmt_if();
    Statement* parse_stmt_for();
    ForEachStatement* parse_stmt_foreach();
    LabelStatement* parse_stmt_label();
    void parse_pattern_bindings(LabelStatement* s);
    JumpStatement* parse_stmt_jump();
    SwitchStatement* parse_stmt_switch();

    TypeExpression* parse_type();
    Vector<Annotation> parse_annotations();
    Statement* parse_decl(bool top_level = false);
    StructDecl* parse_decl_struct();
    Declaration* parse_decl_struct_member();
    SumDecl* parse_decl_sum();
    VariantDecl* parse_variant();
    FunctionDefinition* parse_decl_function_def();
    VarDecl* parse_decl_param();

    // Import statements
    ImportDecl* parse_import();
    String parse_module_path();

    /// Parses script
    Ast parse_program();
};

} // namespace nw::smalls
