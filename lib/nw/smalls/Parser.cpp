#include "Parser.hpp"

#include "../log.hpp"
#include "../util/string.hpp"
#include "Smalls.hpp"
#include "SourceLocation.hpp"

namespace nw::smalls {

namespace {

String process_escape_sequences(StringView input)
{
    String result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            char next = input[i + 1];
            switch (next) {
            case 'n':
                result.push_back('\n');
                ++i;
                break;
            case 't':
                result.push_back('\t');
                ++i;
                break;
            case 'r':
                result.push_back('\r');
                ++i;
                break;
            case '\\':
                result.push_back('\\');
                ++i;
                break;
            case '"':
                result.push_back('"');
                ++i;
                break;
            case '0':
                result.push_back('\0');
                ++i;
                break;
            default:
                result.push_back(input[i]);
                break;
            }
        } else {
            result.push_back(input[i]);
        }
    }

    return result;
}

struct ParseDepthGuard {
    Parser& parser;

    explicit ParseDepthGuard(Parser& p)
        : parser(p)
    {
        uint32_t limit = parser.ctx_ ? parser.ctx_->limits.max_parse_depth : 0;
        if (limit != 0 && parser.depth_ + 1 > limit) {
            if (!parser.tokens.empty()) {
                parser.diagnostic("Parser nesting limit exceeded", parser.peek());
            }
            throw parser_error("Parser nesting limit exceeded");
        }
        ++parser.depth_;
    }

    ~ParseDepthGuard()
    {
        if (parser.depth_ != 0) {
            --parser.depth_;
        }
    }
};

struct TokenPositionGuard {
    Parser& parser;
    size_t saved;
    bool committed = false;

    explicit TokenPositionGuard(Parser& p)
        : parser(p)
        , saved(p.current_)
    {
    }

    ~TokenPositionGuard()
    {
        if (!committed) {
            parser.current_ = saved;
        }
    }

    void commit() { committed = true; }
};

} // namespace

struct FStringContext : public Context {
    Context* parent_ctx;
    SourcePosition offset;

    FStringContext(Context* parent, SourcePosition offset_)
        : parent_ctx(parent)
        , offset(offset_)
    {
        arena = parent->arena;
        scope = parent->scope;
        config = parent->config;
        limits = parent->limits;
    }

    SourceRange adjust_range(SourceRange range) const
    {
        SourceRange adjusted = range;
        adjusted.start.line += offset.line;
        adjusted.start.column += offset.column;
        adjusted.end.line += offset.line;
        adjusted.end.column += offset.column;
        return adjusted;
    }

    void parse_diagnostic(Script* script, StringView msg, bool warning, SourceRange range) override
    {
        parent_ctx->parse_diagnostic(script, msg, warning, adjust_range(range));
    }

    void semantic_diagnostic(Script* script, StringView msg, bool warning, SourceRange range) override
    {
        parent_ctx->semantic_diagnostic(script, msg, warning, adjust_range(range));
    }
};

Parser::Parser(StringView view, Context* ctx, Script* parent)
    : ctx_{ctx}
    , parent_{parent}
    , view_{view}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Token Parser::advance()
{
    if (!is_end()) { ++current_; }
    while (!is_end()) {
        if (tokens[current_].type == TokenType::COMMENT) {
            ++current_;
        } else {
            break;
        }
    }
    return previous();
}

bool Parser::check(std::initializer_list<TokenType> types) const
{
    for (const auto t : types) {
        if (peek().type == t) {
            return true;
        }
    }
    return false;
}

bool Parser::check_is_decl() const
{
    return check({TokenType::CONST_, TokenType::VAR, TokenType::FN, TokenType::TYPE});
}

void Parser::diagnostic(StringView msg, Token token, bool is_warning)
{
    ctx_->parse_diagnostic(parent_, msg, is_warning, token.loc.range);
    if (!is_warning) {
        ++error_count_;
        if (!error_limit_reported_ && error_count_ >= error_limit_) {
            error_limit_reported_ = true;
            throw ast_limit_error("too many parse errors, aborting parse");
        }
    }
}

bool Parser::is_end() const
{
    return current_ >= tokens.size() || tokens[current_].type == TokenType::END;
}

Token Parser::consume(TokenType type, StringView message)
{
    if (check({type})) return advance();

    diagnostic(message, peek());
    throw parser_error(message);
}

void Parser::lex()
{
    size_t last_comment_line = String::npos;
    Comment current_comment;
    try {
        Lexer lexer{view_, ctx_, parent_};
        Token tok = lexer.next();
        while (tok.type != TokenType::END) {
            if (tok.type == TokenType::COMMENT) {
                // Append all comments that on adjacent rows
                if (last_comment_line == String::npos
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
            tok = lexer.next();
        }
        tokens.push_back(tok);
        if (!current_comment.comment_.empty()) {
            ast_.comments.push_back(std::move(current_comment));
        }
        ast_.line_map = lexer.line_map;
    } catch (const lexical_error& error) {
        ctx_->lexical_diagnostic(parent_, error.what(), false, error.location.range);
        tokens.clear();
    }
}

Token Parser::lookahead(size_t index) const
{
    if (index + 1 + current_ >= tokens.size()) {
        throw parser_error("Out of bounds");
    }
    return tokens[current_ + index + 1];
}

bool Parser::match(std::initializer_list<TokenType> types)
{
    for (const auto t : types) {
        if (peek().type == t) {
            advance();
            return true;
        }
    }
    return false;
}

Token Parser::peek() const
{
    if (is_end()) {
        return tokens.back();
    }
    return tokens[current_];
}

Token Parser::previous()
{
    if (current_ == 0 || current_ - 1 >= tokens.size()) {
        LOG_F(ERROR, "token out of bounds");
        return {};
    }
    return tokens[current_ - 1];
}

void Parser::synchronize(bool allow_rbrace)
{
    while (!is_end()) {
        // [TODO] I forget why I took rbrace out, so special case it here for now.
        if (allow_rbrace && peek().type == TokenType::BRACE_CLOSE) {
            return;
        }

        switch (peek().type) {
        default:
            break;
        case TokenType::CONST_:
        case TokenType::VAR:
        case TokenType::FN:
        case TokenType::TYPE:
        case TokenType::FOR:
        case TokenType::IF:
        case TokenType::RETURN:
            return;
        }

        advance();
    }
}

// ---- Expressions -----------------------------------------------------------

Expression* Parser::parse_expr()
{
    ParseDepthGuard depth_guard(*this);
    return parse_expr_assign();
}

Expression* Parser::parse_expr_assign()
{
    auto expr = parse_expr_conditional();

    if (match({TokenType::EQ,
            TokenType::DIVEQ,
            TokenType::MINUSEQ,
            TokenType::MODEQ,
            TokenType::PLUSEQ,
            TokenType::TIMESEQ})) {
        auto equals = previous();
        auto value = parse_expr_assign();

        if (dynamic_cast<PathExpression*>(expr) || dynamic_cast<IndexExpression*>(expr)) {
            auto ae = ast_.create_node<AssignExpression>(expr, equals, value);
            ae->range_.start = expr->range_.start;
            ae->range_.end = value->range_.end;
            return ae;
        }

        diagnostic("Invalid assignment target.", peek());
    }

    return expr;
}

Expression* Parser::parse_expr_conditional()
{
    auto expr = parse_expr_or();
    if (match({TokenType::QUESTION})) {
        auto tbranch = parse_expr_conditional();
        consume(TokenType::COLON, "Expected ':'.");
        auto fbranch = parse_expr_conditional();
        auto ce = ast_.create_node<ConditionalExpression>(expr, tbranch, fbranch);
        ce->range_.start = expr->range_.start;
        ce->range_.end = fbranch->range_.end;
        expr = ce;
    }

    return expr;
}

Expression* Parser::parse_expr_or()
{
    auto expr = parse_expr_and();
    while (match({TokenType::OROR})) {
        auto op = previous();
        auto right = parse_expr_and();
        auto le = ast_.create_node<LogicalExpression>(expr, op, right);
        le->range_.start = expr->range_.start;
        le->range_.end = right->range_.end;
        expr = le;
    }
    return expr;
}

Expression* Parser::parse_expr_and()
{
    auto expr = parse_expr_equality();
    while (match({TokenType::ANDAND})) {
        auto op = previous();
        auto right = parse_expr_equality();
        expr = ast_.create_node<LogicalExpression>(expr, op, right);
    }
    return expr;
}

Expression* Parser::parse_expr_equality()
{
    auto expr = parse_expr_relational();
    while (match({TokenType::NOTEQ, TokenType::EQEQ})) {
        auto op = previous();
        auto right = parse_expr_relational();
        expr = ast_.create_node<ComparisonExpression>(expr, op, right);
    }
    return expr;
}

Expression* Parser::parse_expr_relational()
{
    auto expr = parse_expr_shift();
    while (match({TokenType::GT, TokenType::GTEQ, TokenType::LT, TokenType::LTEQ})) {
        auto op = previous();
        auto right = parse_expr_shift();
        expr = ast_.create_node<ComparisonExpression>(expr, op, right);
    }
    return expr;
}

Expression* Parser::parse_expr_shift()
{
    return parse_expr_additive();
}

Expression* Parser::parse_expr_additive()
{
    auto expr = parse_expr_multiplicative();

    while (match({TokenType::MINUS, TokenType::PLUS})) {
        auto op = previous();
        auto right = parse_expr_multiplicative();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }

    return expr;
}

Expression* Parser::parse_expr_multiplicative()
{
    auto expr = parse_expr_unary();

    while (match({TokenType::DIV, TokenType::TIMES, TokenType::MOD})) {
        auto op = previous();
        auto right = parse_expr_unary();
        expr = ast_.create_node<BinaryExpression>(expr, op, right);
    }

    return expr;
}

Expression* Parser::parse_expr_unary()
{
    if (match({TokenType::NOT,
            TokenType::MINUS,
            TokenType::PLUS})) {
        auto op = previous();
        auto right = parse_expr_unary();

        // Constant fold unary expressions cause this will cause problems later
        // to avoid the AST having (unary - (literal 1)) instead of (literal -1) etc
        if (auto lit = dynamic_cast<LiteralExpression*>(right)) {
            auto fold = [&](Token& op_tok, LiteralExpression* l, Expression* r) -> Expression* {
                l->literal.loc = merge_source_location(op_tok.loc, l->literal.loc);
                r->range_ = l->literal.loc.range;
                return r;
            };

            if (op.type == TokenType::PLUS
                && (lit->literal.type == TokenType::INTEGER_LITERAL
                    || lit->literal.type == TokenType::FLOAT_LITERAL)) {
                return fold(op, lit, right);
            } else if (op.type == TokenType::MINUS) {
                if (lit->literal.type == TokenType::INTEGER_LITERAL) {
                    lit->data = -lit->data.as<int32_t>();
                    return fold(op, lit, right);
                } else if (lit->literal.type == TokenType::FLOAT_LITERAL) {
                    lit->data = -lit->data.as<float>();
                    return fold(op, lit, right);
                }
            } else if (op.type == TokenType::NOT) {
                if (lit->literal.type == TokenType::INTEGER_LITERAL) {
                    lit->data = !lit->data.as<int32_t>();
                    return fold(op, lit, right);
                }
            }
            // Leave everything else for errors later
        }
        auto e = ast_.create_node<UnaryExpression>(op, right);
        e->range_.start = op.loc.range.start;
        e->range_.end = right->range_.end;
        return e;
    }
    return parse_expr_postfix();
}

Expression* Parser::parse_expr_postfix()
{
    auto expr = parse_expr_primary();

    while (match({TokenType::PAREN_OPEN, TokenType::DOT, TokenType::BRACKET_OPEN, TokenType::AS, TokenType::IS})) {
        if (previous().type == TokenType::PAREN_OPEN) {
            auto e = ast_.create_node<CallExpression>(expr, ctx_->arena);
            e->range_.start = expr->range_.start;
            e->arg_range.start = previous().loc.range.end;
            while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
                if (match({TokenType::COMMA})) {
                    e->comma_ranges.push_back(previous().loc.range);
                    diagnostic("expected expression", previous());
                    continue;
                }

                try {
                    auto arg = parse_expr();
                    e->args.push_back(arg);
                } catch (const parser_error& error) {
                    if (match({TokenType::COMMA})) {
                        e->comma_ranges.push_back(previous().loc.range);
                    }
                    break;
                }

                if (match({TokenType::COMMA})) {
                    e->comma_ranges.push_back(previous().loc.range);
                    if (check({TokenType::PAREN_CLOSE})) {
                        diagnostic("expected expression", peek());
                    }
                }
            }
            if (!match({TokenType::PAREN_CLOSE})) {
                diagnostic("Expected ')'.", peek());
                // Bail out, this could be a snippet or something that will lead to a lot more errors
                throw parser_error("Expected ')'");
            }
            e->range_.end = previous().loc.range.end;
            e->arg_range.end = previous().loc.range.start;

            expr = e;
        } else if (previous().type == TokenType::DOT) {
            if (check({TokenType::INTEGER_LITERAL})) {
                auto index_token = peek();
                diagnostic("tuple-style .index syntax is not supported", index_token);
                throw parser_error("Invalid dot index");
            }
            if (!check({TokenType::IDENTIFIER})) {
                diagnostic("expected identifier after '.'", peek());
                throw parser_error("Expected identifier after '.'");
            }

            advance();
            auto ident_token = previous();
            auto* right = parse_identifier_expression(ident_token);
            right->range_ = {ident_token.loc.range.start, previous().loc.range.end};

            PathExpression* path = dynamic_cast<PathExpression*>(expr);
            if (!path) {
                path = ast_.create_node<PathExpression>(ctx_->arena);
                path->parts.push_back(expr);
                path->range_.start = expr->range_.start;
            }

            path->parts.push_back(right);
            path->range_.end = right->range_.end;
            expr = path;
        } else if (previous().type == TokenType::BRACKET_OPEN) {
            auto index = parse_expr();
            if (!match({TokenType::BRACKET_CLOSE})) {
                diagnostic("Expected ']'", peek());
                throw parser_error("Expected ']'");
            }
            auto e = ast_.create_node<IndexExpression>(expr, index);
            e->range_.start = expr->range_.start;
            e->range_.end = previous().loc.range.end;
            expr = e;
        } else if (previous().type == TokenType::AS || previous().type == TokenType::IS) {
            auto op_token = previous();
            auto target_type = parse_type();
            if (!target_type) {
                diagnostic(fmt::format("Expected type after '{}'", op_token.loc.view()), peek());
                throw parser_error(fmt::format("Expected type after '{}'", op_token.loc.view()));
            }
            auto e = ast_.create_node<CastExpression>(ctx_->arena);
            e->expr = expr;
            e->op = op_token;
            e->target_type = target_type;
            e->range_.start = expr->range_.start;
            e->range_.end = target_type->range_.end;
            expr = e;
        }
    }

    return expr;
}

IdentifierExpression* Parser::parse_identifier_expression(Token ident_token)
{
    auto* expr = ast_.create_node<IdentifierExpression>(ident_token, ctx_->arena);
    expr->range_ = ident_token.loc.range;

    if (match({TokenType::NOT})) {
        consume(TokenType::PAREN_OPEN, "Expected '(' after '!'.");
        if (!check({TokenType::PAREN_CLOSE})) {
            while (true) {
                Expression* param;
                if (check({TokenType::IDENTIFIER})) {
                    param = parse_type();
                } else {
                    param = parse_expr_shift();
                }
                expr->type_params.push_back(param);
                if (!match({TokenType::COMMA})) { break; }
            }
        }
        consume(TokenType::PAREN_CLOSE, "Expected closing ')'.");
        expr->range_.end = previous().loc.range.end;
    }

    return expr;
}

// INT | FLOAT | STRING | "(" expression ")" | IDENTIFIER | MACRO
Expression* Parser::parse_expr_primary()
{
    if (match({TokenType::BOOL_LITERAL_TRUE, TokenType::BOOL_LITERAL_FALSE,
            TokenType::STRING_LITERAL, TokenType::STRING_RAW_LITERAL,
            TokenType::INTEGER_LITERAL, TokenType::FLOAT_LITERAL,
            TokenType::_FUNCTION_, TokenType::_FILE_, TokenType::_DATE_, TokenType::_TIME_, TokenType::_LINE_})) {
        auto expr = ast_.create_node<LiteralExpression>(previous());
        expr->range_ = previous().loc.range;
        if (expr->literal.type == TokenType::STRING_LITERAL) {
            String processed = process_escape_sequences(expr->literal.loc.view());
            expr->data = PString(processed, ctx_->arena);
        } else if (expr->literal.type == TokenType::STRING_RAW_LITERAL) {
            expr->data = PString(expr->literal.loc.view(), ctx_->arena);
        } else if (expr->literal.type == TokenType::INTEGER_LITERAL) {
            if (auto val = string::from<int32_t>(expr->literal.loc.view())) {
                expr->data = *val;
            } else {
                ctx_->parse_diagnostic(parent_,
                    fmt::format("unable to parse integer literal '{}'", expr->literal.loc.view()),
                    false,
                    expr->literal.loc.range);
            }
        } else if (expr->literal.type == TokenType::FLOAT_LITERAL) {
            if (auto val = string::from<float>(expr->literal.loc.view())) {
                expr->data = *val;
            } else {
                ctx_->parse_diagnostic(parent_,
                    fmt::format("unable to parse float literal '{}'", expr->literal.loc.view()),
                    false,
                    expr->literal.loc.range);
            }
        } else if (expr->literal.type == TokenType::BOOL_LITERAL_TRUE) {
            expr->data = true;
        } else if (expr->literal.type == TokenType::BOOL_LITERAL_FALSE) {
            expr->data = false;
        }
        return expr;
    } else if (match({TokenType::FSTRING_LITERAL})) {
        auto fstring_token = previous();
        auto expr = parse_fstring(fstring_token);
        expr->range_ = fstring_token.loc.range;
        return expr;
    } else if (match({TokenType::IDENTIFIER})) {
        auto type_token = previous();
        if (match({TokenType::BRACE_OPEN})) {
            auto expr = parse_brace_init_literal();
            expr->type = type_token;
            return expr;
        } else {
            auto* ident_expr = parse_identifier_expression(type_token);
            auto* path = ast_.create_node<PathExpression>(ctx_->arena);
            path->parts.push_back(ident_expr);
            path->range_ = ident_expr->range_;
            return path;
        }
    } else if (match({TokenType::PAREN_OPEN})) {
        // Distinguish tuple literals from grouping expressions

        // Empty tuple: ()
        if (check({TokenType::PAREN_CLOSE})) {
            advance();
            auto* tuple = ast_.create_node<TupleLiteral>(ctx_->arena);
            return tuple;
        }

        // Parse first expression
        auto* first = parse_expr();

        // Check for comma to determine tuple vs grouping
        if (match({TokenType::COMMA})) {
            // It's a tuple: (expr, ...) or (expr,)
            auto* tuple = ast_.create_node<TupleLiteral>(ctx_->arena);
            tuple->elements.push_back(first);

            // Parse remaining elements (if any)
            while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
                tuple->elements.push_back(parse_expr());
                if (!match({TokenType::COMMA})) {
                    break;
                }
            }

            consume(TokenType::PAREN_CLOSE, "Expected ')' after tuple elements.");
            return tuple;
        }

        // No comma - just grouping: (expr)
        consume(TokenType::PAREN_CLOSE, "Expected ')' after expression.");
        return ast_.create_node<GroupingExpression>(first);
    } else if (match({TokenType::BRACE_OPEN})) {
        auto expr = parse_brace_init_literal();
        return expr;
    } else if (match({TokenType::FN})) {
        // Lambda expression: fn(x: int, y: int) { body }
        auto fn_token = previous();
        auto* lambda = ast_.create_node<LambdaExpression>(ctx_->arena);
        lambda->range_.start = fn_token.loc.range.start;

        consume(TokenType::PAREN_OPEN, "Expected '(' after 'fn' in lambda expression.");

        // Parse parameters
        while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
            if (!check({TokenType::IDENTIFIER})) {
                diagnostic("Expected parameter name", peek());
                break;
            }
            advance();
            auto param_name = previous();

            consume(TokenType::COLON, "Expected ':' after parameter name.");

            auto* param = ast_.create_node<VarDecl>();
            param->identifier_ = param_name;
            param->type = parse_type();
            param->range_ = SourceRange{param_name.loc.range.start, param->type->range_.end};
            lambda->params.push_back(param);

            if (!match({TokenType::COMMA})) {
                break;
            }
        }

        consume(TokenType::PAREN_CLOSE, "Expected ')' after lambda parameters.");

        // Parse optional return type
        if (match({TokenType::COLON})) {
            lambda->return_type = parse_type();
        }

        // Parse body
        consume(TokenType::BRACE_OPEN, "Expected '{' to start lambda body.");
        lambda->body = parse_stmt_block();
        lambda->range_.end = lambda->body->range_.end;

        return lambda;
    }

    diagnostic(fmt::format("expected primary expression: '{}'", peek().loc.view()), peek());
    throw parser_error("expected primary expression");
}

// ---- Statements ------------------------------------------------------------

TypeExpression* Parser::parse_type()
{
    ParseDepthGuard depth_guard(*this);
    // Check for function type: fn(T1, T2): R
    if (check({TokenType::FN})) {
        auto fn_token = advance();
        auto* fn_type = ast_.create_node<TypeExpression>();
        fn_type->range_.start = fn_token.loc.range.start;
        fn_type->is_function_type = true;

        consume(TokenType::PAREN_OPEN, "Expected '(' after 'fn' in function type.");

        // Parse parameter types
        while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
            fn_type->params.push_back(parse_type());
            if (!match({TokenType::COMMA})) {
                break;
            }
        }

        consume(TokenType::PAREN_CLOSE, "Expected ')' after function parameter types.");

        // Parse optional return type
        if (match({TokenType::COLON})) {
            fn_type->fn_return_type = parse_type();
            fn_type->range_.end = fn_type->fn_return_type->range_.end;
        } else {
            fn_type->range_.end = previous().loc.range.end;
        }

        return fn_type;
    }

    // Check for tuple type: (Type1, Type2, ...)
    if (check({TokenType::PAREN_OPEN})) {
        auto paren_open = advance();
        auto* tuple_type = ast_.create_node<TypeExpression>();
        tuple_type->range_.start = paren_open.loc.range.start;

        // Parse tuple element types
        while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
            tuple_type->params.push_back(parse_type());
            if (!match({TokenType::COMMA})) {
                break;
            }
        }

        consume(TokenType::PAREN_CLOSE, "Expected ')' after tuple types.");
        tuple_type->range_.end = previous().loc.range.end;

        // Tuple types are identified by having params but no name
        // The resolver will recognize this pattern and create a tuple TypeID
        return tuple_type;
    }

    auto* t = ast_.create_node<TypeExpression>();
    Token first_ident = previous();
    // Don't use full expression parser to avoid parsing brace-init like `int { ... }`
    if (!check({TokenType::IDENTIFIER})) {
        diagnostic("expected type name", peek());
        return t;
    }

    advance();
    auto name_token = previous();
    auto* name_expr = ast_.create_node<IdentifierExpression>(name_token, ctx_->arena);
    name_expr->range_ = name_token.loc.range;
    auto* path_expr = ast_.create_node<PathExpression>(ctx_->arena);
    path_expr->parts.push_back(name_expr);
    path_expr->range_ = name_expr->range_;
    t->name = path_expr;
    t->range_.start = name_token.loc.range.start;

    while (match({TokenType::DOT})) {
        auto dot_token = previous();
        if (!check({TokenType::IDENTIFIER})) {
            diagnostic("expected identifier after '.'", peek());
            break;
        }
        advance();
        auto rhs_token = previous();
        auto* rhs_expr = ast_.create_node<IdentifierExpression>(rhs_token, ctx_->arena);
        rhs_expr->range_ = rhs_token.loc.range;

        path_expr->parts.push_back(rhs_expr);
        path_expr->range_.end = rhs_expr->range_.end;
        t->name = path_expr;
    }

    if (match({TokenType::NOT})) {
        consume(TokenType::PAREN_OPEN, "Expected '(' after '!'.");
        if (!check({TokenType::PAREN_CLOSE})) {
            while (true) {
                Expression* param;
                if (check({TokenType::IDENTIFIER})) {
                    param = parse_type();
                } else {
                    param = parse_expr_shift();
                }
                t->params.push_back(param);
                if (!match({TokenType::COMMA})) { break; }
            }
        }
        consume(TokenType::PAREN_CLOSE, "Expected closing ')'.");
        t->range_.end = previous().loc.range.end;
    } else if (t->name) {
        t->range_.end = t->name->range_.end;
    }

    // Check for fixed-size array syntax: T[N]
    if (match({TokenType::BRACKET_OPEN})) {
        auto bracket_token = previous();

        // Parse array size (must be a constant expression)
        Expression* size_expr = parse_expr_shift();

        consume(TokenType::BRACKET_CLOSE, "Expected ']' after array size.");

        // Create a new TypeExpression representing T[N]
        // Store the entire TypeExpression 't' as a nested type, not just the name
        auto* fixed_array_type = ast_.create_node<TypeExpression>();

        // Create a wrapper TypeExpression for the element type if needed
        auto* elem_type_wrapper = ast_.create_node<TypeExpression>();
        elem_type_wrapper->name = t->name;
        elem_type_wrapper->params = t->params;
        elem_type_wrapper->range_ = t->range_;

        fixed_array_type->name = elem_type_wrapper;    // Store as TypeExpression
        fixed_array_type->params.push_back(size_expr); // The size
        fixed_array_type->is_fixed_array = true;       // Mark as fixed array
        fixed_array_type->range_.start = t->range_.start;
        fixed_array_type->range_.end = previous().loc.range.end;

        return fixed_array_type;
    }

    return t;
}

BraceInitLiteral* Parser::parse_brace_init_literal()
{
    Token start;
    auto expr = ast_.create_node<BraceInitLiteral>(ctx_->arena);

    if (!check({TokenType::BRACE_CLOSE})) {
        start = previous();
        bool first_item = true;
        do {
            BraceInitItem item;

            // Parse the first expression (could be a key or a value)
            auto first_expr = parse_expr_conditional();
            if (!first_expr) {
                diagnostic("Expected expression", peek());
                break;
            }

            // Check what comes after to determine the type
            if (peek().type == TokenType::EQ) {
                // Field syntax: key = value
                if (first_item) expr->init_type = BraceInitType::field;
                if (auto path = dynamic_cast<PathExpression*>(first_expr)) {
                    if (path->parts.size() == 1) {
                        item.key = dynamic_cast<IdentifierExpression*>(path->parts[0]);
                    }
                }
                if (!item.key) {
                    diagnostic("expected identifier for brace initializer field", start);
                    break;
                }
                if (!match({TokenType::EQ})) {
                    diagnostic("Expected '='", peek());
                    break;
                }
                item.value = parse_expr();
                if (!item.value) {
                    diagnostic("Expected expression", peek());
                    break;
                }
            } else if (peek().type == TokenType::COLON) {
                // KV syntax: key: value
                if (first_item) expr->init_type = BraceInitType::kv;
                if (auto path = dynamic_cast<PathExpression*>(first_expr)) {
                    if (path->parts.size() == 1) {
                        item.key = dynamic_cast<IdentifierExpression*>(path->parts[0]);
                    }
                }
                if (!item.key) {
                    item.key = first_expr;
                }
                if (!match({TokenType::COLON})) {
                    diagnostic("Expected ':'", peek());
                    break;
                }
                item.value = parse_expr();
                if (!item.value) {
                    diagnostic("Expected expression after ':'", peek());
                    break;
                }
            } else {
                // List syntax: just value
                if (first_item) expr->init_type = BraceInitType::list;
                item.value = first_expr;
            }

            expr->items.push_back(item);
            first_item = false;

            if (!match({TokenType::COMMA})) {
                break;
            }

            if (check({TokenType::BRACE_CLOSE})) {
                break;
            }

        } while (true);
    }

    consume(TokenType::BRACE_CLOSE, "Expected '}'");
    expr->range_.start = start.loc.range.start;
    expr->range_.end = previous().loc.range.end;

    bool has_named = false, has_positional = false;
    for (const auto& it : expr->items) {
        if (it.key) {
            has_named = true;
        } else {
            has_positional = true;
        }
    }

    if (has_named && has_positional) {
        diagnostic("cannot mix positional and named syntax in brace initializer", start);
    }

    return expr;
}

// statement -> exprStmt
//           | forStmt
//           | ifStmt
//           | jumpStmt
//           | switchStmt
//           | block
Statement* Parser::parse_stmt()
{
    ParseDepthGuard depth_guard(*this);
    if (match({TokenType::IF})) {
        return parse_stmt_if();
    }

    if (match({TokenType::FOR})) {
        return parse_stmt_for();
    }

    if (match({TokenType::RETURN, TokenType::BREAK, TokenType::CONTINUE})) {
        return parse_stmt_jump();
    }

    if (match({TokenType::DEFAULT, TokenType::CASE})) {
        return parse_stmt_label();
    }

    if (match({TokenType::SWITCH})) {
        return parse_stmt_switch();
    }

    if (match({TokenType::BRACE_OPEN})) {
        return parse_stmt_block();
    }

    if (match({TokenType::SEMICOLON})) {
        return ast_.create_node<EmptyStatement>();
    }

    // Check for tuple assignment: a, b, c = expr
    // Lookahead to see if this is identifier , identifier ... =
    if (check({TokenType::IDENTIFIER})) {
        bool is_tuple_assign = false;
        {
            TokenPositionGuard guard(*this);

            // Skip past identifiers and commas
            while (check({TokenType::IDENTIFIER})) {
                advance();
                if (!match({TokenType::COMMA})) {
                    if (check({TokenType::EQ})) {
                        is_tuple_assign = (guard.saved < current_ - 1); // Multiple identifiers
                    }
                    break;
                }
            }
        }

        if (is_tuple_assign) {
            // Parse tuple assignment
            auto* tuple_lhs = ast_.create_node<TupleLiteral>(ctx_->arena);
            do {
                consume(TokenType::IDENTIFIER, "Expected identifier");
                auto var_token = previous();
                auto* ident_expr = ast_.create_node<IdentifierExpression>(var_token, ctx_->arena);
                ident_expr->range_ = var_token.loc.range;
                auto* path = ast_.create_node<PathExpression>(ctx_->arena);
                path->parts.push_back(ident_expr);
                path->range_ = ident_expr->range_;
                tuple_lhs->elements.push_back(path);
            } while (match({TokenType::COMMA}));

            consume(TokenType::EQ, "Expected '='");
            auto eq_token = previous();
            auto* value = parse_expr();
            consume(TokenType::SEMICOLON, "Expected ';'");

            // Create assignment
            auto* assign = ast_.create_node<AssignExpression>(tuple_lhs, eq_token, value);
            auto s = ast_.create_node<ExprStatement>();
            s->expr = assign;
            return s;
        }
    }

    auto s = ast_.create_node<ExprStatement>();
    s->expr = parse_expr();
    if (!match({TokenType::SEMICOLON})) {
        diagnostic("Expected ';'.", peek());
        synchronize(true);
    }

    return s;
}

// block -> "{" declaration* "}"
BlockStatement* Parser::parse_stmt_block()
{
    auto s = ast_.create_node<BlockStatement>(ctx_->arena);
    s->range_.start = previous().loc.range.end; // Don't include the braces in the range.. for now.
    while (!is_end() && !check({TokenType::BRACE_CLOSE})) {
        size_t before = current_;
        try {
            auto n = parse_decl();
            if (!n) {
                // Not a declaration, try parsing as a statement
                n = parse_stmt();
            }

            if (auto fdef = dynamic_cast<FunctionDefinition*>(n)) {
                diagnostic("blocks cannot contain nested function definitions", fdef->identifier_);
            } else if (auto sd = dynamic_cast<StructDecl*>(n)) {
                Token diag_token = peek();
                if (auto name_path = dynamic_cast<PathExpression*>(sd->type->name)) {
                    if (!name_path->parts.empty()) {
                        if (auto ident = dynamic_cast<IdentifierExpression*>(name_path->parts.front())) {
                            diag_token = ident->ident;
                        }
                    }
                }
                diagnostic("blocks cannot contain nested other struct declarations", diag_token);
            } else if (n) {
                s->nodes.push_back(n);
            }
        } catch (const parser_error&) {
            if (peek().type != TokenType::BRACE_CLOSE) {
                synchronize(true);
            }
        }

        // Ensure forward progress even on malformed inputs.
        if (current_ == before) {
            if (is_end() || check({TokenType::BRACE_CLOSE})) {
                break;
            }
            advance();
        }
    }

    if (!match({TokenType::BRACE_CLOSE})) {
        diagnostic("Expected '}'.", peek());
    }
    s->range_.end = previous().loc.range.start;
    return s;
}

// exprStmt -> expression ";"
ExprStatement* Parser::parse_stmt_expr()
{
    auto s = ast_.create_node<ExprStatement>();
    s->expr = parse_expr();
    s->range_.start = s->expr->range_.start;
    consume(TokenType::SEMICOLON, "Expected ';'.");
    s->range_.end = previous().loc.range.end;
    return s;
}

// ifStmt -> if "(" expression ")" block ( else block )?
IfStatement* Parser::parse_stmt_if()
{
    auto s = ast_.create_node<IfStatement>();
    s->range_.start = previous().loc.range.start;
    consume(TokenType::PAREN_OPEN, "Expected '('.");
    s->expr = parse_expr();
    consume(TokenType::PAREN_CLOSE, "Expected ')'.");
    consume(TokenType::BRACE_OPEN, "Expected '{'.");
    s->range_.end = previous().loc.range.end;
    s->if_branch = parse_stmt_block();
    if (match({TokenType::ELSE})) {
        consume(TokenType::BRACE_OPEN, "Expected '{'.");
        s->else_branch = parse_stmt_block();
    }
    s->range_.end = previous().loc.range.end;
    return s;
}

// forStmt -> for block                                                       (infinite loop)
//         |  for ( expression ) block                                        (while-style)
//         |  for ( (decl | expression)? ; expression? ; expression? ) block  (C-style)
//         |  for ( var x in expr ) block                                     (for-each array)
//         |  for ( var k, v in expr ) block                                  (for-each map)
Statement* Parser::parse_stmt_for()
{
    auto start_range = previous().loc.range.start;

    if (check({TokenType::BRACE_OPEN})) {
        auto s = ast_.create_node<ForStatement>();
        s->range_.start = start_range;
        consume(TokenType::BRACE_OPEN, "Expected '{'.");
        s->block = parse_stmt_block();
        s->range_.end = previous().loc.range.end;
        return s;
    }

    consume(TokenType::PAREN_OPEN, "Expected '(' or '{'.");

    // Detect for-each: for (var x in ...) or for (var k, v in ...)
    if (check({TokenType::VAR})) {
        bool is_foreach = false;
        {
            TokenPositionGuard guard(*this);
            advance(); // var
            if (check({TokenType::IDENTIFIER})) {
                advance();
                // Check for comma (second var) or 'in'
                if (check({TokenType::COMMA}) || check({TokenType::IN})) {
                    is_foreach = true;
                } else if (check({TokenType::COLON})) {
                    // Check for type annotation then 'in'
                    advance();
                    parse_type();
                    if (check({TokenType::IN})) {
                        is_foreach = true;
                    } else if (check({TokenType::COMMA})) {
                        advance();
                        if (check({TokenType::IDENTIFIER})) {
                            advance();
                            if (check({TokenType::COLON})) {
                                advance();
                                parse_type();
                            }
                            if (check({TokenType::IN})) {
                                is_foreach = true;
                            }
                        }
                    }
                }
            }
        }
        if (is_foreach) {
            return parse_stmt_foreach();
        }
    }

    auto s = ast_.create_node<ForStatement>();
    s->range_.start = start_range;

    bool need_check_inc = true;

    if (check({TokenType::SEMICOLON})) {
        consume(TokenType::SEMICOLON, "Expected ';'.");
    } else if (check_is_decl()) {
        s->init = parse_decl();
    } else {
        auto expr = parse_expr();

        if (check({TokenType::PAREN_CLOSE})) {
            // while-style: for (expr)
            s->check = expr;
            consume(TokenType::PAREN_CLOSE, "Expected ')'.");
            need_check_inc = false;
        } else {
            s->init = expr;
            consume(TokenType::SEMICOLON, "Expected ';'.");
        }
    }

    if (need_check_inc) {
        if (!check({TokenType::SEMICOLON})) {
            s->check = parse_expr();
        }
        consume(TokenType::SEMICOLON, "Expected ';'.");
        if (!check({TokenType::PAREN_CLOSE})) {
            s->inc = parse_expr();
        }
        consume(TokenType::PAREN_CLOSE, "Expected ')'.");
    }

    consume(TokenType::BRACE_OPEN, "Expected '{'.");
    s->block = parse_stmt_block();
    s->range_.end = previous().loc.range.end;
    return s;
}

// foreachStmt -> for ( var x in expr ) block            (for-each array)
//             |  for ( var k, v in expr ) block         (for-each map)
ForEachStatement* Parser::parse_stmt_foreach()
{
    auto s = ast_.create_node<ForEachStatement>();
    s->range_.start = tokens[current_ - 1].loc.range.start; // 'for' token

    consume(TokenType::VAR, "Expected 'var'.");

    // Parse first variable
    Token var1_name = consume(TokenType::IDENTIFIER, "Expected identifier.");
    auto var1 = ast_.create_node<VarDecl>();
    var1->identifier_ = var1_name;
    var1->range_.start = var1_name.loc.range.start;

    // Check for optional type annotation on first variable
    if (check({TokenType::COLON})) {
        advance();
        var1->type = parse_type();
    }
    var1->range_.end = previous().loc.range.end;

    // Check for second variable (map iteration)
    if (check({TokenType::COMMA})) {
        advance();
        Token var2_name = consume(TokenType::IDENTIFIER, "Expected identifier after ','.");
        auto var2 = ast_.create_node<VarDecl>();
        var2->identifier_ = var2_name;
        var2->range_.start = var2_name.loc.range.start;

        // Check for optional type annotation on second variable
        if (check({TokenType::COLON})) {
            advance();
            var2->type = parse_type();
        }
        var2->range_.end = previous().loc.range.end;

        s->key_var = var1;
        s->value_var = var2;
    } else {
        s->var = var1;
    }

    consume(TokenType::IN, "Expected 'in'.");
    s->collection = parse_expr();
    consume(TokenType::PAREN_CLOSE, "Expected ')'.");

    consume(TokenType::BRACE_OPEN, "Expected '{'.");
    s->block = parse_stmt_block();
    s->range_.end = previous().loc.range.end;
    return s;
}

// returnStmt -> return expression (, expression)* ;
JumpStatement* Parser::parse_stmt_jump()
{
    auto s = ast_.create_node<JumpStatement>(ctx_->arena);
    s->range_.start = previous().loc.range.start;
    s->op = previous();
    if (s->op.type == TokenType::RETURN && !check({TokenType::SEMICOLON})) {
        s->exprs.push_back(parse_expr());
        while (match({TokenType::COMMA})) {
            s->exprs.push_back(parse_expr());
        }
    }
    consume(TokenType::SEMICOLON, "Expected ';'.");
    s->range_.end = previous().loc.range.end;
    return s;
}

void Parser::parse_pattern_bindings(LabelStatement* s)
{
    if (!check({TokenType::PAREN_CLOSE})) {
        do {
            consume(TokenType::IDENTIFIER, "Expected binding variable name.");
            auto var_decl = ast_.create_node<VarDecl>();
            var_decl->identifier_ = previous();
            var_decl->range_.start = var_decl->identifier_.loc.range.start;
            var_decl->range_.end = var_decl->identifier_.loc.range.end;
            var_decl->range_selection_ = var_decl->identifier_.loc.range;
            s->bindings.push_back(var_decl);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::PAREN_CLOSE, "Expected ')' after pattern bindings.");

    if (match({TokenType::IF})) {
        s->guard = parse_expr();
    }
}

LabelStatement* Parser::parse_stmt_label()
{
    auto s = ast_.create_node<LabelStatement>(ctx_->arena);
    s->range_.start = previous().loc.range.start;
    s->type = previous();

    if (s->type.type == TokenType::CASE) {
        if (check({TokenType::IDENTIFIER})) {
            bool fallback = false;
            {
                TokenPositionGuard guard(*this);
                Token first_ident = consume(TokenType::IDENTIFIER, "Expected identifier.");

                // Check for qualified name: Color.Red
                auto* path_expr = ast_.create_node<PathExpression>(ctx_->arena);
                auto* first_expr = parse_identifier_expression(first_ident);
                path_expr->parts.push_back(first_expr);
                path_expr->range_.start = first_expr->range_.start;

                if (match({TokenType::DOT})) {
                    Token variant_name = consume(TokenType::IDENTIFIER, "Expected variant name after '.'.");
                    auto* rhs_expr = parse_identifier_expression(variant_name);
                    path_expr->parts.push_back(rhs_expr);
                    path_expr->range_.end = rhs_expr->range_.end;

                    if (match({TokenType::PAREN_OPEN})) {
                        // Qualified with bindings: Color.Ok(x)
                        guard.commit();
                        s->is_pattern_match = true;
                        s->expr = path_expr;
                        parse_pattern_bindings(s);
                    } else if (check({TokenType::COLON})) {
                        // Qualified unit variant: Color.Red
                        guard.commit();
                        s->is_pattern_match = true;
                        s->expr = path_expr;
                    } else {
                        fallback = true;
                    }
                } else if (match({TokenType::PAREN_OPEN})) {
                    guard.commit();
                    s->is_pattern_match = true;
                    path_expr->range_.end = first_expr->range_.end;
                    s->expr = path_expr;
                    parse_pattern_bindings(s);
                } else if (check({TokenType::COLON})) {
                    guard.commit();
                    s->is_pattern_match = true;
                    path_expr->range_.end = first_expr->range_.end;
                    s->expr = path_expr;
                } else {
                    fallback = true;
                }
            }
            if (fallback) {
                s->expr = parse_expr();
            }
        } else {
            s->expr = parse_expr();
        }
    }

    consume(TokenType::COLON, "Expected ':'.");
    s->range_.end = previous().loc.range.end;
    return s;
}

SwitchStatement* Parser::parse_stmt_switch()
{
    auto s = ast_.create_node<SwitchStatement>();
    s->range_.start = previous().loc.range.start;
    consume(TokenType::PAREN_OPEN, "Expected '('.");
    s->target = parse_expr();
    consume(TokenType::PAREN_CLOSE, "Expected ')'.");
    consume(TokenType::BRACE_OPEN, "Expected '{'.");
    s->block = parse_stmt_block();
    s->range_.end = previous().loc.range.end;
    return s;
}

// Parse single annotation before a declaration
// annotation -> [[ IDENTIFIER ( argList )? ]]
// argList -> arg (, arg)*
// arg -> expression
//      | IDENTIFIER = expression
Vector<Annotation> Parser::parse_annotations()
{
    Vector<Annotation> annotations;

    if (match({TokenType::ANNOTATION_OPEN})) {
        Annotation annot;
        annot.range_.start = previous().loc.range.start;

        consume(TokenType::IDENTIFIER, "Expected annotation name");
        annot.name = previous();

        if (match({TokenType::PAREN_OPEN})) {
            if (!check({TokenType::PAREN_CLOSE})) {
                do {
                    AnnotationArg arg;
                    if (check({TokenType::IDENTIFIER}) && lookahead(0).type == TokenType::EQ) {
                        arg.name = consume(TokenType::IDENTIFIER, "Expected identifier");
                        consume(TokenType::EQ, "Expected '='");
                        arg.is_named = true;
                    }

                    arg.value = parse_expr();
                    annot.args.push_back(arg);
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::PAREN_CLOSE, "Expected ')'");
        }

        consume(TokenType::ANNOTATION_CLOSE, "Expected ']]'");
        annot.range_.end = previous().loc.range.end;
        annotations.push_back(annot);
    }

    return annotations;
}

// declaration -> (const|var) identifier_list [: TYPE] [= expr_list] ;
//             | func IDENTIFIER(parameter*) -> TYPE block
//             | type IDENTIFIER { declaration+ }
//             | type IDENTIFIER = TYPE ;
//             | type IDENTIFIER ( TYPE ) ;
//
// identifier_list -> IDENTIFIER (, IDENTIFIER)*
// expr_list       -> expression (, expression)*
Statement* Parser::parse_decl(bool top_level)
{
    ParseDepthGuard depth_guard(*this);
    auto annotations = parse_annotations();

    Statement* result = nullptr;

    if (match({TokenType::IMPORT, TokenType::FROM})) {
        current_ = current_ - 1;
        result = parse_import();
        if (result) {
            if (auto import = dynamic_cast<ImportDecl*>(result)) {
                ast_.imports.push_back(import);
            }
        }
    } else if (match({TokenType::CONST_, TokenType::VAR})) {
        auto var_const_token = previous();
        bool is_const = var_const_token.type == TokenType::CONST_;
        auto start_pos = var_const_token.loc.range.start;

        Vector<Token> identifiers;
        consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        identifiers.push_back(previous());

        while (match({TokenType::COMMA})) {
            consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
            identifiers.push_back(previous());
        }

        TypeExpression* type = nullptr;
        bool has_type = false;
        if (match({TokenType::COLON})) {
            type = parse_type();
            has_type = true;
        }

        Vector<Expression*> initializers;
        if (match({TokenType::EQ})) {
            initializers.push_back(parse_expr());
            while (match({TokenType::COMMA})) {
                initializers.push_back(parse_expr());
            }
        }

        consume(TokenType::SEMICOLON, "Expected ';'.");
        auto end_pos = previous().loc.range.end;

        if (!has_type && initializers.empty()) {
            diagnostic("Variable declaration must have either type or initializer", var_const_token);
        }

        if (!initializers.empty()
            && initializers.size() != identifiers.size()
            && initializers.size() != 1) {
            diagnostic(fmt::format("Expected {} initializers or 1 (for tuple destructuring), got {}",
                           identifiers.size(), initializers.size()),
                var_const_token);
        }

        if (identifiers.size() == 1) {
            auto vd = ast_.create_node<VarDecl>();
            vd->identifier_ = identifiers[0];
            vd->type = type;
            vd->is_const_ = is_const;
            if (!initializers.empty()) {
                vd->init = initializers[0];
            }
            vd->range_.start = start_pos;
            vd->range_.end = end_pos;
            vd->range_selection_ = vd->identifier_.loc.range;
            result = vd;
        } else {
            auto decls = ast_.create_node<DeclList>(ctx_->arena);
            decls->type = type;
            decls->is_const_ = is_const;

            bool is_tuple_destructure = initializers.size() == 1;

            for (size_t i = 0; i < identifiers.size(); ++i) {
                auto vd = ast_.create_node<VarDecl>();
                vd->identifier_ = identifiers[i];
                vd->type = type;
                vd->is_const_ = is_const;
                if (is_tuple_destructure) {
                    vd->init = initializers[0];
                } else if (i < initializers.size()) {
                    vd->init = initializers[i];
                }
                vd->range_.start = start_pos;
                vd->range_.end = end_pos;
                vd->range_selection_ = vd->identifier_.loc.range;
                decls->decls.push_back(vd);
            }

            decls->range_.start = start_pos;
            decls->range_.end = end_pos;
            result = decls;
        }

    } else if (match({TokenType::TYPE})) {
        consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        auto name_token = previous();
        auto name_expr = ast_.create_node<IdentifierExpression>(name_token, ctx_->arena);
        name_expr->range_ = name_token.loc.range;
        auto name_path = ast_.create_node<PathExpression>(ctx_->arena);
        name_path->parts.push_back(name_expr);
        name_path->range_ = name_expr->range_;
        auto type_name = ast_.create_node<TypeExpression>();
        type_name->name = name_path;

        if (match({TokenType::NOT})) {
            consume(TokenType::PAREN_OPEN, "Expected '(' after '!'.");
            if (!check({TokenType::PAREN_CLOSE})) {
                while (true) {
                    auto* param = parse_type();
                    type_name->params.push_back(param);
                    if (!match({TokenType::COMMA})) { break; }
                }
            }
            consume(TokenType::PAREN_CLOSE, "Expected closing ')'.");
        }

        if (match({TokenType::BRACE_OPEN})) {
            auto sd = parse_decl_struct();
            sd->type = type_name;
            sd->range_.start = sd->type->name->range_.start;
            sd->range_selection_ = sd->type->name->range_;
            result = sd;
        } else if (match({TokenType::EQ})) {
            bool is_sum = false;
            if (check({TokenType::IDENTIFIER})) {
                TokenPositionGuard guard(*this);
                advance();
                is_sum = check({TokenType::OR, TokenType::PAREN_OPEN});
            }

            if (is_sum) {
                auto sum = parse_decl_sum();
                sum->type = type_name;
                sum->range_.start = sum->type->name->range_.start;
                sum->range_selection_ = sum->type->name->range_;
                result = sum;
            } else {
                auto alias = ast_.create_node<TypeAlias>();
                alias->type = type_name;
                alias->aliased_type = parse_type();
                consume(TokenType::SEMICOLON, "Expected ';'.");
                alias->range_.start = alias->type->name->range_.start;
                alias->range_.end = previous().loc.range.end;
                alias->range_selection_ = alias->type->name->range_;
                result = alias;
            }
        } else if (match({TokenType::PAREN_OPEN})) {
            auto newtype = ast_.create_node<NewtypeDecl>();
            newtype->type = type_name;
            newtype->wrapped_type = parse_type();
            consume(TokenType::PAREN_CLOSE, "Expected ')'.");
            consume(TokenType::SEMICOLON, "Expected ';'.");
            newtype->range_.start = newtype->type->name->range_.start;
            newtype->range_.end = previous().loc.range.end;
            newtype->range_selection_ = newtype->type->name->range_;
            result = newtype;
        } else if (match({TokenType::SEMICOLON})) {
            auto opaque = ast_.create_node<OpaqueTypeDecl>();
            opaque->type = type_name;
            opaque->range_.start = opaque->type->name->range_.start;
            opaque->range_.end = previous().loc.range.end;
            opaque->range_selection_ = opaque->type->name->range_;
            result = opaque;
        } else {
            diagnostic("Expected '{', '=', '(', or ';' after type name", peek());
        }
    } else if (match({TokenType::FN})) {
        result = parse_decl_function_def();
    }

    if (!result && top_level) {
        diagnostic("expected declaration", peek(), true);
        advance();
        synchronize();
    }

    if (result) {
        if (auto decl = dynamic_cast<Declaration*>(result)) {
            decl->annotations_ = std::move(annotations);
        } else if (auto decl_list = dynamic_cast<DeclList*>(result)) {
            for (auto d : decl_list->decls) {
                d->annotations_ = annotations;
            }
        }
    }

    return result;
}

StructDecl* Parser::parse_decl_struct()
{
    auto decl = ast_.create_node<StructDecl>(ctx_->arena);

    while (!is_end() && !check({TokenType::BRACE_CLOSE})) {
        try {
            auto member = parse_decl_struct_member();
            decl->decls.push_back(member);
        } catch (const parser_error&) {
            synchronize(true);
        }
    }
    consume(TokenType::BRACE_CLOSE, "Expected '}'.");
    if (!match({TokenType::SEMICOLON})) {
        diagnostic("Expected ';'.", peek());
    }
    decl->range_.end = previous().loc.range.start;

    return decl;
}

Declaration* Parser::parse_decl_struct_member()
{
    auto annotations = parse_annotations();
    Vector<Token> identifiers;

    consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    identifiers.push_back(previous());

    while (match({TokenType::COMMA})) {
        consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
        identifiers.push_back(previous());
    }

    consume(TokenType::COLON, "Expected ':'.");
    TypeExpression* type = parse_type();
    consume(TokenType::SEMICOLON, "Expected ';'.");

    if (identifiers.size() == 1) {
        auto vd = ast_.create_node<VarDecl>();
        vd->identifier_ = identifiers[0];
        vd->type = type;
        vd->range_.start = vd->identifier_.loc.range.start;
        vd->range_.end = previous().loc.range.end;
        vd->range_selection_ = vd->identifier_.loc.range;
        vd->annotations_ = std::move(annotations);
        return vd;
    } else {
        auto decls = ast_.create_node<DeclList>(ctx_->arena);
        decls->type = type;

        for (const auto& id : identifiers) {
            auto vd = ast_.create_node<VarDecl>();
            vd->identifier_ = id;
            vd->type = type;
            vd->range_.start = vd->identifier_.loc.range.start;
            vd->range_.end = previous().loc.range.end;
            vd->range_selection_ = vd->identifier_.loc.range;
            vd->annotations_ = annotations;
            decls->decls.push_back(vd);
        }

        decls->range_.start = identifiers.front().loc.range.start;
        decls->range_.end = previous().loc.range.end;
        return decls;
    }
}

SumDecl* Parser::parse_decl_sum()
{
    auto decl = ast_.create_node<SumDecl>(ctx_->arena);

    while (!is_end()) {
        auto variant = parse_variant();
        decl->variants.push_back(variant);

        if (!match({TokenType::OR})) {
            break;
        }
    }

    consume(TokenType::SEMICOLON, "Expected ';' after sum type declaration.");
    decl->range_.end = previous().loc.range.end;

    return decl;
}

VariantDecl* Parser::parse_variant()
{
    auto variant = ast_.create_node<VariantDecl>();

    consume(TokenType::IDENTIFIER, "Expected variant name.");
    variant->identifier_ = previous();
    variant->range_.start = variant->identifier_.loc.range.start;

    if (match({TokenType::PAREN_OPEN})) {
        auto paren_start = previous();

        // Parse comma-separated payload types
        std::vector<TypeExpression*> payload_types;
        while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
            payload_types.push_back(parse_type());
            if (!match({TokenType::COMMA})) {
                break;
            }
        }

        consume(TokenType::PAREN_CLOSE, "Expected ')' after variant payload type.");

        if (payload_types.size() == 1) {
            // Single payload: use it directly
            variant->payload = payload_types[0];
        } else {
            // Multiple payloads: create a tuple type
            auto* tuple_type = ast_.create_node<TypeExpression>();
            tuple_type->range_.start = paren_start.loc.range.start;
            tuple_type->range_.end = previous().loc.range.end;
            for (auto* type : payload_types) {
                tuple_type->params.push_back(type);
            }
            variant->payload = tuple_type;
        }

        variant->range_.end = previous().loc.range.end;
    } else {
        variant->payload = nullptr;
        variant->range_.end = variant->identifier_.loc.range.end;
    }

    return variant;
}

FunctionDefinition* Parser::parse_decl_function_def()
{
    auto def = ast_.create_node<FunctionDefinition>(ctx_->arena);

    consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    def->identifier_ = previous();
    def->range_.start = def->identifier_.loc.range.start;
    def->range_selection_ = def->identifier_.loc.range;

    consume(TokenType::PAREN_OPEN, "Expected '('.");
    while (!is_end() && !check({TokenType::PAREN_CLOSE})) {
        def->params.emplace_back(parse_decl_param());
        match({TokenType::COMMA});
    }
    consume(TokenType::PAREN_CLOSE, "Expected ')'.");

    if (match({TokenType::COLON})) {
        def->return_type = parse_type();
    }

    // Function can have a body or just be a declaration (ended with semicolon)
    if (match({TokenType::SEMICOLON})) {
        // Function declaration without body (e.g., native function)
        def->block = nullptr;
        def->range_.end = previous().loc.range.end;
    } else {
        // Function definition with body
        consume(TokenType::BRACE_OPEN, "Expected '{' or ';' after function signature.");
        def->block = parse_stmt_block();
        def->range_.end = previous().loc.range.end;
    }

    return def;
}

VarDecl* Parser::parse_decl_param()
{
    auto vd = ast_.create_node<VarDecl>();

    consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER'.");
    vd->identifier_ = previous();
    vd->range_.start = vd->identifier_.loc.range.start;
    vd->range_selection_ = vd->identifier_.loc.range;
    if (match({TokenType::COLON})) {
        vd->type = parse_type();
    }
    if (match({TokenType::EQ})) {
        vd->init = parse_expr_conditional();
    }

    if (vd->init) {
        vd->range_.end = vd->init->range_.end;
    } else if (vd->type) {
        vd->range_.end = vd->type->range_.end;
    } else {
        vd->range_.end = vd->identifier_.loc.range.end;
    }
    return vd;
}

// Parses a module path: identifier(.identifier)*
String Parser::parse_module_path()
{
    consume(TokenType::IDENTIFIER, "Expected module path identifier.");
    String path(previous().loc.view());

    while (match({TokenType::DOT})) {
        consume(TokenType::IDENTIFIER, "Expected identifier after '.'");
        path += ".";
        path += String(previous().loc.view());
    }

    return path;
}

ImportDecl* Parser::parse_import()
{
    // from core.math.vector import { Vector, Point };
    if (match({TokenType::FROM})) {
        String module_path = parse_module_path();

        consume(TokenType::IMPORT, "Expected 'import' after module path.");
        consume(TokenType::BRACE_OPEN, "Expected '{' after 'import'.");

        auto decl = ast_.create_node<SelectiveImportDecl>();
        decl->module_path = module_path;

        while (!is_end() && !check({TokenType::BRACE_CLOSE})) {
            consume(TokenType::IDENTIFIER, "Expected symbol name.");
            decl->imported_symbols.push_back(previous());
            if (!match({TokenType::COMMA})) {
                break;
            }
        }

        consume(TokenType::BRACE_CLOSE, "Expected '}' after import list.");
        consume(TokenType::SEMICOLON, "Expected ';' after import statement.");

        return decl;
    }

    // import core.math.vector as vec;
    if (match({TokenType::IMPORT})) {
        String module_path = parse_module_path();

        // Require "as" alias
        if (match({TokenType::AS})) {
            consume(TokenType::IDENTIFIER, "Expected alias after 'as'.");

            auto decl = ast_.create_node<AliasedImportDecl>();
            decl->module_path = module_path;
            decl->alias = previous();
            consume(TokenType::SEMICOLON, "Expected ';' after import statement.");
            return decl;
        } else {
            diagnostic("Expected 'as' after module path. Use 'import <module> as <alias>' or 'from <module> import { ... }'", peek());
            return nullptr;
        }
    }

    diagnostic("Expected 'import' or 'from'.", peek());
    return nullptr;
}

// program -> (declaration)* EOF
Ast Parser::parse_program()
{
    ParseDepthGuard depth_guard(*this);
    lex();

    while (!is_end()) {
        size_t before = current_;
        try {
            auto decl = parse_decl(true);
            if (auto d = dynamic_cast<Declaration*>(decl)) {
                ast_.decls.push_back(d);
            } else {
                diagnostic("Expected import, function definition, type declaration, or variable declaration", peek());
                throw parser_error("Expected import, function definition, type declaration, or variable declaration");
            }
        } catch (const ast_limit_error& err) {
            diagnostic(err.what(), peek());
            break;
        } catch (const parser_error& err) {
            synchronize();
        }

        // Ensure forward progress even on malformed inputs.
        if (current_ == before) {
            if (is_end()) {
                break;
            }
            advance();
        }
    }
    return std::move(ast_);
}

FStringExpression* Parser::parse_fstring(Token fstring_token)
{
    auto expr = ast_.create_node<FStringExpression>(ctx_->arena);
    expr->fstring_token = fstring_token;

    StringView content = fstring_token.loc.view();
    size_t pos = 0;
    String current_part;

    while (pos < content.size()) {
        if (content[pos] == '{') {
            if (pos + 1 < content.size() && content[pos + 1] == '{') {
                current_part += '{';
                pos += 2;
                continue;
            }

            expr->parts.push_back(PString(current_part, ctx_->arena));
            current_part.clear();

            size_t expr_start = pos + 1;
            size_t expr_end = expr_start;
            int brace_depth = 1;

            while (expr_end < content.size() && brace_depth > 0) {
                if (content[expr_end] == '{') {
                    brace_depth++;
                } else if (content[expr_end] == '}') {
                    brace_depth--;
                }
                if (brace_depth > 0) {
                    expr_end++;
                }
            }

            if (brace_depth != 0) {
                diagnostic("Unmatched braces in f-string", fstring_token);
                throw parser_error("Unmatched braces in f-string");
            }

            StringView expr_str = content.substr(expr_start, expr_end - expr_start);

            SourcePosition expr_offset = fstring_token.loc.range.start;
            expr_offset.column += expr_start + 2;
            FStringContext fstring_ctx(ctx_, expr_offset);

            Parser sub_parser(expr_str, &fstring_ctx, parent_);
            sub_parser.lex();
            auto sub_expr = sub_parser.parse_expr();
            expr->expressions.push_back(sub_expr);

            pos = expr_end + 1;
        } else if (content[pos] == '}') {
            if (pos + 1 < content.size() && content[pos + 1] == '}') {
                current_part += '}';
                pos += 2;
                continue;
            }
            diagnostic("Unmatched closing brace in f-string", fstring_token);
            throw parser_error("Unmatched closing brace in f-string");
        } else {
            current_part += content[pos];
            pos++;
        }
    }

    expr->parts.push_back(PString(current_part, ctx_->arena));

    return expr;
}

} // namespace nw::script
