#include "Nss.hpp"

#include "../kernel/Resources.hpp"

#include <cstring>
#include <fstream>
#include <string_view>

namespace nw::script {

Nss* ScriptContext::get(Resref resref)
{
    Resource res{resref, ResourceType::nss};
    auto it = scripts.find(res);
    if (it != std::end(scripts)) {
        return it->second.get();
    }

    auto data = nw::kernel::resman().demand(res);
    if (data.bytes.size()) {
        auto nss = std::make_unique<Nss>(std::move(data));
        nss->parse();
        auto it = scripts.insert({res, std::move(nss)});
        return it.first->second.get();
    }

    return nullptr;
}

Nss::Nss(const std::filesystem::path& filename)
    : data_{ResourceData::from_file(filename)}
    , parser_{data_.bytes.string_view()}
{
}

Nss::Nss(std::string_view script)
    : parser_{script}
{
}

Nss::Nss(ResourceData data)
    : data_{std::move(data)}
    , parser_{data_.bytes.string_view()}
{
}

size_t Nss::errors() const noexcept
{
    return parser_.errors_;
}

size_t Nss::warnings() const noexcept
{
    return parser_.warnings_;
}

void Nss::parse()
{
    script_ = parser_.parse_program();
}

NssParser& Nss::parser() { return parser_; }
const NssParser& Nss::parser() const { return parser_; }

bool Nss::process_includes(Nss* parent)
{
    if (!parent) { parent = this; }

    parent->ctx_.include_stack.push_back(data_.name.resref.string());

    script_.includes.reserve(script_.include_resrefs.size());
    for (const auto& include : script_.include_resrefs) {
        for (const auto& entry : parent->ctx_.include_stack) {
            if (include == entry) {
                throw std::runtime_error(fmt::format("[script] recursive includes: {}",
                    string::join(parent->ctx_.include_stack, ", ")));
            }
        }

        auto script = ctx_.get({include});
        if (!script) {
            throw std::runtime_error(fmt::format("[script] unable to locate include file: {}", include));
        }

        script_.includes.push_back(script);
        script->process_includes(parent);
    }

    parent->ctx_.include_stack.pop_back();

    return true;
}

Script& Nss::script() { return script_; }
const Script& Nss::script() const { return script_; }

std::string_view Nss::text() const noexcept { return data_.bytes.string_view(); }

} // namespace nw::script
