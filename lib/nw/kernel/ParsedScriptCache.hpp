#pragma once

#include "../resources/Resource.hpp"
#include "../script/Nss.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>

namespace nw {
namespace kernel {

struct ParsedScriptCache : public kernel::Service {
    ParsedScriptCache() = default;
    ParsedScriptCache(const ParsedScriptCache&) = delete;
    ParsedScriptCache(ParsedScriptCache&&) = default;
    ParsedScriptCache& operator=(const ParsedScriptCache&) = delete;
    ParsedScriptCache& operator=(ParsedScriptCache&&) = default;

    script::Script* get(Resref resref);

    virtual void clear() override;
    virtual void initialize() override;

private:
    absl::flat_hash_map<Resource, std::unique_ptr<script::Nss>> cache_;
};

inline ParsedScriptCache& parsed_scripts()
{
    auto res = detail::s_services.get_mut<ParsedScriptCache>();
    return res ? *res : *detail::s_services.add<ParsedScriptCache>();
}

} // namespace kernel
} // namespace nw
