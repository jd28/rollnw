#pragma once

#include "../model/Mdl.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>
#include <string_view>

namespace nw::kernel {

struct ModelPayload {
    std::unique_ptr<nw::model::Mdl> original_;
    uint32_t refcount_ = 0;
};

struct ModelCache : public Service {
    const static std::type_index type_index;

    ModelCache(MemoryResource* scope);
    void clear();
    model::Mdl* load(StringView resref);
    void release(StringView resref);

    absl::flat_hash_map<String, ModelPayload> map_;
};

ModelCache& models();

} // namespace nw::kernel
