#include "TilesetRegistry.hpp"

#include "../formats/Ini.hpp"
#include "Resources.hpp"

namespace nw::kernel {

void TilesetRegistry::clear()
{
    metrics_ = TilesetRegistryMetrics{};
    tileset_map_.clear();
}

void TilesetRegistry::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    LOG_F(INFO, "kernel: tileset registry initializing...");
    auto start = std::chrono::high_resolution_clock::now();

    auto set_getter = [this](const Resource& res) {
        if (res.type != ResourceType::set) { return; }
        load(res.resref.view());
    };

    resman().visit(set_getter, {ResourceType::set});

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    metrics_.initialization_time = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_F(INFO, "kernel: tileset registry tilesets loaded: {}", metrics_.tilesets_loaded);
    LOG_F(INFO, "kernel: tileset registry initialized ({}ms)", metrics_.initialization_time);
}

bool TilesetRegistry::load(std::string_view resref)
{
    absl::string_view sv{resref.data(), resref.size()};
    auto it = tileset_map_.find(resref);
    if (it != std::end(tileset_map_)) {
        LOG_F(ERROR, "[tilsets] set has already been loaded: {}.set", resref);
        return false;
    }

    auto rd = resman().demand({Resref{resref}, ResourceType::set});
    if (rd.bytes.size() == 0) {
        LOG_F(ERROR, "[tilsets] unable to locate set file: {}.set", resref);
        return false;
    }

    nw::Ini set{std::move(rd)};
    if (!set.valid()) {
        LOG_F(ERROR, "[tilsets] failed to parse set file: {}.set", resref);
        return false;
    }

    int32_t temp = 0;

    // Create tileset
    auto& tileset = tileset_map_[resref];

    set.get_to("general/name", tileset.strref);
    set.get_to("general/UnlocalizedName", tileset.name);

    if (!set.get_to("tiles/count", temp)) {
        LOG_F(ERROR, "[tilsets] unable to determine tile count: {}.set", resref);
        return false;
    }
    tileset.tiles.resize(size_t(temp));

    for (size_t i = 0; i < tileset.tiles.size(); ++i) {
        auto key = fmt::format("tile{}", i);
        if (!set.get_to(key + "/model", tileset.tiles[i].model)) {
            LOG_F(ERROR, "[tilsets] failed to load tile {} in {}.set", i, resref);
            return false;
        }
    }

    ++metrics_.tilesets_loaded;
    return true;
}

Tileset* TilesetRegistry::get(std::string_view resref)
{
    absl::string_view sv{resref.data(), resref.size()};
    auto it = tileset_map_.find(resref);
    if (it == std::end(tileset_map_)) {
        return nullptr;
    }

    return &it->second;
}

} // namespace nw::kernel
