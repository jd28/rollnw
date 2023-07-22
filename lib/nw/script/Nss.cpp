#include "Nss.hpp"

#include "../kernel/Resources.hpp"

#include <cstring>
#include <fstream>
#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename, std::shared_ptr<Context> ctx)
    : ctx_{ctx}
    , data_{ResourceData::from_file(filename)}
    , parser_{data_.bytes.string_view(), ctx_, this}
{
}

Nss::Nss(std::string_view script, std::shared_ptr<Context> ctx)
    : ctx_{ctx}
    , parser_{script, ctx_, this}
{
}

Nss::Nss(ResourceData data, std::shared_ptr<Context> ctx)
    : ctx_{ctx}
    , data_{std::move(data)}
    , parser_{data_.bytes.string_view(), ctx_, this}
{
}

std::shared_ptr<Context> Nss::ctx() const
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
    ast_ = parser_.parse_program();
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

        auto script = ctx_->get({include}, ctx_);
        if (!script) {
            throw std::runtime_error(fmt::format("[script] unable to locate include file: {}", include));
        }

        ast_.includes.push_back(script);
        script->process_includes(parent);
    }

    parent->ctx_->include_stack_.pop_back();

    return true;
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
