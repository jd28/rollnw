#include "NssParser.hpp"

#include "../log.hpp"

namespace nw::script {

NssParser::NssParser(std::string_view view, std::shared_ptr<Context> ctx, Nss* parent)
    : ctx_{ctx}
    , parent_{parent}
    , view_{view}
    , lexer_{view_, ctx_}
{
    NssToken tok = lexer_.next();
    while (tok.type != NssTokenType::END) {
        tokens.push_back(tok);
        tok = lexer_.next();
    }
}

NssToken NssParser::advance()
{
    if (!is_end()) { ++current_; }
    while (!is_end()) {
        if (tokens[current_].type == NssTokenType::COMMENT) {
            ++current_;
        } else {
            break;
        }
    }
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

void NssParser::error(std::string_view msg, NssToken token)
{
    ++errors_;
    ctx_->parse_error(parent_, msg, token);
}

void NssParser::warn(std::string_view msg, NssToken token)
{
    ++warnings_;
    ctx_->parse_warning(parent_, msg, token);
}

bool NssParser::is_end() const
{
    return current_ >= tokens.size();
}

NssToken NssParser::consume(NssTokenType type, std::string_view message)
{
    if (check({type})) return advance();

    error(message, peek());
    throw parser_error(message);
}

NssToken NssParser::lookahead(size_t index) const
{
    if (index + 1 + current_ >= tokens.size()) {
        throw parser_error("Out of bounds");
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
    if (current_ >= tokens.size()) {
        LOG_F(ERROR, "token out of bounds");
        return {};
    }
    return tokens[current_];
}

NssToken NssParser::previous()
{
    if (current_ == 0 || current_ - 1 >= tokens.size()) {
        LOG_F(ERROR, "token out of bounds");
        return {};
    }
    return tokens[current_ - 1];
}

void NssParser::synchronize()
{
    advance();

    while (!is_end()) {
        if (previous().type == NssTokenType::SEMICOLON) return;

        switch (peek().type) {
        default:
            break;
        case NssTokenType::CONST_:
        case NssTokenType::ACTION:
        case NssTokenType::CASSOWARY:
        case NssTokenType::EFFECT:
        case NssTokenType::EVENT:
        case NssTokenType::FLOAT:
        case NssTokenType::INT:
        case NssTokenType::ITEMPROPERTY:
        case NssTokenType::JSON:
        case NssTokenType::LOCATION:
        case NssTokenType::OBJECT:
        case NssTokenType::SQLQUERY:
        case NssTokenType::STRING:
        case NssTokenType::STRUCT:
        case NssTokenType::TALENT:
        case NssTokenType::VECTOR:
        case NssTokenType::VOID_:
        case NssTokenType::FOR:
        case NssTokenType::IF:
        case NssTokenType::WHILE:
        case NssTokenType::RETURN:
            return;
        }

        advance();
    }
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

        error("Invalid assignment target.", peek());
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

    while (match({NssTokenType::DIV, NssTokenType::TIMES, NssTokenType::MOD})) {
        auto op = previous();
        auto right = parse_expr_unary();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expression> NssParser::parse_expr_unary()
{
    // In the lexing stage -- and ++ will be prioritized over multiple
    // unary - and +.
    if (match({NssTokenType::NOT,
            NssTokenType::MINUS,
            NssTokenType::MINUSMINUS,
            NssTokenType::PLUS,
            NssTokenType::PLUSPLUS,
            NssTokenType::TILDE})) {
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
            if (!dynamic_cast<VariableExpression*>(expr.get())) {
                error("expression cannot be used as a function", previous());
            }

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
            if (!dynamic_cast<VariableExpression*>(right.get())
                && !dynamic_cast<DotExpression*>(right.get())) {
                error("expression cannot be used as a struct index", previous());
            }
            expr = std::make_unique<DotExpression>(std::move(expr), std::move(right));
        }
    }

    return expr;
}

// INT | FLOAT | STRING | "(" expression ")" | IDENTIFIER
std::unique_ptr<Expression> NssParser::parse_expr_primary()
{
    if (match({NssTokenType::STRING_CONST, NssTokenType::INTEGER_CONST, NssTokenType::FLOAT_CONST})) {
        auto expr = std::make_unique<LiteralExpression>(previous());

        if (expr->literal.type == NssTokenType::STRING_CONST) {
            // Probably need to process the string..
            expr->data = std::string(expr->literal.loc.view);
        } else if (expr->literal.type == NssTokenType::INTEGER_CONST) {
            if (auto val = string::from<int32_t>(expr->literal.loc.view)) {
                expr->data = *val;
            } else {
                ctx_->parse_error(parent_,
                    fmt::format("unable to parse integer literal '{}'", expr->literal.loc.view),
                    expr->literal);
            }
        } else if (expr->literal.type == NssTokenType::FLOAT_CONST) {
            if (auto val = string::from<float>(expr->literal.loc.view)) {
                expr->data = *val;
            } else {
                ctx_->parse_error(parent_,
                    fmt::format("unable to parse float literal '{}'", expr->literal.loc.view),
                    expr->literal);
            }
        }
        return expr;
    }

    if (match({NssTokenType::IDENTIFIER})) {
        return std::make_unique<VariableExpression>(previous());
    }

    if (match({NssTokenType::LBRACKET})) {
        NssToken x, y, z;
        consume(NssTokenType::FLOAT_CONST, "Expected floating point literal");
        x = previous();
        consume(NssTokenType::COMMA, "Expected ','");
        consume(NssTokenType::FLOAT_CONST, "Expected floating point literal");
        y = previous();
        consume(NssTokenType::COMMA, "Expected ','");
        consume(NssTokenType::FLOAT_CONST, "Expected floating point literal");
        z = previous();
        consume(NssTokenType::RBRACKET, "Expected ']'");

        auto expr = std::make_unique<LiteralVectorExpression>(x, y, z);

        if (auto val = string::from<float>(expr->x.loc.view)) {
            expr->data.x = *val;
        } else {
            ctx_->parse_error(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->x.loc.view),
                expr->x);
        }

        if (auto val = string::from<float>(expr->y.loc.view)) {
            expr->data.y = *val;
        } else {
            ctx_->parse_error(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->y.loc.view),
                expr->y);
        }

        if (auto val = string::from<float>(expr->z.loc.view)) {
            expr->data.z = *val;
        } else {
            ctx_->parse_error(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->z.loc.view),
                expr->z);
        }

        return expr;
    }

    if (match({NssTokenType::LPAREN})) {
        auto expr = parse_expr();
        consume(NssTokenType::RPAREN, "Expected ')' after expression.");
        return std::make_unique<GroupingExpression>(std::move(expr));
    }

    error("expected primary expression", peek());
    throw parser_error("expected primary expression");
}

// ---- Statements ------------------------------------------------------------

Type NssParser::parse_type()
{
    Type t;

    // Type qualifiers
    if (match({NssTokenType::CONST_})) {
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
            NssTokenType::VOID_})) {
        t.type_specifier = previous();
        if (t.type_specifier.type == NssTokenType::STRUCT) {
            consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
            t.struct_id = previous();
        }
    } else {
        error("Expected type specifier", peek());
        throw parser_error("Expected type specifier");
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

    if (match({NssTokenType::SEMICOLON})) {
        return std::make_unique<EmptyStatement>();
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
        if (match({NssTokenType::INTEGER_CONST, NssTokenType::STRING_CONST})) {
            auto lit = new LiteralExpression(previous());
            s->expr = std::unique_ptr<Expression>(lit);
        } else {
            error("Expected constant literal integer or string expression", peek());
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
    try {
        if (check({NssTokenType::CONST_,
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
                NssTokenType::VOID_})) {
            auto decls = std::make_unique<DeclStatement>();
            Type t = parse_type();

            while (true) {
                auto vd = std::make_unique<VarDecl>();
                vd->type = t;

                consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
                vd->identifier = previous();
                if (match({NssTokenType::EQ})) {
                    vd->init = parse_expr();
                }
                decls->decls.push_back(std::move(vd));
                if (!match({NssTokenType::COMMA})) { break; }
            }
            consume(NssTokenType::SEMICOLON, "Expected ';'.");

            if (decls->decls.size() == 1) {
                return std::move(decls->decls[0]);
            } else {
                return decls;
            }
        }
        auto s = parse_stmt();
        // consume(NssTokenType::SEMICOLON, "Expected ';'.");
        return s;
    } catch (const parser_error& err) {
        synchronize();
        return {};
    }
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
Ast NssParser::parse_program()
{
    Ast p;
    while (!is_end()) {
        if (match({NssTokenType::POUND})) { // Only at top level
            if (peek().loc.view == "include") {
                consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'."); // include
                if (match({NssTokenType::STRING_CONST})) {
                    p.include_resrefs.push_back(std::string(previous().loc.view));
                } else {
                    error("Expected string literal", peek());
                    throw parser_error("Expected string literal");
                }
            } else if (peek().loc.view == "define") {
                consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'."); // define
                std::string name, value;
                if (match({NssTokenType::IDENTIFIER})) {
                    name = std::string(previous().loc.view);
                } else {
                    error("Expected identifier", peek());
                    throw parser_error("Expected identifier");
                }
                value = std::string(advance().loc.view);
                p.defines.push_back({name, value});
            }
        } else if (match({NssTokenType::COMMENT})) {
            continue;
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
std::unique_ptr<Statement> NssParser::parse_decl_external()
{
    if (match({NssTokenType::SEMICOLON})) {
        warn("spurious ';'", previous());
        return std::make_unique<EmptyStatement>();
    }

    Type t = parse_type();

    // If after the type is parsed there is an '{' then it's a struct declaration
    if (match({NssTokenType::LBRACE})) {
        auto sd = parse_decl_struct();
        sd->type = t;
        return sd;
    }

    if (lookahead(0).type == NssTokenType::EQ
        || lookahead(0).type == NssTokenType::SEMICOLON
        || lookahead(0).type == NssTokenType::COMMA) {
        auto gvd = parse_decl_global_var();
        if (auto ds = dynamic_cast<DeclStatement*>(gvd.get())) {
            for (auto& d : ds->decls) {
                d->type = t;
            }
        } else {
            auto vd = static_cast<VarDecl*>(gvd.get());
            vd->type = t;
        }
        return gvd;
    }

    if (lookahead(0).type == NssTokenType::LPAREN) {
        auto fd = parse_decl_function_def();
        if (auto decl = dynamic_cast<FunctionDecl*>(fd.get())) {
            decl->type = t;
        } else if (auto def = dynamic_cast<FunctionDefinition*>(fd.get())) {
            def->decl->type = t;
        }
        return fd;
    }

    error("Expected function definition/declaration, struct declaration, or global variable declaration", peek());
    throw parser_error("Expected function definition/declaration, struct declaration, or variable declaration");
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

std::unique_ptr<Declaration> NssParser::parse_decl_function_def()
{
    auto decl = parse_decl_function();
    if (match({NssTokenType::SEMICOLON})) {
        return decl;
    }
    auto def = std::make_unique<FunctionDefinition>();
    def->decl = std::move(decl);
    consume(NssTokenType::LBRACE, "Expected '{'.");
    def->block = parse_stmt_block();
    return def;
}

std::unique_ptr<Statement> NssParser::parse_decl_global_var()
{
    auto decls = std::make_unique<DeclStatement>();
    while (true) {
        auto ret = std::make_unique<VarDecl>();
        consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        ret->identifier = previous();
        if (match({NssTokenType::EQ})) {
            // [TODO] Parse is going to need to error on non constant expressions
            ret->init = parse_expr();
        }
        decls->decls.push_back(std::move(ret));
        if (!match({NssTokenType::COMMA})) { break; }
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    if (decls->decls.size() == 1) {
        return std::move(decls->decls[0]);
    } else {
        return decls;
    }
}

} // namespace nw::script
