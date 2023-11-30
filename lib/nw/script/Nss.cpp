#include "Nss.hpp"

#include "AstResolver.hpp"

extern "C" {
#include <fzy/match.h>
}

#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename, Context* ctx, bool command_script)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , parser_{data_.bytes.string_view(), ctx_, this}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(std::string_view script, Context* ctx, bool command_script)
    : ctx_{ctx}
    , parser_{script, ctx_, this}
    , is_command_script_{command_script}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(ResourceData data, Context* ctx, bool command_script)
    : ctx_{ctx}
    , data_{std::move(data)}
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
    }

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

Declaration* Nss::locate_export(const std::string& name, bool is_type, bool search_dependencies)
{
    auto sym = symbol_table_.find(name);
    Declaration* decl = nullptr;
    if (sym) {
        decl = is_type ? sym->type : sym->decl;
    }

    if (decl || !search_dependencies) { return decl; }

    for (const auto it : ast_.includes) {
        if ((decl = it.script->locate_export(name, is_type, search_dependencies))) {
            return decl;
        }
    }

    if (!is_command_script_ && ctx_->command_script_) {
        return ctx_->command_script_->locate_export(name, is_type);
    }

    return nullptr;
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

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
