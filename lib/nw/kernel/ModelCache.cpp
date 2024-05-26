#include "ModelCache.hpp"

#include "../log.hpp"
#include "Kernel.hpp"
#include "Resources.hpp"

namespace nw::kernel {

void ModelCache::clear()
{
    map_.clear();
}

model::Mdl* ModelCache::load(std::string_view resref)
{
    absl::string_view sv{resref.data(), resref.size()};
    auto it = map_.find(sv);
    if (it != std::end(map_)) {
        ++it->second.refcount_;
        return it->second.original_.get();
    }

    auto data = nw::kernel::resman().demand({resref, nw::ResourceType::mdl});
    if (data.bytes.size() == 0) {
        LOG_F(ERROR, "Failed to find model: {}", resref);
        return nullptr;
    }

    auto model = std::make_unique<nw::model::Mdl>(std::move(data));
    if (!model->valid()) {
        LOG_F(ERROR, "Failed to parse model: {}", resref);
        return nullptr;
    }

    auto insert = map_.emplace(std::string(resref), ModelPayload{std::move(model), 1});
    return insert.first->second.original_.get();
}

void ModelCache::release(std::string_view resref)
{
    absl::string_view sv{resref.data(), resref.size()};
    auto it = map_.find(sv);
    if (it != std::end(map_)) {
        --it->second.refcount_;
        if (it->second.refcount_ <= 0) {
            map_.erase(it);
        }
    }
}

ModelCache& models()
{
    auto res = services().get_mut<ModelCache>();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load model service");
    }
    return *res;
}

} // namespace nw::kernel
