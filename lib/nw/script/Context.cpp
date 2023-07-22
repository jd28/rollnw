#include "Context.hpp"

#include "../kernel/Resources.hpp"
#include "Nss.hpp"

namespace nw::script {

Context::Context()
{
    register_default_types();
}

Nss* Context::get(Resref resref, std::shared_ptr<Context> ctx)
{
    Resource res{resref, ResourceType::nss};
    auto it = dependencies_.find(res);
    if (it != std::end(dependencies_)) {
        return it->second.get();
    }

    auto data = nw::kernel::resman().demand(res);
    if (data.bytes.size()) {
        auto nss = std::make_unique<Nss>(std::move(data), ctx);
        nss->parse();
        auto it = dependencies_.insert({res, std::move(nss)});
        return it.first->second.get();
    }

    return nullptr;
}

void Context::register_default_types()
{
    type_id("action");
    type_id("cassowary");
    type_id("effect");
    type_id("event");
    type_id("float");
    type_id("int");
    type_id("itemproperty");
    type_id("json");
    type_id("location");
    type_id("object");
    type_id("sqlquery");
    type_id("string");
    type_id("talent");
    type_id("vector");
    type_id("void");
}

size_t Context::type_id(std::string_view type_name)
{
    absl::string_view tn{type_name.data(), type_name.size()};
    auto it = type_map_.find(tn);
    if (it == std::end(type_map_)) {
        size_t new_id = type_array_.size();
        type_array_.emplace_back(type_name);
        type_map_.emplace(type_name, new_id);
        return new_id;
    } else {
        return it->second;
    }
}

size_t Context::type_id(Type type_name)
{
    if (type_name.type_specifier.type == NssTokenType::STRUCT) {
        return type_id(type_name.struct_id.loc.view);
    } else {
        return type_id(type_name.type_specifier.loc.view);
    }
}

std::string_view Context::type_name(size_t type_id)
{
    if (type_id >= type_array_.size()) { return "<unknown>"; }
    return type_array_[type_id];
}

void Context::lexical_error(Nss* script, std::string_view msg, SourceLocation loc)
{
    if (script) { script->increment_errors(); }
    auto out = fmt::format("{}:{}:{} error: {}", script ? script->name() : "<source>",
        loc.line, loc.column, msg);
    LOG_F(ERROR, "{}", out);
    throw std::runtime_error(out);
}

void Context::lexical_warning(Nss* script, std::string_view msg, SourceLocation loc)
{
    if (script) { script->increment_warnings(); }
    auto out = fmt::format("{}:{}:{} warning: {}", script ? script->name() : "<source>",
        loc.line, loc.column, msg);
    LOG_F(WARNING, "{}", out);
}

void Context::parse_error(Nss* script, std::string_view msg, NssToken token)
{
    if (script) { script->increment_errors(); }
    auto out = fmt::format("{}:{}:{} error: {}", script ? script->name() : "<source>",
        token.loc.line, token.loc.column, msg);
    LOG_F(ERROR, "{}", out);
    throw parser_error(out);
}

void Context::parse_warning(Nss* script, std::string_view msg, NssToken token)
{
    if (script) { script->increment_warnings(); }
    auto out = fmt::format("{}:{}:{} warning: {}", script ? script->name() : "<source>",
        token.loc.line, token.loc.column, msg);
    LOG_F(WARNING, "{}", out);
}

void Context::semantic_error(Nss* script, std::string_view msg, NssToken token)
{
    if (script) { script->increment_errors(); }
    auto out = fmt::format("{}:{}:{} error: {}", script ? script->name() : "<source>",
        token.loc.line, token.loc.column, msg);
    LOG_F(ERROR, "{}", out);
}

void Context::semantic_warning(Nss* script, std::string_view msg, NssToken token)
{
    if (script) { script->increment_warnings(); }
    auto out = fmt::format("{}:{}:{} warning: {}", script ? script->name() : "<source>",
        token.loc.line, token.loc.column, msg);
    LOG_F(WARNING, "{}", out);
}

} // nw::script
