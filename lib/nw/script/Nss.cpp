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

void Nss::add_export(std::string_view name, Declaration* decl)
{
    bool is_dupe = false;
    absl::string_view n{name.data(), name.size()};
    auto it = exports_.find(n);
    if (it == std::end(exports_)) {
        if (auto s = dynamic_cast<StructDecl*>(decl)) {
            exports_.insert({std::string(name), {nullptr, s}});
        } else {
            exports_.insert({std::string(name), {decl, nullptr}});
        }
    } else {
        if (auto s = dynamic_cast<StructDecl*>(decl)) {
            if (!it->second.type) {
                it->second.type = s;
            } else {
                is_dupe = true;
            }
        } else {
            if (!it->second.decl) {
                it->second.decl = decl;
            } else if (dynamic_cast<FunctionDecl*>(it->second.decl) && dynamic_cast<FunctionDefinition*>(decl)) {
                // Superseding function declaration with function definition seems reasonable.  If both exists
                // resolver ensures compat.
                it->second.decl = decl;
            } else {
                is_dupe = true;
            }
        }
    }

    if (is_dupe) {
        // [TODO] This will get flagged in the AstResolver as an error, if outside of that context, throw I guess..
        if (errors_ == 0) {
            throw std::runtime_error(fmt::format("duplicate export: {}", name));
        }
    }
}

std::vector<std::string> Nss::complete(const std::string& needle)
{
    std::vector<std::string> result;
    for (const auto& [name, _] : exports_) {
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

Declaration* Nss::locate_export(std::string_view name, bool is_type)
{
    absl::string_view n{name.data(), name.size()};
    auto it = exports_.find(n);
    if (it == std::end(exports_)) { return nullptr; }
    return is_type ? it->second.type : it->second.decl;
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
    resolved_ = true;
}

Ast& Nss::ast() { return ast_; }
const Ast& Nss::ast() const { return ast_; }

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
