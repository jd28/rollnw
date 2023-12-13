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

Nss::Nss(std::string_view script, Context* ctx, bool command_script)
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

void Nss::complete(const std::string& needle, CompletionContext& out) const
{
    for (const auto& [name, exp] : symbol_table_) {
        if (has_match(needle.c_str(), name.c_str())) {
            if (exp.decl) { out.add(declaration_to_symbol(exp.decl)); }
            if (exp.type) { out.add(declaration_to_symbol(exp.type)); }
        }
    }
}

inline void complete_includes(const Nss* script, const std::string& needle, CompletionContext& out)
{
    script->complete(needle, out);
    for (const auto& it : script->ast().includes) {
        if (!it.script) { continue; }
        complete_includes(it.script, needle, out);
    }
}

void Nss::complete_at(const std::string& needle, size_t line, size_t character, CompletionContext& out)
{
    AstLocator locator{this, needle, line, character};
    locator.visit(&ast_);
    auto node = locator.last_seen_decl;

    if (locator.dot) {
        std::string struct_name(ctx_->type_name(locator.dot->lhs->type_id_));
        const StructDecl* struct_decl = nullptr;
        auto exp = locator.dot->env_.find(struct_name);
        if (exp && exp->type) {
            struct_decl = exp->type;
        } else {
            auto symbol = locate_export(struct_name, true, true);
            struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
        }

        if (struct_decl) {
            for (auto decl : struct_decl->decls) {
                out.add(declaration_to_symbol(decl));
            }
        }
        return;
    }

    if (node) {
        std::vector<const Declaration*> decls;
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

    for (const auto& it : ast_.includes) {
        if (!it.script) { continue; }
        complete_includes(it.script, needle, out);
    }

    if (!is_command_script_) {
        ctx_->command_script_->complete(needle, out);
    }
}

void Nss::complete_dot(const std::string& needle, size_t line, size_t character, std::vector<Symbol>& out)
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
            LOG_F(INFO, "Failed to find needle declaration: {}", node->identifier());
            return;
        }
        decl = exp->decl;
    }

    if (decl->type.struct_id.type == NssTokenType::INVALID) {
        LOG_F(INFO, "needle declaration doesn't have type struct");
        return;
    }

    std::string struct_id(decl->type.struct_id.loc.view());
    auto exp2 = node->env_.find(struct_id);
    const StructDecl* struct_decl = nullptr;
    if (auto sd = dynamic_cast<const StructDecl*>(exp2->type)) {
        struct_decl = sd;
    } else {
        auto symbol = locate_export(struct_id, true, true);
        struct_decl = dynamic_cast<const StructDecl*>(symbol.decl);
    }

    if (struct_decl) {
        for (auto decl : struct_decl->decls) {
            out.push_back(declaration_to_symbol(decl));
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
    result.provider = name();
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

std::set<std::string> Nss::dependencies() const
{
    std::set<std::string> result;
    for (const auto& [key, _] : ctx_->dependencies_) {
        if (key == data_.name) { continue; }
        result.emplace(key.resref.view());
    }
    return result;
}

const std::vector<Diagnostic>& Nss::diagnostics() const noexcept
{
    return diagnostics_;
}

std::vector<InlayHint> Nss::inlay_hints(SourceRange range)
{
    AstHinter hinter{this, range};
    hinter.visit(&ast_);
    return hinter.result_;
}

Symbol Nss::locate_export(const std::string& symbol, bool is_type, bool search_dependencies) const
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
        for (const auto it : ast_.includes) {
            if (!it.script) { continue; }
            result = it.script->locate_export(symbol, is_type, search_dependencies);
            if (result.decl) { return result; }
        }
    }

    return result;
}

Symbol Nss::locate_symbol(const std::string& symbol, size_t line, size_t character)
{
    AstLocator locator{this, symbol, line, character};
    locator.visit(&ast_);
    return locator.result_;
}

std::string_view Nss::name() const noexcept
{
    if (data_.name.resref.empty()) {
        return "<source>";
    } else {
        return data_.name.resref.view();
    }
}

void Nss::parse()
{
    NssParser parser{text_, ctx_, this};
    ast_ = parser.parse_program();
    resolved_ = false;
}

void Nss::process_includes(Nss* parent)
{
    if (!parent) { parent = this; }

    parent->ctx_->include_stack_.push_back(data_.name.resref.string());

    for (auto& include : ast_.includes) {
        for (const auto& entry : parent->ctx_->include_stack_) {
            if (include.resref == entry) {
                ctx_->semantic_diagnostic(parent,
                    fmt::format("recursive includes: {}",
                        string::join(parent->ctx_->include_stack_, ", ")),
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

    parent->ctx_->include_stack_.pop_back();
}

void Nss::resolve()
{
    if (resolved_) { return; }

    for (const auto& it : ast_.includes) {
        if (it.script) { it.script->resolve(); }
    }

    AstResolver resolver{this, ctx_, is_command_script_};
    resolver.visit(&ast_);
    symbol_table_ = resolver.symbol_table();
    resolved_ = true;
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

void Nss::set_name(const std::string& new_name)
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
        std::string name(ve->var.loc.view());
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

std::string_view Nss::text() const noexcept { return text_; }

std::string_view Nss::view_from_range(SourceRange range) const noexcept
{
    size_t start = ast().line_map[range.start.line - 1] + range.start.column;
    size_t end = ast().line_map[range.end.line - 1] + range.end.column;
    if (start >= text_.length() || end >= text_.length()) { return ""; }
    return std::string_view(text_.data() + start, text_.data() + end);
}

} // namespace nw::script
