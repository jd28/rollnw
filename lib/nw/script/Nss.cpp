#include "Nss.hpp"

#include "AstHinter.hpp"
#include "AstLocator.hpp"
#include "AstResolver.hpp"

extern "C" {
#include <fzy/match.h>
}

#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename, Context* ctx, bool command_script)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , text_{data_.bytes.string_view()}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(StringView script, Context* ctx, bool command_script)
    : ctx_{ctx}
    , text_{script}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(ResourceData data, Context* ctx, bool command_script)
    : ctx_{ctx}
    , data_{std::move(data)}
    , text_{data_.bytes.string_view()}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

void Nss::add_diagnostic(Diagnostic diagnostic)
{
    diagnostics_.push_back(std::move(diagnostic));
}

void Nss::complete(const String& needle, CompletionContext& out, bool no_filter) const
{
    for (const auto& [name, exp] : symbol_table_) {
        if (no_filter || has_match(needle.c_str(), name.c_str())) {
            if (exp.decl) { out.add(declaration_to_symbol(exp.decl)); }
            if (exp.type) { out.add(declaration_to_symbol(exp.type)); }
        }
    }
}

void Nss::complete_at(const String& needle, size_t line, size_t character, CompletionContext& out, bool no_filter)
{
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);
    auto node = locator.last_seen_decl;

    if (locator.dot) {
        String struct_name(ctx_->type_name(locator.dot->lhs->type_id_));
        const StructDecl* struct_decl = nullptr;
        const Nss* provider = this;
        auto exp = locator.dot->env_.find(struct_name);
        if (exp && exp->type) {
            struct_decl = exp->type;
        } else {
            auto symbol = locate_export(struct_name, true, true);
            provider = symbol.provider;
            struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
        }

        if (struct_decl) {
            for (auto decl : struct_decl->decls) {
                if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                    for (auto vd : vdl->decls) {
                        out.add(provider->declaration_to_symbol(vd));
                    }
                } else {
                    out.add(provider->declaration_to_symbol(decl));
                }
            }
        }
        return;
    }

    if (node) {
        Vector<const Declaration*> decls;
        node->complete(needle, decls);
        for (auto decl : decls) {
            out.add(declaration_to_symbol(decl));
        }

        if (node->range_.start.line < line) { // Skip if on the same line
            if (auto d = dynamic_cast<const DeclList*>(node)) {
                for (const auto decl : d->decls) {
                    out.add(declaration_to_symbol(decl));
                }
            } else {
                out.add(declaration_to_symbol(node));
            }
        }
    }

    // Find our position in the include stack.. should be first
    auto it = std::begin(ctx_->preprocessed_);
    auto end = std::begin(ctx_->preprocessed_);
    while (it != end) {
        if (it->resref == name()) { break; }
        ++it;
    }

    if (it == end) {
        LOG_F(ERROR, "[script] failed to find script '{}' in include stack", name());
        return;
    }

    ++it;
    while (it != end) {
        it->script->complete(needle, out);
    }

    if (!is_command_script_) {
        ctx_->command_script_->complete(needle, out, no_filter);
    }
}

void Nss::complete_dot(const String& needle, size_t line, size_t character, Vector<Symbol>& out,
    bool)
{
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);

    auto node = locator.last_seen_decl;
    if (!node) {
        LOG_F(INFO, "Failed to find node");
        return;
    }

    const Declaration* decl;
    if (node->identifier() == needle) {
        decl = node;
    } else {
        auto exp = node->env_.find(needle);
        if (!exp || !exp->decl) {
            LOG_F(INFO, "Failed to find needle declaration: {}", needle);
            return;
        }
        decl = exp->decl;
    }

    if (decl->type.struct_id.type == NssTokenType::INVALID) {
        LOG_F(INFO, "needle declaration doesn't have type struct");
        return;
    }

    String struct_id(decl->type.struct_id.loc.view());
    const StructDecl* struct_decl = nullptr;
    const Nss* provider = this;

    auto exp2 = node->env_.find(struct_id);
    if (exp2 && exp2->type) {
        struct_decl = dynamic_cast<const StructDecl*>(exp2->type);
    }

    if (!struct_decl) {
        auto symbol = locate_export(struct_id, true, true);
        provider = symbol.provider;
        struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
    }

    if (struct_decl) {
        for (auto decl : struct_decl->decls) {
            if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                for (auto vd : vdl->decls) {
                    out.push_back(provider->declaration_to_symbol(vd));
                }
            } else {
                out.push_back(provider->declaration_to_symbol(decl));
            }
        }
    }
}

Context* Nss::ctx() const
{
    return ctx_;
}

Symbol Nss::declaration_to_symbol(const Declaration* decl) const
{
    Symbol result;
    result.decl = decl;
    result.comment = ast().find_comment(decl->range_.start.line);
    result.type = ctx_->type_name(decl->type_id_);
    result.provider = this;
    result.view = view_from_range(decl->range_);

    if (dynamic_cast<const StructDecl*>(decl)) {
        result.kind = SymbolKind::type;
    } else if (dynamic_cast<const FunctionDecl*>(decl)) {
        result.kind = SymbolKind::function;
    } else if (auto fd = dynamic_cast<const FunctionDefinition*>(decl)) {
        result.kind = SymbolKind::function;
        result.view = view_from_range(fd->decl_inline->range_);
        // Prefer some stuff from external declaration
        if (fd->decl_external) {
            // There is no guarantee that external decl is in the same file, but
            // this is ok.. for now.
            auto comment = ast().find_comment(fd->decl_external->range_.start.line);
            if (!comment.empty()) {
                result.comment = comment;
            }
        }
    } else {
        result.kind = SymbolKind::variable;
    }

    return result;
}

Vector<String> Nss::dependencies() const
{
    Vector<String> result;
    for (const auto& [key, _] : ctx_->preprocessed_) {
        if (key == data_.name.resref.view()) { continue; }
        result.push_back(key);
    }
    return result;
}

const Vector<Diagnostic>& Nss::diagnostics() const noexcept
{
    return diagnostics_;
}

Vector<InlayHint> Nss::inlay_hints(SourceRange range)
{
    AstHinter hinter{this, range};
    hinter.visit(&ast_);
    return hinter.result_;
}

Symbol Nss::locate_export(const String& symbol, bool is_type, bool search_dependencies) const
{
    auto sym = symbol_table_.find(symbol);

    Symbol result;

    if (sym) {
        auto decl = is_type ? sym->type : sym->decl;
        if (decl) {
            result = declaration_to_symbol(is_type ? sym->type : sym->decl);
        }
    }

    if (result.decl || !search_dependencies) { return result; }

    if (!is_command_script_ && ctx_->command_script_) {
        result = ctx_->command_script_->locate_export(symbol, is_type);
    }

    if (!result.decl) {
        for (const auto& it : reverse(ctx_->preprocessed_)) {
            if (it.resref == name()) { break; }
            if (!it.script) { continue; }
            result = it.script->locate_export(symbol, is_type, false);
            if (result.decl) { return result; }
        }
    }

    return result;
}

Symbol Nss::locate_symbol(const String& symbol, size_t line, size_t character)
{
    AstLocator locator{this, symbol, line, character};
    locator.visit(&ast_);
    return locator.result_;
}

StringView Nss::name() const noexcept
{
    if (data_.name.resref.empty()) {
        return "<source>";
    } else {
        return data_.name.resref.view();
    }
}

void Nss::parse()
{
    if (parsed_) { return; }
    NssParser parser{text_, ctx_, this};
    ast_ = parser.parse_program();
    parsed_ = true;
}

void Nss::process_includes(Nss* parent)
{
    bool has_parent = !!parent;
    if (!has_parent && (includes_processed_ || is_command_script_)) { return; }
    if (!parent) { parent = this; }
    auto resref = String(name());

    // Need two lists here, one to prevent recursive includes, one to track
    // all includes.  Conceptually, at the end is 'preprocessed_' is like a
    // faux view file processed by the C preprocessor.
    parent->ctx_->include_stack_.push_back(IncludeStackEntry{resref, this});
    parent->ctx_->preprocessed_.push_back(IncludeStackEntry{resref, this});

    // Go through last include first
    for (auto& include : reverse(ast_.includes)) {
        for (const auto& entry : parent->ctx_->include_stack_) {
            if (include.resref == entry.resref) {
                ctx_->semantic_diagnostic(parent,
                    fmt::format("recursive includes: {}", resref),
                    false,
                    include.location);
                return;
            }
        }

        include.script = ctx_->get(Resref{include.resref});
        if (!include.script) {
            ctx_->semantic_diagnostic(parent,
                fmt::format("unable to locate include file: {}", include.resref),
                false,
                include.location);
        } else {
            include.script->process_includes(parent);
        }
    }

    ctx_->include_stack_.pop_back();

    if (!has_parent) {
        // We're removing all but the last occurance of an include, imagine as a flat file
        absl::flat_hash_set<String> set;
        int count = 0;
        auto test = [&set, &count](const IncludeStackEntry& entry) {
            if (set.contains(entry.resref)) {
                ++count;
                return true;
            }
            set.insert(entry.resref);
            return false;
        };

        auto it = std::remove_if(std::rbegin(ctx_->preprocessed_),
            std::rend(ctx_->preprocessed_), test);

        ctx_->preprocessed_.erase(std::begin(ctx_->preprocessed_), std::begin(ctx_->preprocessed_) + count);
    }

    includes_processed_ = true;
}

void Nss::resolve()
{
    if (resolved_) { return; }
    parse();
    process_includes();

    // Command script naturally doesn't have anything in the preprocessed stack
    if (is_command_script_) {
        AstResolver resolver{this, ctx_, is_command_script_};
        resolver.visit(&ast_);
        symbol_table_ = resolver.symbol_table();
        resolved_ = true;
    } else {
        for (const auto& it : reverse(ctx_->preprocessed_)) {
            AstResolver resolver{it.script, ctx_, is_command_script_};
            resolver.visit(&it.script->ast_);
            it.script->symbol_table_ = resolver.symbol_table();
            it.script->resolved_ = true;
        }
    }
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

void Nss::set_name(const String& new_name)
{
    data_.name = Resource(new_name, ResourceType::nss);
}

SignatureHelp Nss::signature_help(size_t line, size_t character)
{
    SignatureHelp result;
    AstLocator locator{this, "", line, character};
    locator.visit(&ast_);
    if (!locator.call) { return result; }

    if (auto ve = dynamic_cast<VariableExpression*>(locator.call->expr)) {
        String name(ve->var.loc.view());
        result.expr = locator.call;
        result.active_param = locator.active_param;
        auto exp = result.expr->env_.find(name);
        if (exp && exp->decl) {
            result.decl = exp->decl;
        } else {
            auto sym = locate_export(name, false, true);
            result.decl = sym.decl;
        }
    }

    if (result.expr) {
        LOG_F(INFO, "Found call expression");
    }

    if (result.decl) {
        LOG_F(INFO, "Found call decl");
    }

    return result;
}

StringView Nss::text() const noexcept { return text_; }

StringView Nss::view_from_range(SourceRange range) const noexcept
{
    size_t start = ast().line_map[range.start.line - 1] + range.start.column;
    size_t end = ast().line_map[range.end.line - 1] + range.end.column;
    if (start >= text_.length() || end >= text_.length()) { return ""; }
    return StringView(text_.data() + start, end - start);
}

} // namespace nw::script
