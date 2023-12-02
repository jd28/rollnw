#include "Nss.hpp"

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
    , parser_{data_.bytes.string_view(), ctx_, this}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(std::string_view script, Context* ctx, bool command_script)
    : ctx_{ctx}
    , text_{script}
    , parser_{script, ctx_, this}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(ResourceData data, Context* ctx, bool command_script)
    : ctx_{ctx}
    , data_{std::move(data)}
    , text_{data_.bytes.string_view()}
    , parser_{data_.bytes.string_view(), ctx_, this}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

void Nss::add_diagnostic(Diagnostic diagnostic)
{
    diagnostics_.push_back(std::move(diagnostic));
}

void Nss::complete(const std::string& needle, std::vector<std::string>& out) const
{
    for (const auto& [name, _] : symbol_table_) {
        if (has_match(needle.c_str(), name.c_str())) {
            out.push_back(name);
        }
    }
}

inline void complete_includes(const Nss* script, const std::string& needle, std::vector<std::string>& out)
{
    script->complete(needle, out);
    for (const auto& it : script->ast().includes) {
        if (!it.script) { continue; }
        complete_includes(it.script, needle, out);
    }
}

void Nss::complete_at(const std::string& needle, size_t line, size_t character, std::vector<std::string>& out) const
{
    auto node = ast_.find_last_declaration(line, character);
    if (node) {
        node->complete(needle, out);

        if (node->range_.start.line < line) { // Skip if on the same line
            std::string identifier;
            if (auto d = dynamic_cast<const VarDecl*>(node)) {
                identifier = std::string{d->identifier.loc.view()};
            } else if (auto d = dynamic_cast<const FunctionDecl*>(node)) {
                identifier = std::string{d->identifier.loc.view()};
            } else if (auto d = dynamic_cast<const FunctionDefinition*>(node)) {
                identifier = std::string{d->decl_inline->identifier.loc.view()};
            } else if (auto d = dynamic_cast<const DeclList*>(node)) {
                for (const auto decl : d->decls) {
                    std::string id{decl->identifier.loc.view()};
                    if (has_match(needle.c_str(), id.c_str())) {
                        out.push_back(identifier);
                    }
                }
            }

            if (!identifier.empty() && has_match(needle.c_str(), identifier.c_str())) {
                out.push_back(identifier);
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

Context* Nss::ctx() const
{
    return ctx_;
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

Symbol Nss::locate_export(const std::string& symbol, bool is_type, bool search_dependencies)
{
    auto sym = symbol_table_.find(symbol);

    Symbol result;

    if (sym) {
        result.decl = is_type ? sym->type : sym->decl;
        if (result.decl) {
            if (!is_type) {
                if (dynamic_cast<VarDecl*>(result.decl)) {
                    result.kind = SymbolKind::variable;
                } else if (auto fd = dynamic_cast<FunctionDefinition*>(result.decl)) {
                    if (fd->decl_external) {
                        result.decl = fd->decl_external;
                    } else {
                        result.decl = fd->decl_inline;
                    }
                    result.kind = SymbolKind::function;
                } else {
                    result.kind = SymbolKind::function;
                }
            } else {
                result.kind = SymbolKind::type;
            }
            result.type = ctx_->type_name(result.decl->type_id_);
            result.provider = name();
            result.comment = ast().find_comment(result.decl->range_.start.line);
            result.view = view_from_range(result.decl->range());
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
    if (parsed_) { return; }
    ast_ = parser_.parse_program();
    parsed_ = true;
}

NssParser& Nss::parser() { return parser_; }
const NssParser& Nss::parser() const { return parser_; }

void Nss::process_includes(Nss* parent)
{
    if (!parent) { parent = this; }

    parent->ctx_->include_stack_.push_back(data_.name.resref.string());

    ast_.includes.reserve(ast_.includes.size());
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

std::string_view Nss::text() const noexcept { return text_; }

std::string_view Nss::view_from_range(SourceRange range) const noexcept
{
    size_t start = ast().line_map[range.start.line - 1] + range.start.column;
    size_t end = ast().line_map[range.end.line - 1] + range.end.column;
    if (start >= text_.length() || end >= text_.length()) { return ""; }
    return std::string_view(text_.data() + start, text_.data() + end);
}

} // namespace nw::script
