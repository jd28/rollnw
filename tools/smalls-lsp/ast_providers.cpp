#include "ast_providers.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/WalkVisitor.hpp>
#include <nw/smalls/runtime.hpp>

#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <limits>
#include <string_view>

namespace smalls_lsp {

namespace lang = nw::smalls;

namespace {

bool valid_range(lang::SourceRange range) noexcept
{
    return range.start.line != 0 && range.end.line != 0;
}

/// True when `inner` is contained in `outer`.
bool range_contains(lang::SourceRange outer, lang::SourceRange inner) noexcept
{
    return lang::contains_position(outer, inner.start)
        && lang::contains_position(outer, inner.end);
}

/// A range is smaller when it starts later or ends earlier.
bool range_is_tighter(lang::SourceRange lhs, lang::SourceRange rhs) noexcept
{
    if (lhs.start != rhs.start) { return rhs.start < lhs.start; }
    return lhs.end < rhs.end;
}

/// Resolves the identifier a declaration's type expression names.
lang::IdentifierExpression* declared_name(lang::Declaration* decl)
{
    if (!decl || !decl->type || !decl->type->name) { return nullptr; }
    return lang::as_identifier(decl->type->name);
}

bool is_stdlib_provider(const lang::Script& script, const lang::Declaration* decl)
{
    const lang::Script* provider = script.provider_for_decl(decl);
    if (!provider) { return false; }
    return std::string_view{provider->name()}.starts_with("core.");
}

} // namespace

// == Semantic tokens =========================================================

const std::vector<std::string>& semantic_token_type_names()
{
    static const std::vector<std::string> names{
        "namespace", "type", "struct", "enum", "enumMember", "typeParameter",
        "parameter", "variable", "property", "function", "string", "number",
        "decorator"};
    return names;
}

const std::vector<std::string>& semantic_token_modifier_names()
{
    static const std::vector<std::string> names{
        "declaration", "definition", "readonly", "defaultLibrary"};
    return names;
}

namespace {

struct SemanticTokenCollector : lang::WalkVisitor {
    /// Declaring overloads below hides the rest of the base's set.
    using lang::WalkVisitor::visit;

    explicit SemanticTokenCollector(lang::Script& script)
        : script_{script}
        , text_{script.text()}
    {
        line_starts_.push_back(0);
        for (size_t i = 0; i < text_.size(); ++i) {
            if (text_[i] == '\n') { line_starts_.push_back(i + 1); }
        }
    }

    std::vector<SemanticToken> tokens;

    /// Byte length of a one-based line, excluding its line terminator.
    size_t line_length(size_t line) const
    {
        if (line == 0 || line > line_starts_.size()) { return 0; }
        size_t start = line_starts_[line - 1];
        size_t end = line < line_starts_.size() ? line_starts_[line] - 1 : text_.size();
        if (end > start && text_[end - 1] == '\r') { --end; }
        return end > start ? end - start : 0;
    }

    /// The protocol has no multi-line token, so a range that spans lines is
    /// split into one token per line. `r"..."` reaches this: the lexer scans a
    /// raw string with `no_eol` false, so it may cross a line boundary.
    void add(lang::SourceRange range, TokenType type, int modifiers)
    {
        if (!valid_range(range)) { return; }

        if (range.start.line == range.end.line) {
            push_token(range.start.line, range.start.column, range.end.column, type, modifiers);
            return;
        }
        if (range.end.line < range.start.line) { return; }

        for (size_t line = range.start.line; line <= range.end.line; ++line) {
            size_t start_column = line == range.start.line ? range.start.column : 0;
            size_t end_column = line == range.end.line ? range.end.column : line_length(line);
            push_token(line, start_column, end_column, type, modifiers);
        }
    }

    void push_token(size_t line, size_t start_column, size_t end_column, TokenType type,
        int modifiers)
    {
        constexpr auto int_max = static_cast<size_t>(std::numeric_limits<int>::max());
        if (line == 0 || end_column <= start_column || line - 1 > int_max
            || start_column > int_max || end_column - start_column > int_max) {
            return;
        }

        tokens.push_back({static_cast<int>(line - 1), static_cast<int>(start_column),
            static_cast<int>(end_column - start_column), static_cast<int>(type), modifiers});
    }

    int decl_modifiers(const lang::Declaration* decl, int base) const
    {
        int result = base;
        if (decl && decl->is_const_) { result |= modifier_readonly; }
        if (decl && is_stdlib_provider(script_, decl)) {
            result |= modifier_default_library;
        }
        return result;
    }

    /// Emits the name a type declaration introduces and suppresses the
    /// identifier node so the type-expression walk does not emit it again.
    void add_declared_name(lang::Declaration* decl, TokenType type)
    {
        auto* name = declared_name(decl);
        if (!name) { return; }
        add(name->range_, type, decl_modifiers(decl, modifier_declaration | modifier_definition));
        suppressed_.insert(name);
    }

    void accept_annotations(lang::Declaration* decl) override
    {
        for (auto& annotation : decl->annotations_) {
            add(annotation.name.loc.range, TokenType::decorator, modifier_none);
            for (auto& arg : annotation.args) {
                accept_node(arg.value);
            }
        }
    }

    // -- Declarations --------------------------------------------------------

    void visit(lang::FunctionDefinition* decl) override
    {
        add(decl->identifier_.loc.range, TokenType::function,
            decl_modifiers(decl, modifier_declaration | modifier_definition));
        for (auto* param : decl->params) {
            if (param) { parameters_.insert(param); }
        }
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::LambdaExpression* expr) override
    {
        for (auto* param : expr->params) {
            if (param) { parameters_.insert(param); }
        }
        lang::WalkVisitor::visit(expr);
    }

    void visit(lang::StructDecl* decl) override
    {
        add_declared_name(decl, TokenType::struct_);
        for (auto* member : decl->decls) {
            if (auto* var = dynamic_cast<lang::VarDecl*>(member)) {
                fields_.insert(var);
            } else if (auto* list = dynamic_cast<lang::DeclList*>(member)) {
                for (auto* var_in_list : list->decls) {
                    if (var_in_list) { fields_.insert(var_in_list); }
                }
            }
        }
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::SumDecl* decl) override
    {
        add_declared_name(decl, TokenType::enum_);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::TypeAlias* decl) override
    {
        add_declared_name(decl, TokenType::type);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::NewtypeDecl* decl) override
    {
        add_declared_name(decl, TokenType::type);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::OpaqueTypeDecl* decl) override
    {
        add_declared_name(decl, TokenType::type);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::VariantDecl* decl) override
    {
        add(decl->identifier_.loc.range, TokenType::enum_member,
            modifier_declaration | modifier_definition);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::VarDecl* decl) override
    {
        TokenType type = TokenType::variable;
        if (parameters_.contains(decl)) {
            type = TokenType::parameter;
        } else if (fields_.contains(decl)) {
            type = TokenType::property;
        }
        add(decl->identifier_.loc.range, type, decl_modifiers(decl, modifier_declaration));
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::AliasedImportDecl* decl) override
    {
        add(decl->alias.loc.range, TokenType::namespace_, modifier_declaration);
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::SelectiveImportDecl* decl) override
    {
        for (const auto& symbol : decl->imported_symbols) {
            add(symbol.loc.range, TokenType::type, modifier_none);
        }
        lang::WalkVisitor::visit(decl);
    }

    // -- Expressions ---------------------------------------------------------

    void visit(lang::LabelStatement* stmt) override
    {
        // `case Circle(radius):` names a variant, but VariantDecl is not a
        // Declaration, so the identifier carries no `decl` to classify from.
        if (stmt->is_pattern_match && stmt->expr) {
            lang::Expression* head = stmt->expr;
            if (auto* call = dynamic_cast<lang::CallExpression*>(head)) {
                head = call->expr;
            }
            if (auto* identifier = lang::as_identifier(head)) {
                variant_patterns_.insert(identifier);
            }
        }
        lang::WalkVisitor::visit(stmt);
    }

    void visit(lang::TypeExpression* expr) override
    {
        ++type_depth_;
        lang::WalkVisitor::visit(expr);
        --type_depth_;
    }

    void visit(lang::IdentifierExpression* expr) override
    {
        if (!suppressed_.contains(expr)) {
            add(expr->range_, classify(expr), decl_modifiers(expr->decl, modifier_none));
        }
        // Type arguments are always types regardless of the enclosing context.
        ++type_depth_;
        accept_all(expr->type_params);
        --type_depth_;
    }

    void visit(lang::LiteralExpression* expr) override
    {
        switch (expr->literal.type) {
        case lang::TokenType::STRING_LITERAL:
        case lang::TokenType::STRING_RAW_LITERAL:
        case lang::TokenType::FSTRING_LITERAL:
            add(expr->range_, TokenType::string, modifier_none);
            break;
        case lang::TokenType::INTEGER_LITERAL:
        case lang::TokenType::FLOAT_LITERAL:
            add(expr->range_, TokenType::number, modifier_none);
            break;
        default:
            break;
        }
    }

private:
    TokenType classify(const lang::IdentifierExpression* expr) const
    {
        if (variant_patterns_.contains(expr)) { return TokenType::enum_member; }
        if (const auto* decl = expr->decl) {
            if (dynamic_cast<const lang::FunctionDefinition*>(decl)) {
                return TokenType::function;
            }
            if (dynamic_cast<const lang::StructDecl*>(decl)) {
                return TokenType::struct_;
            }
            if (dynamic_cast<const lang::SumDecl*>(decl)) {
                return TokenType::enum_;
            }
            if (dynamic_cast<const lang::TypeAlias*>(decl)
                || dynamic_cast<const lang::NewtypeDecl*>(decl)
                || dynamic_cast<const lang::OpaqueTypeDecl*>(decl)) {
                return TokenType::type;
            }
            if (dynamic_cast<const lang::AliasedImportDecl*>(decl)) {
                return TokenType::namespace_;
            }
            if (const auto* var = dynamic_cast<const lang::VarDecl*>(decl)) {
                if (parameters_.contains(var)) { return TokenType::parameter; }
                if (fields_.contains(var)) { return TokenType::property; }
            }
            return type_depth_ > 0 ? TokenType::type : TokenType::variable;
        }

        if (type_depth_ > 0) {
            // Unresolved inside a type position: a builtin or a generic parameter.
            return expr->ident.loc.view().starts_with("$")
                ? TokenType::type_parameter
                : TokenType::type;
        }
        return TokenType::variable;
    }

    lang::Script& script_;
    std::string_view text_;
    std::vector<size_t> line_starts_;
    absl::flat_hash_set<const lang::VarDecl*> parameters_;
    absl::flat_hash_set<const lang::VarDecl*> fields_;
    absl::flat_hash_set<const lang::AstNode*> suppressed_;
    absl::flat_hash_set<const lang::AstNode*> variant_patterns_;
    int type_depth_ = 0;
};

} // namespace

std::vector<SemanticToken> collect_semantic_tokens(lang::Script& script)
{
    SemanticTokenCollector collector{script};
    for (auto* decl : script.ast().decls) {
        if (decl) { decl->accept(&collector); }
    }

    auto& tokens = collector.tokens;
    std::sort(tokens.begin(), tokens.end(), [](const SemanticToken& a, const SemanticToken& b) {
        if (a.line != b.line) { return a.line < b.line; }
        if (a.start != b.start) { return a.start < b.start; }
        // Prefer the tighter token when two share a start.
        return a.length < b.length;
    });

    // The protocol requires non-overlapping tokens. Keep the first of any
    // overlapping run, which sorting has made the tightest at that start.
    std::vector<SemanticToken> result;
    result.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (!result.empty()) {
            const auto& previous = result.back();
            if (previous.line == token.line && token.start < previous.start + previous.length) {
                continue;
            }
        }
        result.push_back(token);
    }
    return result;
}

// == Document symbols ========================================================

namespace {

std::string type_name_of(nw::smalls::TypeID type_id)
{
    if (type_id == lang::invalid_type_id) { return {}; }
    return std::string{nw::kernel::runtime().type_name(type_id)};
}

std::string function_detail(const lang::FunctionDefinition* decl)
{
    std::string detail = "(";
    bool first = true;
    for (const auto* param : decl->params) {
        if (!param) { continue; }
        if (!first) { detail += ", "; }
        first = false;
        detail += std::string{param->identifier_.loc.view()};
        auto type = type_name_of(param->type_id_);
        if (!type.empty()) { detail += ": " + type; }
    }
    detail += ")";
    if (decl->return_type) {
        auto type = type_name_of(decl->type_id_);
        if (!type.empty()) { detail += " -> " + type; }
    }
    return detail;
}

/// Falls back to the declaration range when a selection range is unusable.
lang::SourceRange usable_selection(lang::SourceRange selection, lang::SourceRange range)
{
    if (!valid_range(selection) || !range_contains(range, selection)) { return range; }
    return selection;
}

void append_variable_symbol(std::vector<DocumentSymbol>& out, lang::VarDecl* decl,
    bool is_field)
{
    auto range = decl->range();
    if (!valid_range(range)) { return; }

    SymbolKind kind = SymbolKind::variable;
    if (is_field) {
        kind = SymbolKind::field;
    } else if (decl->is_const_) {
        kind = SymbolKind::constant;
    }

    out.push_back({decl->identifier(), type_name_of(decl->type_id_), kind, range,
        usable_selection(decl->identifier_.loc.range, range), {}});
}

void append_symbol(std::vector<DocumentSymbol>& out, lang::Declaration* decl);

void append_members(std::vector<DocumentSymbol>& out, lang::StructDecl* decl)
{
    for (auto* member : decl->decls) {
        if (auto* var = dynamic_cast<lang::VarDecl*>(member)) {
            append_variable_symbol(out, var, true);
        } else if (auto* list = dynamic_cast<lang::DeclList*>(member)) {
            for (auto* var_in_list : list->decls) {
                if (var_in_list) { append_variable_symbol(out, var_in_list, true); }
            }
        } else if (member) {
            append_symbol(out, member);
        }
    }
}

void append_symbol(std::vector<DocumentSymbol>& out, lang::Declaration* decl)
{
    auto range = decl->range();
    if (!valid_range(range)) { return; }

    auto declared = declared_name(decl);
    auto type_selection = declared ? declared->range_ : range;

    if (auto* function = dynamic_cast<lang::FunctionDefinition*>(decl)) {
        out.push_back({function->identifier(), function_detail(function),
            SymbolKind::function, range,
            usable_selection(function->identifier_.loc.range, range), {}});
    } else if (auto* structure = dynamic_cast<lang::StructDecl*>(decl)) {
        DocumentSymbol symbol{structure->identifier(), {}, SymbolKind::struct_, range,
            usable_selection(type_selection, range), {}};
        append_members(symbol.children, structure);
        out.push_back(std::move(symbol));
    } else if (auto* sum = dynamic_cast<lang::SumDecl*>(decl)) {
        DocumentSymbol symbol{sum->identifier(), {}, SymbolKind::enum_, range,
            usable_selection(type_selection, range), {}};
        for (auto* variant : sum->variants) {
            if (!variant || !valid_range(variant->range_)) { continue; }
            symbol.children.push_back({variant->identifier(), {},
                SymbolKind::enum_member, variant->range_,
                usable_selection(variant->identifier_.loc.range, variant->range_), {}});
        }
        out.push_back(std::move(symbol));
    } else if (auto* alias = dynamic_cast<lang::TypeAlias*>(decl)) {
        out.push_back({alias->identifier(), alias->aliased_type ? alias->aliased_type->str() : "",
            SymbolKind::class_, range, usable_selection(type_selection, range), {}});
    } else if (auto* newtype = dynamic_cast<lang::NewtypeDecl*>(decl)) {
        out.push_back({newtype->identifier(),
            newtype->wrapped_type ? newtype->wrapped_type->str() : "",
            SymbolKind::struct_, range, usable_selection(type_selection, range), {}});
    } else if (auto* opaque = dynamic_cast<lang::OpaqueTypeDecl*>(decl)) {
        out.push_back({opaque->identifier(), {}, SymbolKind::class_, range,
            usable_selection(type_selection, range), {}});
    } else if (auto* import = dynamic_cast<lang::AliasedImportDecl*>(decl)) {
        out.push_back({import->identifier(), import->module_path, SymbolKind::module,
            range, usable_selection(import->alias.loc.range, range), {}});
    } else if (auto* list = dynamic_cast<lang::DeclList*>(decl)) {
        for (auto* var : list->decls) {
            if (var) { append_variable_symbol(out, var, false); }
        }
    } else if (auto* var = dynamic_cast<lang::VarDecl*>(decl)) {
        append_variable_symbol(out, var, false);
    }
    // SelectiveImportDecl introduces no single named symbol.
}

} // namespace

std::vector<DocumentSymbol> collect_document_symbols(lang::Script& script)
{
    std::vector<DocumentSymbol> result;
    for (auto* statement : script.ast().decls) {
        if (auto* decl = dynamic_cast<lang::Declaration*>(statement)) {
            append_symbol(result, decl);
        }
    }
    return result;
}

// == Folding ranges ==========================================================

namespace {

struct FoldingRangeCollector : lang::WalkVisitor {
    /// Declaring overloads below hides the rest of the base's set.
    using lang::WalkVisitor::visit;

    std::vector<FoldingRange> ranges;

    /// Folds a brace-delimited region, leaving the closing line visible.
    void add_block(lang::SourceRange range)
    {
        if (!valid_range(range) || range.end.line < 2) { return; }
        add_lines(static_cast<int>(range.start.line) - 1,
            static_cast<int>(range.end.line) - 2, FoldingKind::none);
    }

    void add_lines(int start_line, int end_line, FoldingKind kind)
    {
        if (start_line < 0 || end_line <= start_line) { return; }
        ranges.push_back({start_line, end_line, kind});
    }

    void visit(lang::BlockStatement* stmt) override
    {
        add_block(stmt->range_);
        lang::WalkVisitor::visit(stmt);
    }

    void visit(lang::StructDecl* decl) override
    {
        add_block(decl->range());
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::SumDecl* decl) override
    {
        add_block(decl->range());
        lang::WalkVisitor::visit(decl);
    }

    void visit(lang::BraceInitLiteral* expr) override
    {
        add_block(expr->range_);
        lang::WalkVisitor::visit(expr);
    }
};

} // namespace

std::vector<FoldingRange> collect_folding_ranges(lang::Script& script)
{
    FoldingRangeCollector collector;
    auto& ast = script.ast();
    for (auto* decl : ast.decls) {
        if (decl) { decl->accept(&collector); }
    }

    for (const auto& comment : ast.comments) {
        auto range = comment.range_.range;
        if (!valid_range(range)) { continue; }
        collector.add_lines(static_cast<int>(range.start.line) - 1,
            static_cast<int>(range.end.line) - 1, FoldingKind::comment);
    }

    // The leading import run folds as one region.
    size_t first_import_line = 0;
    size_t last_import_line = 0;
    for (const auto* import : ast.imports) {
        auto range = import->range();
        if (!valid_range(range)) { continue; }
        if (first_import_line == 0 || range.start.line < first_import_line) {
            first_import_line = range.start.line;
        }
        last_import_line = std::max(last_import_line, range.end.line);
    }
    if (first_import_line != 0) {
        collector.add_lines(static_cast<int>(first_import_line) - 1,
            static_cast<int>(last_import_line) - 1, FoldingKind::imports);
    }

    auto& ranges = collector.ranges;
    std::sort(ranges.begin(), ranges.end(), [](const FoldingRange& a, const FoldingRange& b) {
        if (a.start_line != b.start_line) { return a.start_line < b.start_line; }
        if (a.end_line != b.end_line) { return a.end_line > b.end_line; }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    ranges.erase(std::unique(ranges.begin(), ranges.end()), ranges.end());
    return ranges;
}

// == Unused imports ==========================================================

namespace {

/// Collects every name an expression position refers to.
struct ReferenceCollector : lang::WalkVisitor {
    /// Declaring overloads below hides the rest of the base's set.
    using lang::WalkVisitor::visit;

    absl::flat_hash_set<std::string> names;
    absl::flat_hash_set<const lang::Declaration*> decls;

    void visit(lang::IdentifierExpression* expr) override
    {
        names.insert(std::string{expr->ident.loc.view()});
        if (expr->decl) { decls.insert(expr->decl); }
        lang::WalkVisitor::visit(expr);
    }
};

} // namespace

std::vector<UnusedImport> collect_unused_imports(lang::Script& script)
{
    std::vector<UnusedImport> result;

    for (const auto& diagnostic : script.diagnostics()) {
        if (diagnostic.severity == lang::DiagnosticSeverity::error) {
            return result;
        }
    }

    ReferenceCollector references;
    for (auto* decl : script.ast().decls) {
        if (decl) { decl->accept(&references); }
    }

    auto is_used = [&](const lang::Declaration* decl, std::string_view name) {
        return references.decls.contains(decl) || references.names.contains(std::string{name});
    };

    for (auto* import : script.ast().imports) {
        if (!import || !valid_range(import->range())) { continue; }

        if (auto* aliased = dynamic_cast<lang::AliasedImportDecl*>(import)) {
            auto name = aliased->alias.loc.view();
            if (!name.empty() && !is_used(aliased, name)) {
                result.push_back({aliased->alias.loc.range, aliased->range(), std::string{name}});
            }
        } else if (auto* selective = dynamic_cast<lang::SelectiveImportDecl*>(import)) {
            // Report the declaration only when no imported symbol is used;
            // removing one name from a list is a different edit.
            bool any_used = false;
            for (const auto& symbol : selective->imported_symbols) {
                if (is_used(selective, symbol.loc.view())) {
                    any_used = true;
                    break;
                }
            }
            if (!any_used && !selective->imported_symbols.empty()) {
                result.push_back({selective->imported_symbols.front().loc.range,
                    selective->range(), selective->module_path});
            }
        }
    }

    return result;
}

// == Struct initializer completeness =========================================

namespace {

/// A literal of `type_name` suitable as a placeholder value.
std::string default_value_for(std::string_view type_name)
{
    if (type_name == "int") { return "0"; }
    if (type_name == "float") { return "0.0"; }
    if (type_name == "bool") { return "false"; }
    if (type_name == "string") { return "\"\""; }
    return "{}";
}

/// Appends a struct's declared fields in declaration order.
void collect_fields(const lang::StructDecl* decl, std::vector<const lang::VarDecl*>& out)
{
    for (auto* member : decl->decls) {
        if (auto* var = dynamic_cast<lang::VarDecl*>(member)) {
            out.push_back(var);
        } else if (auto* list = dynamic_cast<lang::DeclList*>(member)) {
            for (auto* var_in_list : list->decls) {
                if (var_in_list) { out.push_back(var_in_list); }
            }
        }
    }
}

struct IncompleteLiteralCollector : lang::WalkVisitor {
    /// Declaring overloads below hides the rest of the base's set.
    using lang::WalkVisitor::visit;

    explicit IncompleteLiteralCollector(lang::Script& script)
        : script_{script}
    {
    }

    std::vector<IncompleteStructLiteral> results;

    void visit(lang::BraceInitLiteral* expr) override
    {
        lang::WalkVisitor::visit(expr);

        // The init kind is inferred from the first item, so an empty literal is
        // unclassified. Accept it and let the type name decide: if it names a
        // struct, `Type{}` is a field initializer missing every field, which is
        // exactly when this fix is most useful.
        bool field_init = expr->init_type == lang::BraceInitType::field
            || (expr->init_type == lang::BraceInitType::none && expr->items.empty());
        if (!field_init || !valid_range(expr->range_)) {
            return;
        }

        auto type_name = expr->type.loc.view();
        if (type_name.empty()) { return; }

        auto symbol = script_.locate_export(std::string{type_name}, true);
        const auto* structure = dynamic_cast<const lang::StructDecl*>(symbol.decl);
        if (!structure) { return; }

        std::vector<const lang::VarDecl*> fields;
        collect_fields(structure, fields);
        if (fields.empty()) { return; }

        absl::flat_hash_set<std::string> present;
        for (const auto& item : expr->items) {
            if (auto* key = lang::as_identifier(item.key)) {
                present.insert(std::string{key->ident.loc.view()});
            }
        }

        IncompleteStructLiteral result;
        for (const auto* field : fields) {
            std::string name = field->identifier();
            if (present.contains(name)) { continue; }
            result.missing.push_back(
                {name, default_value_for(type_name_of(field->type_id_))});
        }
        if (result.missing.empty()) { return; }

        result.range = expr->range_;
        result.type_name = std::string{type_name};
        result.has_existing_fields = !present.empty();

        // Anchor after the last field rather than before the closing brace, so
        // the separator lands next to the value instead of after the whitespace
        // in front of `}`.
        lang::SourcePosition last_field_end{};
        for (const auto& item : expr->items) {
            const lang::Expression* value = item.value ? item.value : item.key;
            if (value && valid_range(value->range_) && last_field_end < value->range_.end) {
                last_field_end = value->range_.end;
            }
        }

        if (last_field_end.line != 0) {
            result.insert_at = last_field_end;
        } else {
            // range_.end sits just past the closing brace, so back up over it.
            result.insert_at = expr->range_.end;
            if (result.insert_at.column > 0) { --result.insert_at.column; }
        }
        results.push_back(std::move(result));
    }

private:
    lang::Script& script_;
};

} // namespace

std::vector<IncompleteStructLiteral> collect_incomplete_struct_literals(lang::Script& script)
{
    for (const auto& diagnostic : script.diagnostics()) {
        if (diagnostic.severity == lang::DiagnosticSeverity::error) {
            return {};
        }
    }

    IncompleteLiteralCollector collector{script};
    for (auto* decl : script.ast().decls) {
        if (decl) { decl->accept(&collector); }
    }
    return std::move(collector.results);
}

// == Imports =================================================================

int import_insert_line(lang::Script& script, std::string_view module_path)
{
    int insert_line = 0;
    for (const auto* import : script.ast().imports) {
        if (!import || !valid_range(import->range())) { continue; }
        int line = static_cast<int>(import->range().start.line) - 1;
        // Keep the block sorted: stop at the first import that sorts after the
        // new one, otherwise land after the last.
        if (import->module_path < module_path) {
            insert_line = std::max(insert_line, line + 1);
        } else if (insert_line == 0 || line < insert_line) {
            return line;
        }
    }
    return insert_line;
}

// == Selection ranges ========================================================

namespace {

struct SelectionRangeCollector : lang::WalkVisitor {
    /// Declaring overloads below hides the rest of the base's set.
    using lang::WalkVisitor::visit;

    explicit SelectionRangeCollector(lang::SourcePosition position)
        : position_{position}
    {
    }

    std::vector<lang::SourceRange> ranges;

    void enter(lang::AstNode* node) override
    {
        if (!valid_range(node->range_)) { return; }
        if (lang::contains_position(node->range_, position_)) {
            ranges.push_back(node->range_);
        }
    }

private:
    lang::SourcePosition position_;
};

} // namespace

std::vector<lang::SourceRange> collect_selection_range_chain(
    lang::Script& script, lang::SourcePosition position)
{
    SelectionRangeCollector collector{position};
    for (auto* decl : script.ast().decls) {
        if (decl) { decl->accept(&collector); }
    }

    auto& candidates = collector.ranges;
    std::sort(candidates.begin(), candidates.end(),
        [](lang::SourceRange a, lang::SourceRange b) { return range_is_tighter(a, b); });

    // Keep a strictly increasing chain: each kept range properly contains the
    // one before it. Equal spans collapse to one entry.
    std::vector<lang::SourceRange> result;
    for (auto range : candidates) {
        if (result.empty()) {
            result.push_back(range);
            continue;
        }
        const auto& previous = result.back();
        if (previous.start == range.start && previous.end == range.end) { continue; }
        if (range_contains(range, previous)) { result.push_back(range); }
    }
    return result;
}

} // namespace smalls_lsp
