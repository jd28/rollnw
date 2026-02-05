#include "Smalls.hpp"

#include "AstHinter.hpp"
#include "AstLocator.hpp"
#include "AstResolver.hpp"
#include "runtime.hpp"

#include <new>

extern "C" {
#include <fzy/match.h>
}

#include <string_view>

namespace nw::smalls {

Script::Script(const std::filesystem::path& filename, Context* ctx)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , text_{data_.bytes.string_view()}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    data_.name = Resource::from_path(filename, true);
}

Script::Script(StringView name, StringView script, Context* ctx)
    : ctx_{ctx}
    , text_{script}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");
    CHECK_F(!name.empty(), "[script] script name cannot be empty");

    String module_name(name);
    for (char& c : module_name) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }
    data_.name = Resource(std::move(module_name), ResourceType::smalls);
}

Script::Script(ResourceData data, Context* ctx)
    : ctx_{ctx}
    , data_{std::move(data)}
    , text_{data_.bytes.string_view()}
    , ast_(ctx_)
{
    CHECK_F(!!ctx_, "[script] invalid script context");

    String module_name(data_.name.resref.view());
    for (char& c : module_name) {
        if (c == '/' || c == '\\') {
            c = '.';
        }
    }
    data_.name.resref = Resref(module_name);
}

void Script::add_diagnostic(Diagnostic diagnostic)
{
    diagnostics_.push_back(std::move(diagnostic));
}

void Script::complete(const String& needle, CompletionContext& out, bool no_filter) const
{
    for (const auto& [name, exp] : symbol_table_) {
        if (no_filter || has_match(needle.c_str(), name.c_str())) {
            if (exp.decl) { out.add(declaration_to_symbol(exp.decl)); }
        }
    }
}

void Script::complete_at(const String& needle, size_t line, size_t character, CompletionContext& out, bool no_filter)
{
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);
    auto node = locator.last_seen_decl;

    if (locator.path && locator.path->parts.size() >= 2) {
        auto* base_expr = locator.path->parts[locator.path->parts.size() - 2];
        String struct_name(nw::kernel::runtime().type_name(base_expr->type_id_));
        const StructDecl* struct_decl = nullptr;
        const Script* provider = this;
        auto exp = locator.path->env_.find(struct_name);
        if (exp && exp->decl) {
            struct_decl = dynamic_cast<const StructDecl*>(exp->decl);
        } else {
            auto symbol = locate_export(struct_name, true);
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
    } else if (locator.brace_init) {
        auto& rt = nw::kernel::runtime();
        const Type* type = rt.get_type(locator.brace_init->type_id_);
        if (type && type->type_kind == TK_struct) {
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = rt.type_table_.get(struct_id);
            if (struct_def && struct_def->decl) {
                for (auto decl : struct_def->decl->decls) {
                    if (auto vdl = dynamic_cast<const DeclList*>(decl)) {
                        for (auto vd : vdl->decls) {
                            auto sym = declaration_to_symbol(vd);
                            sym.kind = SymbolKind::field;
                            out.add(sym);
                        }
                    } else {
                        auto sym = declaration_to_symbol(decl);
                        sym.kind = SymbolKind::field;
                        out.add(sym);
                    }
                }
            }
        }
    }

    if (node) {
        Vector<const Declaration*> decls;
        node->complete(needle, decls, no_filter);
        for (auto decl : decls) {
            out.add(declaration_to_symbol(decl));
        }

        // If the last seen declaration is a selective import, it's necessary to grab the imported symbols,
        // because they otherwise wouldn't be available.
        if (auto selective_import = dynamic_cast<const SelectiveImportDecl*>(node)) {
            if (selective_import->loaded_module) {
                auto module_exports = selective_import->loaded_module->exports();
                for (const auto& symbol_token : selective_import->imported_symbols) {
                    auto symbol_name = String(symbol_token.loc.view());
                    if (no_filter || has_match(needle.c_str(), symbol_name.c_str())) {
                        auto export_ptr = module_exports.find(symbol_name);
                        if (export_ptr && export_ptr->decl) {
                            out.add(selective_import->loaded_module->declaration_to_symbol(export_ptr->decl));
                        }
                    }
                }
            }
        }

        if (node->range_.start.line < line) { // Skip if on the same line
            if (auto d = dynamic_cast<const DeclList*>(node)) {
                for (const auto decl : d->decls) {
                    if (no_filter || has_match(needle.c_str(), decl->identifier().data())) {
                        out.add(declaration_to_symbol(decl));
                    }
                }
            } else {
                if (no_filter || has_match(needle.c_str(), node->identifier().data())) {
                    out.add(declaration_to_symbol(node));
                }
            }
        }
    }
}

void Script::complete_dot(const String& needle, size_t line, size_t character, Vector<Symbol>& out,
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

    // Check if this is a module import - if so, return all module exports
    if (auto import_decl = dynamic_cast<const AliasedImportDecl*>(decl)) {
        if (import_decl->loaded_module) {
            for (const auto& [name, exp] : import_decl->loaded_module->exports()) {
                if (exp.decl) {
                    out.push_back(import_decl->loaded_module->declaration_to_symbol(exp.decl));
                }
            }
        }
        return;
    }

    // Otherwise, handle struct field completions
    if (!decl->type) { return; }
    String struct_id(decl->type->str());
    const StructDecl* struct_decl = nullptr;
    const Script* provider = this;

    auto exp2 = node->env_.find(struct_id);
    if (exp2 && exp2->decl) {
        struct_decl = dynamic_cast<const StructDecl*>(exp2->decl);
    }

    if (!struct_decl) {
        auto symbol = locate_export(struct_id, true);
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

Context* Script::ctx() const
{
    return ctx_;
}

Symbol Script::declaration_to_symbol(const Declaration* decl) const
{
    Symbol result;
    result.decl = decl;
    result.comment = ast().find_comment(decl->range_.start.line);
    result.type = nw::kernel::runtime().type_name(decl->type_id_);
    result.provider = this;
    result.view = view_from_range(decl->range_);

    if (dynamic_cast<const StructDecl*>(decl)
        || dynamic_cast<const TypeAlias*>(decl)
        || dynamic_cast<const NewtypeDecl*>(decl)) {
        result.kind = SymbolKind::type;
    } else if (auto fd = dynamic_cast<const FunctionDefinition*>(decl)) {
        result.kind = SymbolKind::function;
        result.view = view_from_range(fd->range_selection_);
    } else {
        result.kind = SymbolKind::variable;
    }

    return result;
}

Vector<String> Script::dependencies() const
{
    Vector<String> result;
    // TODO: Extract module paths from imports
    for (const auto* import : ast_.imports) {
        result.push_back(import->module_path);
    }
    return result;
}

const Vector<Diagnostic>& Script::diagnostics() const noexcept
{
    return diagnostics_;
}

Vector<InlayHint> Script::inlay_hints(SourceRange range)
{
    AstHinter hinter{this, range};
    hinter.visit(&ast_);
    return hinter.result_;
}

Symbol Script::locate_export(const String& symbol, bool search_dependencies) const
{
    auto sym = symbol_table_.find(symbol);

    Symbol result;

    if (sym && sym->decl) {
        result = declaration_to_symbol(sym->decl);
    }

    return result;
}

Symbol Script::locate_symbol(const String& symbol, size_t line, size_t character)
{
    AstLocator locator{this, symbol, line, character};
    locator.visit(&ast_);
    return locator.result_;
}

StringView Script::name() const noexcept
{
    return data_.name.resref.view();
}

void Script::parse()
{
    if (parsed_) { return; }
    try {
        Parser parser{text_, ctx_, this};
        ast_ = parser.parse_program();
    } catch (const std::bad_alloc&) {
        if (ctx_) {
            ctx_->parse_diagnostic(this, "out of memory while parsing", false, {});
        }
    }
    parsed_ = true;
}

void Script::resolve()
{
    if (resolved_) { return; }
    try {
        parse();

        AstResolver resolver{this, ctx_};
        resolver.resolve(&ast_);
        symbol_table_ = resolver.symbol_table();
    } catch (const std::bad_alloc&) {
        if (ctx_) {
            ctx_->semantic_diagnostic(this, "out of memory while resolving", false, {});
        }
    }
    resolved_ = true;
}

Ast& Script::ast() { return ast_; }
const Ast& Script::ast() const { return ast_; }

void Script::set_name(const String& new_name)
{
    data_.name = Resource(new_name, ResourceType::smalls);
}

SignatureHelp Script::signature_help(size_t line, size_t character)
{
    SignatureHelp result;
    AstLocator locator{this, "", line, character};
    locator.visit(&ast_);
    if (!locator.call) { return result; }

    if (auto path = dynamic_cast<PathExpression*>(locator.call->expr)) {
        if (path->parts.size() == 1) {
            if (auto ve = dynamic_cast<IdentifierExpression*>(path->parts.front())) {
                String name(ve->ident.loc.view());
                result.expr = locator.call;
                result.active_param = locator.active_param;
                auto exp = result.expr->env_.find(name);
                if (exp && exp->decl) {
                    result.decl = exp->decl;
                } else {
                    auto sym = locate_export(name, true);
                    result.decl = sym.decl;
                }
            }
        }
    }

    return result;
}

StringView Script::text() const noexcept { return text_; }

StringView Script::view_from_range(SourceRange range) const noexcept
{
    if (range.start.line == 0 || range.end.line == 0) {
        return {};
    }

    const auto& lm = ast().line_map;
    if (range.start.line > lm.size() || range.end.line > lm.size()) {
        return {};
    }

    size_t start = lm[range.start.line - 1] + range.start.column;
    size_t end = lm[range.end.line - 1] + range.end.column;
    if (start > text_.size() || end > text_.size() || end < start) {
        return {};
    }
    return StringView(text_.data() + start, end - start);
}

} // namespace nw::smalls
