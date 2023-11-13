#include "Nss.hpp"

#include "AstResolver.hpp"

extern "C" {
#include <fzy/match.h>
}

#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename, Context* ctx)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , parser_{data_.bytes.string_view(), ctx_, this}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(std::string_view script, Context* ctx)
    : ctx_{ctx}
    , parser_{script, ctx_, this}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

Nss::Nss(ResourceData data, Context* ctx)
    : ctx_{ctx}
    , data_{std::move(data)}
    , parser_{data_.bytes.string_view(), ctx_, this}
{
    CHECK_F(!!ctx_, "[script] invalid script context");
}

std::vector<std::string> Nss::complete(const std::string& needle)
{
    std::vector<std::string> result;
    for (const auto& [name, _] : symbol_table_) {
        if (has_match(needle.c_str(), name.c_str())) {
            result.push_back(name);
        }
    }
    return result;
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

Declaration* Nss::locate_export(const std::string& name, bool is_type)
{
    auto sym = symbol_table_.find(name);
    if (sym) {
        return is_type ? sym->type : sym->decl;
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

bool Nss::process_includes(Nss* parent)
{
    if (!parent) { parent = this; }

    parent->ctx_->include_stack_.push_back(data_.name.resref.string());

    ast_.includes.reserve(ast_.include_resrefs.size());
    for (const auto& include : ast_.include_resrefs) {
        for (const auto& entry : parent->ctx_->include_stack_) {
            if (include == entry) {
                throw std::runtime_error(fmt::format("[script] recursive includes: {}",
                    string::join(parent->ctx_->include_stack_, ", ")));
            }
        }

        auto script = ctx_->get(Resref{include});
        if (!script) {
            throw std::runtime_error(fmt::format("[script] unable to locate include file: {}", include));
        }

        ast_.includes.push_back(script);
        script->process_includes(parent);
    }

    parent->ctx_->include_stack_.pop_back();

    return true;
}

void Nss::resolve()
{
    if (resolved_) { return; }

    for (const auto& it : ast_.includes) {
        it->resolve();
    }

    AstResolver resolver{this, ctx_};
    resolver.visit(&ast_);
    symbol_table_ = resolver.symbol_table();
    resolved_ = true;
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
