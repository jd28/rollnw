#pragma once

#include "../formats/Tileset.hpp"
#include "../log.hpp"
#include "Kernel.hpp"

#include <absl/container/node_hash_map.h>

#include <limits>
#include <stdint.h>
#include <string>
#include <vector>

namespace nw::kernel {

struct TilesetRegistryMetrics {
    size_t tilesets_loaded = 0;
    int64_t initialization_time = 0;
};

struct TilesetRegistry : public Service {
    void clear() override;
    void initialize(ServiceInitTime time) override;
    Tileset* load(std::string_view resref);
    Tileset* get(std::string_view resref);

    absl::node_hash_map<std::string, Tileset> tileset_map_;
    TilesetRegistryMetrics metrics_;
};

inline TilesetRegistry& tilesets()
{
    auto res = services().get_mut<TilesetRegistry>();
    if (!res) {
        throw std::runtime_error("kernel: unable to tileset service");
    }
    return *res;
}

} // namespace nw
