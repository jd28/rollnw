#include "ScriptCache.hpp"

#include "../formats/Nss.hpp"
#include "Resources.hpp"

namespace nw::kernel {

script::Script* ScriptCache::get(Resref resref)
{
    auto res = Resource{resref, ResourceType::nss};
    auto it = cache_.find(res);
    if (it != std::end(cache_)) {
        return it->second.get();
    }

    script::Script* result = nullptr;
    auto ba = kernel::resman().demand(res);
    if (ba.size() == 0) {
        return result;
    }

    script::Nss nss{std::move(ba)};
    result = new script::Script;
    *result = nss.parse();
    cache_.emplace(res, result);

    return result;
}

void ScriptCache::clear()
{
    cache_.clear();
}

void ScriptCache::initialize()
{
    get("nwscript");
}

} // namespace nw::kernel
