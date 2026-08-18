#include "rml_smalls_expression_binding.hpp"

#include <nw/kernel/Memory.hpp>
#include <nw/smalls/Ast.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace nw::toolset {

namespace {

constexpr size_t kMaxHostSourceBytes = 16 * 1024;
constexpr size_t kMaxHostImports = 64;
constexpr size_t kMaxHostSymbols = 256;
constexpr size_t kMaxExpressionBytes = 1024;
constexpr size_t kMaxExpressionArguments = 16;
constexpr uint32_t kMaxExpressionAstNodes = 64;
constexpr uint32_t kMaxExpressionDepth = 16;

struct ParseContext final : nw::smalls::Context {
    void lexical_diagnostic(nw::smalls::Script*, nw::StringView message,
        bool warning, nw::smalls::SourceRange,
        const nw::smalls::DiagnosticInfo& = {}) override
    {
        add(message, warning);
    }

    void parse_diagnostic(nw::smalls::Script*, nw::StringView message,
        bool warning, nw::smalls::SourceRange,
        const nw::smalls::DiagnosticInfo& = {}) override
    {
        add(message, warning);
    }

    void semantic_diagnostic(nw::smalls::Script*, nw::StringView message,
        bool warning, nw::smalls::SourceRange,
        const nw::smalls::DiagnosticInfo& = {}) override
    {
        add(message, warning);
    }

    void add(nw::StringView message, bool warning)
    {
        if (!warning && error.empty()) {
            error.assign(message.data(), message.size());
        }
    }

    std::string error;
};

std::string path_name(const nw::smalls::PathExpression* path)
{
    if (!path || path->parts.empty()) {
        return {};
    }

    std::string result;
    for (size_t i = 0; i < path->parts.size(); ++i) {
        const auto* identifier = dynamic_cast<const nw::smalls::IdentifierExpression*>(
            path->parts[i]);
        if (!identifier || !identifier->type_params.empty()) {
            return {};
        }
        if (i != 0) {
            result.push_back('.');
        }
        result.append(identifier->ident.loc.view());
    }
    return result;
}

const RmlSmallsImportScope::Symbol* find_symbol(
    const RmlSmallsImportScope& scope, std::string_view name)
{
    const auto found = std::find_if(scope.symbols.begin(), scope.symbols.end(),
        [name](const auto& symbol) { return symbol.local_name == name; });
    return found == scope.symbols.end() ? nullptr : &*found;
}

const RmlSmallsImportScope::Alias* find_alias(
    const RmlSmallsImportScope& scope, std::string_view name)
{
    const auto found = std::find_if(scope.aliases.begin(), scope.aliases.end(),
        [name](const auto& alias) { return alias.local_name == name; });
    return found == scope.aliases.end() ? nullptr : &*found;
}

bool split_qualified_name(std::string_view name, std::string_view& lhs,
    std::string_view& rhs)
{
    const auto separator = name.find('.');
    if (separator == std::string_view::npos
        || separator == 0 || separator + 1 >= name.size()
        || name.find('.', separator + 1) != std::string_view::npos) {
        return false;
    }
    lhs = name.substr(0, separator);
    rhs = name.substr(separator + 1);
    return true;
}

const nw::smalls::Export* resolve_export(nw::smalls::Runtime& runtime,
    const RmlSmallsImportScope& scope, std::string_view name,
    std::string& export_name, std::string& error)
{
    std::string module_path;
    if (const auto* symbol = find_symbol(scope, name)) {
        module_path = symbol->module_path;
        export_name = symbol->export_name;
    } else {
        std::string_view alias_name;
        std::string_view qualified_export;
        if (!split_qualified_name(name, alias_name, qualified_export)) {
            error = "symbol is not present in the host import scope: " + std::string{name};
            return nullptr;
        }
        const auto* alias = find_alias(scope, alias_name);
        if (!alias) {
            error = "module alias is not present in the host import scope: "
                + std::string{alias_name};
            return nullptr;
        }
        module_path = alias->module_path;
        export_name.assign(qualified_export);
    }

    auto* provider = runtime.load_module(module_path);
    if (!provider) {
        error = "failed to load imported Smalls module '" + module_path + "'";
        return nullptr;
    }
    const auto exports = provider->exports();
    const auto exported = exports.find(export_name);
    if (!exported) {
        error = "Smalls export '" + export_name + "' was not found in module '"
            + module_path + "'";
        return nullptr;
    }
    return &*exported;
}

nw::smalls::TypeID unwrap_alias(nw::smalls::Runtime& runtime,
    nw::smalls::TypeID type_id)
{
    const auto* type = runtime.get_type(type_id);
    while (type && type->type_kind == nw::smalls::TK_alias
        && type->type_params[0].is<nw::smalls::TypeID>()) {
        const auto next = type->type_params[0].as<nw::smalls::TypeID>();
        if (next == type_id) {
            return nw::smalls::invalid_type_id;
        }
        type_id = next;
        type = runtime.get_type(type_id);
    }
    return type_id;
}

bool argument_matches(nw::smalls::Runtime& runtime,
    const nw::smalls::NormalizedTypeExpr& expected,
    const RmlSmallsBoundArgument& argument)
{
    if (expected.kind != nw::smalls::NormalizedTypeExpr::Kind::concrete
        || expected.concrete_type == nw::smalls::invalid_type_id) {
        return false;
    }
    if (expected.concrete_type == argument.type_id) {
        return true;
    }
    return unwrap_alias(runtime, expected.concrete_type) == argument.type_id;
}

bool bind_export_constant(const nw::smalls::Export& exported,
    RmlSmallsBoundArgument& output,
    std::string& error)
{
    if (exported.kind != nw::smalls::Export::Kind::variable
        || !exported.is_const
        || exported.type_id == nw::smalls::invalid_type_id) {
        error = "bound argument must be an imported primitive or newtype constant";
        return false;
    }

    output.type_id = exported.type_id;
    if (const auto* value = std::get_if<int32_t>(&exported.const_value.value)) {
        output.kind = RmlSmallsArgumentKind::integer;
        output.integer = *value;
        return true;
    }
    if (const auto* value = std::get_if<float>(&exported.const_value.value)) {
        if (!std::isfinite(*value)) {
            error = "floating-point constants must be finite";
            return false;
        }
        output.kind = RmlSmallsArgumentKind::floating;
        output.floating = *value;
        return true;
    }
    if (const auto* value = std::get_if<bool>(&exported.const_value.value)) {
        output.kind = RmlSmallsArgumentKind::boolean;
        output.boolean = *value;
        return true;
    }
    if (const auto* value = std::get_if<nw::String>(
            &exported.const_value.value)) {
        output.kind = RmlSmallsArgumentKind::string;
        output.string = *value;
        return true;
    }

    error = "imported constant has no materialized primitive value";
    return false;
}

bool bind_literal(nw::smalls::Runtime& runtime,
    const nw::smalls::LiteralExpression& literal,
    RmlSmallsBoundArgument& output, std::string& error)
{
    using nw::smalls::TokenType;
    switch (literal.literal.type) {
    case TokenType::INTEGER_LITERAL:
        if (!literal.data.is<int32_t>()) {
            error = "integer literal is outside the int32 range";
            return false;
        }
        output.kind = RmlSmallsArgumentKind::integer;
        output.type_id = runtime.int_type();
        output.integer = literal.data.as<int32_t>();
        return true;
    case TokenType::FLOAT_LITERAL:
        if (!literal.data.is<float>() || !std::isfinite(literal.data.as<float>())) {
            error = "floating-point literals must be finite";
            return false;
        }
        output.kind = RmlSmallsArgumentKind::floating;
        output.type_id = runtime.float_type();
        output.floating = literal.data.as<float>();
        return true;
    case TokenType::BOOL_LITERAL_TRUE:
    case TokenType::BOOL_LITERAL_FALSE:
        output.kind = RmlSmallsArgumentKind::boolean;
        output.type_id = runtime.bool_type();
        output.boolean = literal.data.as<bool>();
        return true;
    case TokenType::STRING_LITERAL:
    case TokenType::STRING_RAW_LITERAL: {
        output.kind = RmlSmallsArgumentKind::string;
        output.type_id = runtime.string_type();
        const auto& value = literal.data.as<nw::PString>();
        output.string.assign(value.data(), value.size());
        return true;
    }
    default:
        error = "argument is not a supported scalar literal";
        return false;
    }
}

bool bind_argument(nw::smalls::Runtime& runtime,
    const RmlSmallsImportScope& scope, const nw::smalls::Expression* expression,
    RmlSmallsBoundArgument& output, std::string& error)
{
    if (const auto* path = dynamic_cast<const nw::smalls::PathExpression*>(expression)) {
        const auto name = path_name(path);
        if (name == "event") {
            output.kind = RmlSmallsArgumentKind::event;
            output.type_id = runtime.type_id("core.rmlui.Event", false);
            return output.type_id != nw::smalls::invalid_type_id;
        }

        std::string export_name;
        const auto* exported = resolve_export(
            runtime, scope, name, export_name, error);
        return exported && bind_export_constant(*exported, output, error);
    }

    if (const auto* literal = dynamic_cast<const nw::smalls::LiteralExpression*>(expression)) {
        return bind_literal(runtime, *literal, output, error);
    }

    const auto* unary = dynamic_cast<const nw::smalls::UnaryExpression*>(expression);
    const auto* literal = unary
        ? dynamic_cast<const nw::smalls::LiteralExpression*>(unary->rhs)
        : nullptr;
    if (!unary || !literal
        || (unary->op.type != nw::smalls::TokenType::PLUS
            && unary->op.type != nw::smalls::TokenType::MINUS)
        || !bind_literal(runtime, *literal, output, error)
        || (output.kind != RmlSmallsArgumentKind::integer
            && output.kind != RmlSmallsArgumentKind::floating)) {
        if (error.empty()) {
            error = "argument must be event, a scalar literal, or an imported constant";
        }
        return false;
    }

    if (unary->op.type == nw::smalls::TokenType::MINUS) {
        if (output.kind == RmlSmallsArgumentKind::integer) {
            if (output.integer == std::numeric_limits<int32_t>::min()) {
                error = "integer literal is outside the int32 range";
                return false;
            }
            output.integer = -output.integer;
        } else {
            output.floating = -output.floating;
        }
    }
    return true;
}

bool valid_handler_return(nw::smalls::Runtime& runtime,
    const nw::smalls::CompiledFunction& function)
{
    if (function.return_type == runtime.void_type()) {
        return true;
    }
    const auto command_type = runtime.type_id("core.rmlui.Command", false);
    const auto* return_type = runtime.get_type(function.return_type);
    return return_type
        && return_type->type_kind == nw::smalls::TK_array
        && return_type->type_params[0].is<nw::smalls::TypeID>()
        && return_type->type_params[0].as<nw::smalls::TypeID>() == command_type;
}

} // namespace

bool parse_rml_smalls_import_scope(nw::smalls::Runtime& runtime,
    std::string_view source, RmlSmallsImportScope& output, std::string& error)
{
    output = {};
    error.clear();
    if (source.size() > kMaxHostSourceBytes) {
        error = "host Smalls import block exceeds the 16 KiB limit";
        return false;
    }

    MemoryScope parse_scope{nw::kernel::tls_scratch()->arena_};
    ParseContext context;
    context.arena = parse_scope.arena_;
    context.scope = &parse_scope;
    context.config.use_color = false;
    context.limits.max_ast_nodes = static_cast<uint32_t>(kMaxHostSymbols + kMaxHostImports);
    context.limits.max_parse_depth = kMaxExpressionDepth;

    nw::smalls::Parser parser{source, &context};
    auto ast = parser.parse_program();
    if (!context.error.empty()) {
        error = context.error;
        return false;
    }
    if (ast.imports.size() > kMaxHostImports) {
        error = "host Smalls block exceeds the 64-import limit";
        return false;
    }
    if (ast.decls.size() != ast.imports.size()) {
        error = "host Smalls script may contain imports only";
        return false;
    }

    size_t symbol_count = 0;
    for (const auto* import : ast.imports) {
        if (const auto* selective
            = dynamic_cast<const nw::smalls::SelectiveImportDecl*>(import)) {
            symbol_count += selective->imported_symbols.size();
            if (symbol_count > kMaxHostSymbols) {
                error = "host Smalls block exceeds the 256-symbol limit";
                return false;
            }

            auto* provider = runtime.load_module(selective->module_path);
            if (!provider) {
                error = "failed to load imported Smalls module '"
                    + selective->module_path + "'";
                return false;
            }
            const auto exports = provider->exports();
            for (const auto& token : selective->imported_symbols) {
                const std::string name{token.loc.view()};
                if (!exports.find(name)) {
                    error = "Smalls export '" + name + "' was not found in module '"
                        + selective->module_path + "'";
                    return false;
                }
                if (find_symbol(output, name) || find_alias(output, name)) {
                    error = "duplicate Smalls import name: " + name;
                    return false;
                }
                output.symbols.push_back({name, selective->module_path, name});
            }
            continue;
        }

        const auto* aliased = dynamic_cast<const nw::smalls::AliasedImportDecl*>(import);
        if (!aliased) {
            error = "unsupported Smalls import form";
            return false;
        }
        ++symbol_count;
        if (symbol_count > kMaxHostSymbols) {
            error = "host Smalls block exceeds the 256-symbol limit";
            return false;
        }
        const std::string alias{aliased->alias.loc.view()};
        if (find_symbol(output, alias) || find_alias(output, alias)) {
            error = "duplicate Smalls import name: " + alias;
            return false;
        }
        if (!runtime.load_module(aliased->module_path)) {
            error = "failed to load imported Smalls module '"
                + aliased->module_path + "'";
            return false;
        }
        output.aliases.push_back({alias, aliased->module_path});
    }
    return true;
}

bool resolve_rml_smalls_call(nw::smalls::Runtime& runtime,
    const RmlSmallsImportScope& scope, std::string_view expression,
    RmlSmallsResolvedCall& output, std::string& error)
{
    output = {};
    error.clear();
    if (expression.size() > kMaxExpressionBytes) {
        error = "Smalls handler expression exceeds the 1 KiB limit";
        return false;
    }

    MemoryScope parse_scope{nw::kernel::tls_scratch()->arena_};
    ParseContext context;
    context.arena = parse_scope.arena_;
    context.scope = &parse_scope;
    context.config.use_color = false;
    context.limits.max_ast_nodes = kMaxExpressionAstNodes;
    context.limits.max_parse_depth = kMaxExpressionDepth;

    nw::smalls::Parser parser{expression, &context};
    parser.lex();
    nw::smalls::Expression* parsed = nullptr;
    try {
        parsed = parser.parse_expr();
    } catch (const nw::smalls::ast_limit_error& exception) {
        error = exception.what();
        return false;
    } catch (const nw::smalls::parser_error& exception) {
        error = context.error.empty() ? exception.what() : context.error;
        return false;
    }
    if (!context.error.empty()) {
        error = context.error;
        return false;
    }
    if (!parser.is_end()) {
        error = "Smalls handler must be one direct call expression";
        return false;
    }

    const auto* call = dynamic_cast<const nw::smalls::CallExpression*>(parsed);
    const auto* callee = call
        ? dynamic_cast<const nw::smalls::PathExpression*>(call->expr)
        : nullptr;
    const auto callee_name = path_name(callee);
    if (!call || callee_name.empty()) {
        error = "Smalls handler must be one direct call expression";
        return false;
    }
    if (call->args.size() > kMaxExpressionArguments) {
        error = "Smalls handler exceeds the 16-argument limit";
        return false;
    }

    std::string imported_name;
    const auto* exported = resolve_export(runtime, scope, callee_name,
        imported_name, error);
    if (!exported) {
        return false;
    }
    if (exported->kind != nw::smalls::Export::Kind::function
        || !exported->function_abi) {
        error = "Smalls handler target is not an exported function: " + callee_name;
        return false;
    }

    const auto& abi = *exported->function_abi;
    if (abi.generic_arity != 0 || exported->is_generic) {
        error = "generic functions are not supported as RML handlers";
        return false;
    }
    if (call->args.size() != abi.max_arity
        || abi.param_types.size() != abi.max_arity) {
        error = "RML handler arguments must include every declared parameter";
        return false;
    }

    output.arguments.reserve(call->args.size());
    for (size_t i = 0; i < call->args.size(); ++i) {
        RmlSmallsBoundArgument argument;
        if (!bind_argument(runtime, scope, call->args[i], argument, error)) {
            error = "argument " + std::to_string(i + 1) + ": " + error;
            return false;
        }
        if (!argument_matches(runtime, abi.param_types[i], argument)) {
            error = "argument " + std::to_string(i + 1)
                + " does not match the exported function ABI";
            return false;
        }
        output.arguments.push_back(std::move(argument));
    }

    const std::string& call_target = abi.call_target;
    const auto separator = call_target.rfind('.');
    if (separator == std::string::npos || separator == 0
        || separator + 1 >= call_target.size()) {
        error = "exported function has no stable call target";
        return false;
    }
    output.module_path = call_target.substr(0, separator);
    output.function_name = call_target.substr(separator + 1);

    auto* provider = runtime.load_module(output.module_path);
    output.module = provider ? runtime.get_or_compile_module(provider) : nullptr;
    output.function = output.module
        ? output.module->get_function(output.function_name)
        : nullptr;
    if (!output.valid()) {
        error = "failed to compile Smalls handler target '" + call_target + "'";
        return false;
    }
    if (output.function->param_count != output.arguments.size()) {
        error = "compiled Smalls handler arity does not match its export ABI";
        return false;
    }
    if (!valid_handler_return(runtime, *output.function)) {
        error = "Smalls RML handler must return void or array!(core.rmlui.Command)";
        return false;
    }
    return true;
}

nw::smalls::Value materialize_rml_smalls_argument(nw::smalls::Runtime& runtime,
    const RmlSmallsBoundArgument& argument)
{
    nw::smalls::Value result;
    switch (argument.kind) {
    case RmlSmallsArgumentKind::event:
        return result;
    case RmlSmallsArgumentKind::integer:
        result = nw::smalls::Value::make_int(argument.integer);
        break;
    case RmlSmallsArgumentKind::floating:
        result = nw::smalls::Value::make_float(argument.floating);
        break;
    case RmlSmallsArgumentKind::boolean:
        result = nw::smalls::Value::make_bool(argument.boolean);
        break;
    case RmlSmallsArgumentKind::string:
        result = nw::smalls::Value::make_string(runtime.alloc_string(argument.string));
        break;
    }
    if (result.type_id != nw::smalls::invalid_type_id
        && argument.type_id != nw::smalls::invalid_type_id) {
        result.type_id = argument.type_id;
    }
    return result;
}

} // namespace nw::toolset
