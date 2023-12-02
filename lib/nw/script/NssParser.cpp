#include "NssParser.hpp"

#include "../log.hpp"
#include "Nss.hpp"
#include "SourceLocation.hpp"

namespace nw::script {

NssParser::NssParser(std::string_view view, Context* ctx, Nss* parent)
    : ctx_{ctx}
    , parent_{parent}
    , view_{view}
    , lexer_{view_, ctx_, parent}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
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

bool NssParser::check_is_type() const
{
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
        return true;
    }

    return false;
}

void NssParser::diagnostic(std::string_view msg, NssToken token, bool is_warning)
{
    ctx_->parse_diagnostic(parent_, msg, is_warning, token.loc);
}

bool NssParser::is_end() const
{
    return current_ >= tokens.size();
}

NssToken NssParser::consume(NssTokenType type, std::string_view message)
{
    if (check({type})) return advance();

    diagnostic(message, peek());
    throw parser_error(message);
}

void NssParser::lex()
{
    if (lexed_) { return; }
    size_t last_comment_line = std::string::npos;
    Comment current_comment;
    try {
        NssToken tok = lexer_.next();
        while (tok.type != NssTokenType::END) {
            if (tok.type == NssTokenType::COMMENT) {
                // Append all comments that on adjacent rows
                if (last_comment_line == std::string::npos
                    || last_comment_line + 1 == tok.loc.range.end.line) {
                    current_comment.append(tok.loc.view(), tok.loc);
                    last_comment_line = tok.loc.range.end.line;
                } else if (!current_comment.comment_.empty()) {
                    ast_.comments.push_back(std::move(current_comment));
                    last_comment_line = tok.loc.range.end.line;
                    current_comment.append(tok.loc.view(), tok.loc);
                }
            } else {
                tokens.push_back(tok);
            }
            tok = lexer_.next();
        }
        if (!current_comment.comment_.empty()) {
            ast_.comments.push_back(std::move(current_comment));
        }
    } catch (const lexical_error& error) {
        ctx_->lexical_diagnostic(parent_, error.what(), false, error.location);
        tokens.clear();
    }
    lexed_ = true;
    ast_.line_map = lexer_.line_map;
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
    while (!is_end()) {
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
        case NssTokenType::RBRACE:
            return;
        }

        advance();
    }
}

// ---- Expressions -----------------------------------------------------------

Expression* NssParser::parse_expr()
{
    return parse_expr_assign();
}

Expression* NssParser::parse_expr_assign()
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

        if (dynamic_cast<VariableExpression*>(expr)) {
            return ast_.create_node<AssignExpression>(expr, equals, value);
        } else if (dynamic_cast<DotExpression*>(expr)) {
            return ast_.create_node<AssignExpression>(expr, equals, value);
        }

        diagnostic("Invalid assignment target.", peek());
    }

    return expr;
}

Expression* NssParser::parse_expr_conditional()
{
    auto expr = parse_expr_or();
    if (match({NssTokenType::QUESTION})) {
        auto tbranch = parse_expr();
        consume(NssTokenType::COLON, "Expected ':'.");
        auto fbranch = parse_expr_conditional();
        expr = ast_.create_node<ConditionalExpression>(expr, tbranch, fbranch);
    }

    return expr;
}

Expression* NssParser::parse_expr_or()
{
    auto expr = parse_expr_and();
    while (match({NssTokenType::OROR})) {
        auto op = previous();
        auto right = parse_expr_and();
        expr = ast_.create_node<LogicalExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_and()
{
    auto expr = parse_expr_bitwise();
    while (match({NssTokenType::ANDAND})) {
        auto op = previous();
        auto right = parse_expr_bitwise();
        expr = ast_.create_node<LogicalExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_bitwise()
{
    auto expr = parse_expr_equality();
    while (match({NssTokenType::AND, NssTokenType::OR, NssTokenType::XOR})) {
        auto op = previous();
        auto right = parse_expr_equality();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_equality()
{
    auto expr = parse_expr_relational();
    while (match({NssTokenType::NOTEQ, NssTokenType::EQEQ})) {
        auto op = previous();
        auto right = parse_expr_relational();
        expr = ast_.create_node<ComparisonExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_relational()
{
    auto expr = parse_expr_shift();
    while (match({NssTokenType::GT, NssTokenType::GTEQ, NssTokenType::LT, NssTokenType::LTEQ})) {
        auto op = previous();
        auto right = parse_expr_shift();
        expr = ast_.create_node<ComparisonExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_shift()
{
    auto expr = parse_expr_additive();
    while (match({NssTokenType::SR, NssTokenType::SL, NssTokenType::USR})) {
        auto op = previous();
        auto right = parse_expr_additive();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }
    return expr;
}

Expression* NssParser::parse_expr_additive()
{
    auto expr = parse_expr_multiplicative();

    while (match({NssTokenType::MINUS, NssTokenType::PLUS})) {
        auto op = previous();
        auto right = parse_expr_multiplicative();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }

    return expr;
}

Expression* NssParser::parse_expr_multiplicative()
{
    auto expr = parse_expr_unary();

    while (match({NssTokenType::DIV, NssTokenType::TIMES, NssTokenType::MOD})) {
        auto op = previous();
        auto right = parse_expr_unary();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }

    return expr;
}

Expression* NssParser::parse_expr_unary()
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

        // Constant fold unary expressions cause this will cause problems later
        // to avoid the AST having (unary - (literal 1)) instead of (literal -1) etc
        if (auto lit = dynamic_cast<LiteralExpression*>(right)) {
            if (op.type == NssTokenType::PLUS
                && (lit->literal.type == NssTokenType::INTEGER_CONST
                    || lit->literal.type == NssTokenType::FLOAT_CONST)) {
                // If plus and a literal throw away the plus if it's an int or a float
                lit->literal.loc = merge_source_location(op.loc, lit->literal.loc);
                return right;
            } else if (op.type == NssTokenType::MINUS) {
                if (lit->literal.type == NssTokenType::INTEGER_CONST) {
                    lit->data = -lit->data.as<int32_t>();
                    lit->literal.loc = merge_source_location(op.loc, lit->literal.loc);
                    return right;
                } else if (lit->literal.type == NssTokenType::FLOAT_CONST) {
                    lit->data = -lit->data.as<float>();
                    lit->literal.loc = merge_source_location(op.loc, lit->literal.loc);
                    return right;
                }
            } else if (op.type == NssTokenType::TILDE) {
                if (lit->literal.type == NssTokenType::INTEGER_CONST) {
                    lit->data = ~lit->data.as<int32_t>();
                    lit->literal.loc = merge_source_location(op.loc, lit->literal.loc);
                    return right;
                }
            } else if (op.type == NssTokenType::NOT) {
                if (lit->literal.type == NssTokenType::INTEGER_CONST) {
                    lit->data = !lit->data.as<int32_t>();
                    lit->literal.loc = merge_source_location(op.loc, lit->literal.loc);
                    return right;
                }
            }
            // Leave everything else for errors later
        }
        return ast_.create_node<UnaryExpression>(op, right);
    }
    return parse_expr_postfix();
}

Expression* NssParser::parse_expr_postfix()
{
    auto expr = parse_expr_primary();

    while (match({NssTokenType::PLUSPLUS, NssTokenType::MINUSMINUS, NssTokenType::LPAREN, NssTokenType::DOT})) {
        if (previous().type == NssTokenType::PLUSPLUS || previous().type == NssTokenType::MINUSMINUS) {
            auto op = previous();
            expr = ast_.create_node<PostfixExpression>(expr, op);
        }

        if (previous().type == NssTokenType::LPAREN) {
            if (!dynamic_cast<VariableExpression*>(expr)) {
                diagnostic("expression cannot be used as a function", previous());
            }

            auto e = ast_.create_node<CallExpression>(expr);
            while (!is_end() && !check({NssTokenType::RPAREN})) {
                e->args.emplace_back(parse_expr());
                if (match({NssTokenType::COMMA}) && check({NssTokenType::RPAREN})) {
                    diagnostic("spurious ','", previous());
                }
            }
            consume(NssTokenType::RPAREN, "Expected ')'.");
            expr = e;
        }

        if (previous().type == NssTokenType::DOT) {
            auto dot = previous();
            auto right = parse_expr_primary();
            expr = ast_.create_node<DotExpression>(expr, dot, right);
        }
    }

    return expr;
}

// INT | FLOAT | STRING | "(" expression ")" | IDENTIFIER
Expression* NssParser::parse_expr_primary()
{
    if (match({NssTokenType::STRING_CONST, NssTokenType::INTEGER_CONST, NssTokenType::FLOAT_CONST,
            NssTokenType::OBJECT_INVALID_CONST, NssTokenType::OBJECT_SELF_CONST})) {
        auto expr = ast_.create_node<LiteralExpression>(previous());

        if (expr->literal.type == NssTokenType::STRING_CONST) {
            // Probably need to process the string..
            expr->data = std::string(expr->literal.loc.view());
        } else if (expr->literal.type == NssTokenType::INTEGER_CONST) {
            if (auto val = string::from<int32_t>(expr->literal.loc.view())) {
                expr->data = *val;
            } else {
                ctx_->parse_diagnostic(parent_,
                    fmt::format("unable to parse integer literal '{}'", expr->literal.loc.view()),
                    false,
                    expr->literal.loc);
            }
        } else if (expr->literal.type == NssTokenType::FLOAT_CONST) {
            if (auto val = string::from<float>(expr->literal.loc.view())) {
                expr->data = *val;
            } else {
                ctx_->parse_diagnostic(parent_,
                    fmt::format("unable to parse float literal '{}'", expr->literal.loc.view()),
                    false,
                    expr->literal.loc);
            }
        }
        return expr;
    }

    if (match({NssTokenType::IDENTIFIER})) {
        return ast_.create_node<VariableExpression>(previous());
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

        auto expr = ast_.create_node<LiteralVectorExpression>(x, y, z);

        if (auto val = string::from<float>(expr->x.loc.view())) {
            expr->data.x = *val;
        } else {
            ctx_->parse_diagnostic(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->x.loc.view()),
                false,
                expr->x.loc);
        }

        if (auto val = string::from<float>(expr->y.loc.view())) {
            expr->data.y = *val;
        } else {
            ctx_->parse_diagnostic(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->y.loc.view()),
                false,
                expr->y.loc);
        }

        if (auto val = string::from<float>(expr->z.loc.view())) {
            expr->data.z = *val;
        } else {
            ctx_->parse_diagnostic(parent_,
                fmt::format("unable to parse vector literal '{}'", expr->z.loc.view()),
                false,
                expr->z.loc);
        }

        return expr;
    }

    if (match({NssTokenType::LPAREN})) {
        auto expr = parse_expr();
        consume(NssTokenType::RPAREN, "Expected ')' after expression.");
        return ast_.create_node<GroupingExpression>(expr);
    }

    diagnostic("expected primary expression", peek());
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
        diagnostic("Expected type specifier", peek());
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
Statement* NssParser::parse_stmt()
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
        return ast_.create_node<EmptyStatement>();
    }

    auto s = ast_.create_node<ExprStatement>();
    s->expr = parse_expr();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    return s;
}

// block -> "{" declaration* "}"
BlockStatement* NssParser::parse_stmt_block()
{
    auto s = ast_.create_node<BlockStatement>();
    s->range.start = previous().loc.range.end; // Don't include the braces in the range.. for now.
    while (!is_end() && !check({NssTokenType::RBRACE})) {
        try {
            auto n = parse_decl();
            if (n) {
                s->nodes.emplace_back(n);
            }
        } catch (const parser_error&) {
            synchronize();
        }
    }
    consume(NssTokenType::RBRACE, "Expected '}'.");
    s->range.end = previous().loc.range.start;
    return s;
}

// doStmt -> do statement while "(" expression ")" ";"
DoStatement* NssParser::parse_stmt_do()
{
    auto s = ast_.create_node<DoStatement>();
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
ExprStatement* NssParser::parse_stmt_expr()
{
    auto s = ast_.create_node<ExprStatement>();
    s->expr = parse_expr();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

// ifStmt -> if "(" expression ")" statement ( else statement )?
IfStatement* NssParser::parse_stmt_if()
{
    auto s = ast_.create_node<IfStatement>();
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
ForStatement* NssParser::parse_stmt_for()
{
    auto s = ast_.create_node<ForStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    bool semi_taken = false;

    if (!check({NssTokenType::SEMICOLON})) {
        if (check_is_type()) {
            s->init = parse_decl();
            semi_taken = true;
        } else {
            s->init = parse_expr();
        }
    }

    if (!semi_taken) {
        consume(NssTokenType::SEMICOLON, "Expected ';'.");
    }

    if (!check({NssTokenType::SEMICOLON})) {
        s->check = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");

    if (!check({NssTokenType::RPAREN})) {
        s->inc = parse_expr();
    }
    consume(NssTokenType::RPAREN, "Expected ')'.");
    s->block = parse_stmt();
    return s;
}

// returnStmt -> return expression? ;
JumpStatement* NssParser::parse_stmt_jump()
{
    auto s = ast_.create_node<JumpStatement>();
    s->op = previous();
    if (s->op.type == NssTokenType::RETURN && !check({NssTokenType::SEMICOLON})) {
        s->expr = parse_expr();
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

LabelStatement* NssParser::parse_stmt_label()
{
    auto s = ast_.create_node<LabelStatement>();
    s->type = previous();
    if (s->type.type == NssTokenType::CASE) {
        // [TODO] check here or in resolver for incorrect types.
        s->expr = parse_expr();
    }
    consume(NssTokenType::COLON, "Expected ':'.");
    return s;
}

SwitchStatement* NssParser::parse_stmt_switch()
{
    auto s = ast_.create_node<SwitchStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->target = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    consume(NssTokenType::LBRACE, "Expected '{'.");
    s->block = parse_stmt_block();
    return s;
}

// whileStmt -> while "(" expression ")" statement
WhileStatement* NssParser::parse_stmt_while()
{
    auto s = ast_.create_node<WhileStatement>();
    consume(NssTokenType::LPAREN, "Expected '('.");
    s->check = parse_expr();
    consume(NssTokenType::RPAREN, "Expected ')'.");
    s->block = parse_stmt();
    return s;
}

// declaration -> var_decl
//             | statement ";"
Statement* NssParser::parse_decl()
{
    if (check_is_type()) {
        auto decls = ast_.create_node<DeclList>();
        Type t = parse_type();

        while (true) {
            auto vd = ast_.create_node<VarDecl>();
            vd->type = t;
            consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
            vd->identifier = previous();
            if (match({NssTokenType::EQ})) {
                vd->init = parse_expr();
            }

            if (t.type_qualifier.type == NssTokenType::INVALID) {
                vd->range_.start = t.type_specifier.loc.range.start;
            } else {
                vd->range_.start = t.type_qualifier.loc.range.start;
            }
            vd->range_selection_.start = vd->identifier.loc.range.start;
            vd->range_selection_.end = vd->identifier.loc.range.end;

            decls->decls.push_back(vd);
            if (!match({NssTokenType::COMMA})) { break; }
        }
        consume(NssTokenType::SEMICOLON, "Expected ';'.");
        for (auto decl : decls->decls) {
            decl->range_.end = previous().loc.range.start;
        }

        if (decls->decls.size() == 1) {
            return decls->decls[0];
        } else {
            return decls;
        }
    }
    auto s = parse_stmt();
    // consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return s;
}

VarDecl* NssParser::parse_decl_struct_member()
{
    Type t = parse_type();
    auto d = ast_.create_node<VarDecl>();
    d->type = t;
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    d->identifier = previous();
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    return d;
}

StructDecl* NssParser::parse_decl_struct()
{
    auto decl = ast_.create_node<StructDecl>();
    while (!is_end() && !check({NssTokenType::RBRACE})) {
        decl->decls.emplace_back(parse_decl_struct_member());
    }
    consume(NssTokenType::RBRACE, "Expected '}'.");
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    decl->range_.end = previous().loc.range.start;

    return decl;
}

// program -> external_declaration* EOF
Ast NssParser::parse_program()
{
    lex();

    while (!is_end()) {
        try {
            if (match({NssTokenType::POUND})) { // Only at top level
                if (peek().loc.view() == "include") {
                    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'."); // include
                    if (match({NssTokenType::STRING_CONST})) {
                        if (previous().loc.view().size() <= nw::Resref::max_size) {
                            ast_.includes.push_back({
                                std::string(previous().loc.view()),
                                previous().loc,
                            });
                        } else {
                            diagnostic(fmt::format("invalid include resref '{}'", previous().loc.view()), previous());
                        }
                    } else {
                        diagnostic("Expected string literal", peek());
                        throw parser_error("Expected string literal");
                    }
                } else if (peek().loc.view() == "define") {
                    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'."); // define
                    std::string name, value;
                    if (match({NssTokenType::IDENTIFIER})) {
                        name = std::string(previous().loc.view());
                    } else {
                        diagnostic("Expected identifier", peek());
                        throw parser_error("Expected identifier");
                    }
                    value = std::string(advance().loc.view());
                    ast_.defines.insert({name, value});
                }
            } else if (match({NssTokenType::COMMENT})) {
                continue;
            } else {
                ast_.decls.emplace_back(parse_decl_external());
            }
        } catch (const parser_error& err) {
            synchronize();
        }
    }
    return std::move(ast_);
}

// const? TYPE IDENTIFIER (= primary )? ;
// const? TYPE IDENTIFIER(PARAMETER*);
// const? TYPE IDENTIFIER(PARAMETER*) block
// struct IDENTIFIER { declaration+ }
Statement* NssParser::parse_decl_external()
{
    if (match({NssTokenType::SEMICOLON})) {
        diagnostic("spurious ';'", previous(), true);
        return ast_.create_node<EmptyStatement>();
    }

    Type t = parse_type();

    // If after the type is parsed there is an '{' then it's a struct declaration
    if (match({NssTokenType::LBRACE})) {
        try {
            auto sd = parse_decl_struct();
            sd->type = t;

            // Note range end is already determined
            sd->range_.start = sd->type.type_specifier.loc.range.start;
            sd->range_selection_.start = sd->type.struct_id.loc.range.start;
            sd->range_selection_.end = sd->type.struct_id.loc.range.end;

            return sd;
        } catch (const parser_error&) {
            int lbrace = 1;
            // Gonna have to be pretty aggressive about recovering from this
            // if this doesn't work probably best to bail on the script
            while (!is_end()) {
                if (peek().type == NssTokenType::LBRACE) {
                    ++lbrace;
                } else if (peek().type == NssTokenType::RBRACE) {
                    --lbrace;
                }
                advance();
                if (lbrace <= 0) { break; }
            }
            throw;
        }
    }

    if (lookahead(0).type == NssTokenType::EQ
        || lookahead(0).type == NssTokenType::SEMICOLON
        || lookahead(0).type == NssTokenType::COMMA) {
        auto gvd = parse_decl_global_var();
        if (auto ds = dynamic_cast<DeclList*>(gvd)) {
            for (auto& d : ds->decls) {
                d->type = t;
                if (t.type_qualifier.type == NssTokenType::INVALID) {
                    d->range_.start = t.type_specifier.loc.range.start;
                } else {
                    d->range_.start = t.type_qualifier.loc.range.start;
                }
                d->range_selection_.start = d->identifier.loc.range.start;
                d->range_selection_.end = d->identifier.loc.range.end;
            }
        } else {
            auto vd = static_cast<VarDecl*>(gvd);
            vd->type = t;
            // Note range end is already determined
            if (t.type_qualifier.type == NssTokenType::INVALID) {
                vd->range_.start = t.type_specifier.loc.range.start;
            } else {
                vd->range_.start = t.type_qualifier.loc.range.start;
            }
            vd->range_selection_.start = vd->identifier.loc.range.start;
            vd->range_selection_.end = vd->identifier.loc.range.end;
        }
        return gvd;
    }

    if (lookahead(0).type == NssTokenType::LPAREN) {
        auto fd = parse_decl_function_def();
        if (auto decl = dynamic_cast<FunctionDecl*>(fd)) {
            decl->type = t;
            decl->range_selection_.start = decl->identifier.loc.range.start;
            decl->range_selection_.end = decl->identifier.loc.range.end;
        } else if (auto def = dynamic_cast<FunctionDefinition*>(fd)) {
            def->decl_inline->type = t;
            def->range_selection_.start = def->decl_inline->identifier.loc.range.start;
            def->range_selection_.end = def->decl_inline->identifier.loc.range.end;
            if (t.type_qualifier.type == NssTokenType::INVALID) {
                def->decl_inline->range_.start = t.type_specifier.loc.range.start;
            } else {
                def->decl_inline->range_.start = t.type_specifier.loc.range.start;
            }
        }

        if (t.type_qualifier.type == NssTokenType::INVALID) {
            fd->range_.start = t.type_specifier.loc.range.start;
        } else {
            fd->range_.start = t.type_qualifier.loc.range.start;
        }

        return fd;
    }

    diagnostic("Expected function definition/declaration, struct declaration, or global variable declaration", peek());
    throw parser_error("Expected function definition/declaration, struct declaration, or variable declaration");
}

VarDecl* NssParser::parse_decl_param()
{
    auto vd = ast_.create_node<VarDecl>();

    Type t = parse_type();
    vd->type = t;
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    vd->identifier = previous();
    if (match({NssTokenType::EQ})) {
        vd->init = parse_expr_unary();
    }
    return vd;
}

FunctionDecl* NssParser::parse_decl_function()
{
    auto fd = ast_.create_node<FunctionDecl>();
    consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    fd->identifier = previous();
    consume(NssTokenType::LPAREN, "Expected '('.");
    while (!is_end() && !check({NssTokenType::RPAREN})) {
        fd->params.emplace_back(parse_decl_param());
        match({NssTokenType::COMMA});
    }
    consume(NssTokenType::RPAREN, "Expected ')'.");
    fd->range_.end = previous().loc.range.end;
    return fd;
}

Declaration* NssParser::parse_decl_function_def()
{
    auto decl = parse_decl_function();
    if (match({NssTokenType::SEMICOLON})) {
        return decl;
    }
    auto def = ast_.create_node<FunctionDefinition>();
    def->decl_inline = decl;
    consume(NssTokenType::LBRACE, "Expected '{'.");
    def->block = parse_stmt_block();
    def->range_.end = previous().loc.range.end;
    return def;
}

Statement* NssParser::parse_decl_global_var()
{
    auto decls = ast_.create_node<DeclList>();
    while (true) {
        auto ret = ast_.create_node<VarDecl>();
        consume(NssTokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        ret->identifier = previous();
        if (match({NssTokenType::EQ})) {
            // [TODO] Parse is going to need to error on non constant expressions
            ret->init = parse_expr();
        }
        decls->decls.push_back(ret);
        if (!match({NssTokenType::COMMA})) {
            break;
        } else {
            decls->decls.back()->range_.end = previous().loc.range.end;
        }
    }
    consume(NssTokenType::SEMICOLON, "Expected ';'.");
    decls->decls.back()->range_.end = previous().loc.range.start;

    if (decls->decls.size() == 1) {
        return decls->decls[0];
    } else {
        return decls;
    }
}

} // namespace nw::script
