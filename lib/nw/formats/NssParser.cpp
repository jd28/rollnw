#include "NssParser.hpp"

#include "../log.hpp"

namespace nw::script {

NssParser::NssParser(std::string_view view)
    : view_{view}
    , lexer_{view_}
{
    NssToken tok = lexer_.next();
    while (tok.type != NssTokenType::END) {
        tokens.push_back(tok);
        tok = lexer_.next();
    }
}

NssToken NssParser::advance()
{
    if (!is_end()) ++current_;
    return previous();
}

bool NssParser::check(std::initializer_list<NssTokenType> types) const
{
    for (const auto t : types) {
        if (peek().type == t) {
            return true;
        }
    }
    return false;
}

void NssParser::error(const std::string& msg) const
{
    LOG_F(FATAL, "{}, Token:{}, {}:{}", msg, peek().id, peek().line, peek().start);
}

bool NssParser::is_end() const
{
    return current_ >= tokens.size();
}

NssToken NssParser::consume(NssTokenType type, const std::string& message)
{
    if (check({type})) return advance();

    error(message);
    throw std::runtime_error(message);
}

NssToken NssParser::lookahead(size_t index) const
{
    if (index + 1 + current_ >= tokens.size()) {
        error("Out of bounds");
        throw std::runtime_error("Out of bounds");
    }
    return tokens[current_ + index + 1];
}

bool NssParser::match(std::initializer_list<NssTokenType> types)
{
    for (const auto t : types) {
        if (peek().type == t) {
            advance();
            return true;
        }
    }
    return false;
}

NssToken NssParser::peek() const
{
    return tokens[current_];
}

NssToken NssParser::previous()
{
    return tokens[current_ - 1];
}

// ---- Expressions -----------------------------------------------------------

std::unique_ptr<Expression> NssParser::parse_expr()
{
    return parse_expr_assign();
}

std::unique_ptr<Expression> NssParser::parse_expr_assign()
{
    auto expr = parse_expr_conditional();

    if (match({NssTokenType::EQ,
            NssTokenType::ANDEQ,
            NssTokenType::DIVEQ,
            NssTokenType::MINUSEQ,
            NssTokenType::MODEQ,
            NssTokenType::OREQ,
            NssTokenType::PLUSEQ,
            NssTokenType::SLEQ,
            NssTokenType::SREQ,
            NssTokenType::TIMESEQ,
            NssTokenType::USREQ,
            NssTokenType::XOREQ})) {
        auto equals = previous();
        auto value = parse_expr_assign();

        if (dynamic_cast<VariableExpression*>(expr.get())) {
            return std::make_unique<AssignExpression>(std::move(expr), equals, std::move(value));
        } else if (dynamic_cast<DotExpression*>(expr.get())) {
            return std::make_unique<AssignExpression>(std::move(expr), equals, std::move(value));
        }

        error("Invalid assignment target.");
    }

    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_conditional()
{
    auto expr = parse_expr_or();
    if (match({NssTokenType::QUESTION})) {
        auto tbranch = parse_expr();
        consume(NssTokenType::COLON, "Expected ':'.");
        auto fbranch = parse_expr_conditional();
        expr = std::make_unique<ConditionalExpression>(std::move(expr), std::move(tbranch), std::move(fbranch));
    }

    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_or()
{
    auto expr = parse_expr_and();
    while (match({NssTokenType::OROR})) {
        auto op = previous();
        auto right = parse_expr_and();
        expr = std::make_unique<LogicalExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_and()
{
    auto expr = parse_expr_bitwise();
    while (match({NssTokenType::ANDAND})) {
        auto op = previous();
        auto right = parse_expr_bitwise();
        expr = std::make_unique<LogicalExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_bitwise()
{
    auto expr = parse_expr_equality();
    while (match({NssTokenType::AND, NssTokenType::OR, NssTokenType::XOR})) {
        auto op = previous();
        auto right = parse_expr_equality();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_equality()
{
    auto expr = parse_expr_relational();
    while (match({NssTokenType::NOTEQ, NssTokenType::EQEQ})) {
        auto op = previous();
        auto right = parse_expr_relational();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_relational()
{
    auto expr = parse_expr_shift();
    while (match({NssTokenType::GT, NssTokenType::GTEQ, NssTokenType::LT, NssTokenType::LTEQ})) {
        auto op = previous();
        auto right = parse_expr_shift();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_shift()
{
    auto expr = parse_expr_additive();
    while (match({NssTokenType::SR, NssTokenType::SL, NssTokenType::USR})) {
        auto op = previous();
        auto right = parse_expr_additive();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_additive()
{
    auto expr = parse_expr_multiplicative();

    while (match({NssTokenType::MINUS, NssTokenType::PLUS})) {
        auto op = previous();
        auto right = parse_expr_multiplicative();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_multiplicative()
{
    auto expr = parse_expr_unary();

    while (match({NssTokenType::DIV, NssTokenType::TIMES})) {
        auto op = previous();
        auto right = parse_expr_unary();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_unary()
{
    if (match({NssTokenType::NOT, NssTokenType::MINUS, NssTokenType::MINUSMINUS, NssTokenType::PLUSPLUS})) {
        auto op = previous();
        auto right = parse_expr_unary();
        return std::make_unique<UnaryExpression>(op, std::move(right));
    }
    return parse_expr_postfix();
}

std::unique_ptr<Expression> NssParser::parse_expr_postfix()
{
    auto expr = parse_expr_primary();

    while (match({NssTokenType::PLUSPLUS, NssTokenType::MINUSMINUS, NssTokenType::LPAREN, NssTokenType::DOT})) {
        if (previous().type == NssTokenType::PLUSPLUS || previous().type == NssTokenType::MINUSMINUS) {
            auto op = previous();
            expr = std::make_unique<PostfixExpression>(std::move(expr), op);
        }

        if (previous().type == NssTokenType::LPAREN) {
            auto e = std::make_unique<CallExpression>(std::move(expr));
            while (!is_end() && !check({NssTokenType::RPAREN})) {
                e->args.emplace_back(parse_expr());
                match({NssTokenType::COMMA});
            }
            consume(NssTokenType::RPAREN, "Expected ')'.");
            expr = std::move(e);
        }

        if (previous().type == NssTokenType::DOT) {

            auto right = parse_expr_postfix();
            expr = std::make_unique<DotExpression>(std::move(expr), std::move(right));
        }
    }

    return expr;
}

// INT | FLOAT | STRING | "(" expression ")" | IDENTIFIER
std::unique_ptr<Expression> NssParser::parse_expr_primary()
{
    if (match({NssTokenType::STRING_CONST, NssTokenType::INTEGER_CONST, NssTokenType::FLOAT_CONST})) {
        return std::make_unique<LiteralExpression>(previous());
    }

    if (match({NssTokenType::IDENTIFIER})) {
        return std::make_unique<VariableExpression>(previous());
    }

    if (match({NssTokenType::LBRACKET})) {
        NssToken x, y, z;
        consume(NssTokenType::FLOAT_CONST, "Expected floating point literal");
        consume(NssTokenType::COMMA, "Expected comma");
        consume(NssTokenType::FLOAT_CONST, "Expected floating point literal");
        consume(NssTokenType::COMMA, "Expected floating point literal");
        consume(NssTokenType::FLOAT_CONST, "Expected comma");
        consume(NssTokenType::RBRACKET, "Expected comma");
        return std::make_unique<LiteralVectorExpression>(x, y, z);
    }

    if (match({NssTokenType::LPAREN})) {
        auto expr = parse_expr();
        consume(NssTokenType::RPAREN, "Expected ')' after expression.");
        return std::make_unique<GroupingExpression>(std::move(expr));
    }

    return nullptr;
}

// ---- Statements ------------------------------------------------------------

Type NssParser::parse_type()
{
    Type t;

    // Type qualifiers
    if (match({NssTokenType::CONST})) {
        t.type_qualifier = previous();
    }

    // Type specifiers
    if (match({NssTokenType::ACTION,
            NssTokenType::CASSOWARY,
            NssTokenType::EFFECT,
            NssTokenType::EVENT,
            NssTokenType::FLOAT,
            NssTokenType::INT,
            NssTokenType::ITEMPROPERTY,
            NssTokenType::JSON,
            NssTokenType::LOCATION,
            NssTokenType::OBJECT,
            NssTokenType::SQLQUERY,
            NssTokenType::STRING,
            NssTokenType::STRUCT,
            NssTokenType::TALENT,
            NssTokenType::VECTOR,
            NssTokenType::VOID})) {
        t.type_specifier = previous();
        if (t.type_specifier.type == NssTokenType::STRUCT) {
            consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
            t.struct_id = previous();
        }
    } else {
        error("Expected type specifier");
        throw std::runtime_error("Expected type specifier");
    }

    return t;
}

// statement -> exprStmt
//           | doStmt
//           | forStmt
//           | ifStmt
//           | jumpStmt
//           | switchStmt
//           | whileStmt
//           | block
std::unique_ptr<Statement> NssParser::parse_stmt()
{
    if (match({NssTokenType::DO})) {
        return parse_stmt_do();
    }

    if (match({NssTokenType::IF})) {
        return parse_stmt_if();
    }

    if (match({NssTokenType::FOR})) {
        return parse_stmt_for();
    }

    if (match({NssTokenType::RETURN, NssTokenType::BREAK, NssTokenType::CONTINUE})) {
        return parse_stmt_jump();
    }

    if (match({NssTokenType::DEFAULT, NssTokenType::CASE})) {
        return parse_stmt_label();
    }

    if (match({NssTokenType::SWITCH})) {
        return parse_stmt_switch();
    }

    if (match({NssTokenType::WHILE})) {
        return parse_stmt_while();
    }

    if (match({NssTokenType::LBRACE})) {
        return parse_stmt_block();
    }

    auto s = std::make_unique<ExprStatement>();
    s->expr = parse_expr();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    return s;
}

// block -> "{" declaration* "}"
std::unique_ptr<BlockStatement> NssParser::parse_stmt_block()
{
    auto s = std::make_unique<BlockStatement>();
    while (!is_end() && !check({NssTokenType::RBRACE})) {
        s->nodes.emplace_back(parse_decl());
    }
    consume(NssTokenType::RBRACE, "Expected '}'.");
    return s;
}

// doStmt -> do statement while "(" expression ")" ";"
std::unique_ptr<DoStatement> NssParser::parse_stmt_do()
{
    auto s = std::make_unique<DoStatement>();
    consume(NssTokenType::LBRACE, "Expected '{'.");
    s->block = parse_stmt_block();
    consume(NssTokenType::WHILE, "Expected 'while'.");
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->expr = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

// exprStmt -> expression ";"
std::unique_ptr<ExprStatement> NssParser::parse_stmt_expr()
{
    auto s = std::make_unique<ExprStatement>();
    s->expr = parse_expr();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

// ifStmt -> if "(" expression ")" statement ( else statement )?
std::unique_ptr<IfStatement> NssParser::parse_stmt_if()
{
    auto s = std::make_unique<IfStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->expr = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    s->if_branch = parse_stmt();
    if (match({NssTokenType::ELSE})) {

        s->else_branch = parse_stmt();
    }
    return s;
}

// forStmt -> for ( expression? ; expression? ; expression? ) statement
std::unique_ptr<ForStatement> NssParser::parse_stmt_for()
{
    auto s = std::make_unique<ForStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");

    if (!check({NssTokenType::SEMICOLON})) {
        s->init = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    if (!check({NssTokenType::SEMICOLON})) {
        s->check = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    if (!check({NssTokenType::SEMICOLON})) {
        s->inc = parse_expr();
    }
    consume(NssTokenType::RPAREN, "Expected ')'.");
    s->block = parse_stmt();
    return s;
}

// returnStmt -> return expression? ;
std::unique_ptr<JumpStatement> NssParser::parse_stmt_jump()
{
    auto s = std::make_unique<JumpStatement>();
    s->op = previous();
    if (s->op.type == NssTokenType::RETURN && !check({NssTokenType::SEMICOLON})) {
        s->expr = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

std::unique_ptr<LabelStatement> NssParser::parse_stmt_label()
{
    auto s = std::make_unique<LabelStatement>();
    s->type = previous();
    if (s->type.type == NssTokenType::CASE) {
        s->expr = parse_expr();
        if (!dynamic_cast<LiteralExpression*>(s->expr.get())) {
            error("Expected literal expression");
        }
    }
    consume(NssTokenType::COLON, "Expected ':'.");
    return s;
}

std::unique_ptr<SwitchStatement> NssParser::parse_stmt_switch()
{
    auto s = std::make_unique<SwitchStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->target = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    consume(NssTokenType::LBRACE, "Expected '{'.");
    s->block = parse_stmt_block();
    return s;
}

// whileStmt -> while "(" expression ")" statement
std::unique_ptr<WhileStatement> NssParser::parse_stmt_while()
{
    auto s = std::make_unique<WhileStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->check = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    s->block = parse_stmt();
    return s;
}

// declaration -> var_decl
//             | statement ";"
std::unique_ptr<Statement> NssParser::parse_decl()
{
    if (check({NssTokenType::CONST,
            NssTokenType::ACTION,
            NssTokenType::CASSOWARY,
            NssTokenType::EFFECT,
            NssTokenType::EVENT,
            NssTokenType::FLOAT,
            NssTokenType::INT,
            NssTokenType::ITEMPROPERTY,
            NssTokenType::JSON,
            NssTokenType::LOCATION,
            NssTokenType::OBJECT,
            NssTokenType::SQLQUERY,
            NssTokenType::STRING,
            NssTokenType::STRUCT,
            NssTokenType::TALENT,
            NssTokenType::VECTOR,
            NssTokenType::VOID})) {
        Type t = parse_type();
        auto vd = std::make_unique<VarDecl>();
        vd->type = t;
        consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        vd->identifier = previous();
        if (match({NssTokenType::EQ})) {
            vd->init = parse_expr();
        }
        consume(NssTokenType::SEMICOLON, "Expected ';'.");
        return vd;
    }
    auto s = parse_stmt();
    // consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

std::unique_ptr<Declaration> NssParser::parse_decl_struct_member()
{
    Type t = parse_type();
    auto d = std::make_unique<VarDecl>();
    d->type = t;
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    d->identifier = previous();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return d;
}

std::unique_ptr<StructDecl> NssParser::parse_decl_struct()
{
    auto decl = std::make_unique<StructDecl>();
    while (!is_end() && !check({NssTokenType::RBRACE})) {
        decl->decls.emplace_back(parse_decl_struct_member());
    }
    consume(NssTokenType::RBRACE, "Expected '}'.");
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return decl;
}

// program -> external_declaration* EOF
Script NssParser::parse_program()
{
    Script p;
    while (!is_end()) {
        if (match({NssTokenType::POUND})) {
            if (peek().id == "include") {
                advance(); // include
                advance(); // script name
            } else if (peek().id == "define") {
                advance(); // define
                advance(); // varname
                advance(); // value
            }
        } else {
            p.decls.emplace_back(parse_decl_external());
        }
    }
    return p;
}

// const? TYPE IDENTIFIER (= primary )? ;
// const? TYPE IDENTIFIER(PARAMETER*);
// const? TYPE IDENTIFIER(PARAMETER*) block
// struct IDENTIFIER { declaration+ }
std::unique_ptr<Declaration> NssParser::parse_decl_external()
{
    Type t = parse_type();

    // If after the type is parsed there is an '{' then it's a struct declaration
    if (match({NssTokenType::LBRACE})) {
        auto sd = parse_decl_struct();
        sd->type = t;
        return sd;
    }

    if (lookahead(0).type == NssTokenType::EQ || lookahead(0).type == NssTokenType::SEMICOLON) {
        auto gvd = parse_decl_global_var();
        gvd->type = t;
        return gvd;
    }

    if (lookahead(0).type == NssTokenType::LPAREN) {
        auto fd = parse_decl_function_def();
        fd->decl->type = t;
        return fd;
    }

    error("Expected function definition/declaration, struct declaration, or global variable declaration");
    throw std::runtime_error("Expected function definition/declaration, struct declaration, or variable declaration");
}

std::unique_ptr<Declaration> NssParser::parse_decl_param()
{
    auto vd = std::make_unique<VarDecl>();

    Type t = parse_type();
    vd->type = t;
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    vd->identifier = previous();
    if (match({NssTokenType::EQ})) {
        vd->init = parse_expr_unary();
    }
    return vd;
}

std::unique_ptr<FunctionDecl> NssParser::parse_decl_function()
{
    auto fd = std::make_unique<FunctionDecl>();
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    fd->identifier = previous();
    consume(NssTokenType::LPAREN, "Expected '('.");
    while (!is_end() && !check({NssTokenType::RPAREN})) {
        fd->params.emplace_back(parse_decl_param());
        match({NssTokenType::COMMA});
    }
    consume(NssTokenType::RPAREN, "Expected ')'.");
    return fd;
}

std::unique_ptr<FunctionDefinition> NssParser::parse_decl_function_def()
{
    auto def = std::make_unique<FunctionDefinition>();
    def->decl = parse_decl_function();
    if (match({NssTokenType::SEMICOLON})) {
        return def;
    }
    consume(NssTokenType::LBRACE, "Expected '{'.");
    def->block = parse_stmt_block();
    return def;
}

std::unique_ptr<VarDecl> NssParser::parse_decl_global_var()
{
    auto ret = std::make_unique<VarDecl>();
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    ret->identifier = previous();
    if (match({NssTokenType::EQ})) {
        // [TODO] Parse is going to need to error on non constant expressions
        ret->init = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    return ret;
}

} // namespace nw::script
