#include "ParsedScriptCache.hpp"

#include "Resources.hpp"

namespace nw::kernel {

script::Script* ParsedScriptCache::get(Resref resref)
{
    auto res = Resource{resref, ResourceType::nss};
    auto it = cache_.find(res);
    if (it != std::end(cache_)) {
        return &it->second.get()->script();
    }

    script::Script* result = nullptr;
    auto data = kernel::resman().demand(res);
    if (data.bytes.size() == 0) {
        return result;
    }

    auto nss = new script::Nss{std::move(data)};
    nss->parse();
    cache_.emplace(res, nss);

    return &nss->script();
}

void ParsedScriptCache::clear()
{
    cache_.clear();
}

void ParsedScriptCache::initialize()
{
    get("nwscript");
}

} // namespace nw::kernel
