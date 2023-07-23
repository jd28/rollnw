#include "Nss.hpp"

#include "../kernel/Resources.hpp"
#include "AstResolver.hpp"

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

void Nss::add_export(std::string_view name, Declaration* decl)
{
    absl::string_view n{name.data(), name.size()};
    auto it = exports_.find(n);
    if (it == std::end(exports_)) {
        exports_.insert({std::string(name), decl});
    } else {
        if (dynamic_cast<FunctionDecl*>(it->second) && dynamic_cast<FunctionDefinition*>(decl)) {
            it->second = decl;
        } else {
            // Should be impossible since the resolver will resolve dupes
            throw std::runtime_error("Duplicate export");
        }
    }
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

Declaration* Nss::locate_export(std::string_view name)
{
    absl::string_view n{name.data(), name.size()};
    auto it = exports_.find(n);
    if (it != std::end(exports_)) {
        return it->second;
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

void Nss::resolve()
{
    if (resolved_) { return; }

    for (const auto& it : ast_.includes) {
        it->resolve();
    }

    AstResolver resolver{this, ctx_};
    resolver.visit(&ast_);
    resolved_ = true;
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
