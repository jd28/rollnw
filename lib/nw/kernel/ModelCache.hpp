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
    void clear() override;
    model::Mdl* load(std::string_view resref);
    void release(std::string_view resref);

    absl::flat_hash_map<std::string, ModelPayload> map_;
};

ModelCache& models();

} // namespace nw::kernel
