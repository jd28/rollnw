#include "TilesetRegistry.hpp"

#include "../formats/Ini.hpp"
#include "Resources.hpp"

namespace nw::kernel {

const std::type_index TilesetRegistry::type_index{typeid(TilesetRegistry)};

TilesetRegistry::TilesetRegistry(MemoryResource* memory)
    : Service(memory)
{
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

Tileset* TilesetRegistry::load(StringView resref)
{
    auto it = tileset_map_.find(resref);
    if (it != std::end(tileset_map_)) {
        return &it->second;
    }

    auto rd = resman().demand({Resref{resref}, ResourceType::set});
    if (rd.bytes.size() == 0) {
        LOG_F(ERROR, "[tilsets] unable to locate set file: {}.set", resref);
        return nullptr;
    }

    nw::Ini set{std::move(rd)};
    if (!set.valid()) {
        LOG_F(ERROR, "[tilsets] failed to parse set file: {}.set", resref);
        return nullptr;
    }

    int32_t temp = 0;

    // Create tileset
    auto& tileset = tileset_map_[resref];

    set.get_to("general/name", tileset.strref);
    set.get_to("general/UnlocalizedName", tileset.name);
    set.get_to("general/transition", tileset.tile_height);

    if (!set.get_to("tiles/count", temp)) {
        LOG_F(ERROR, "[tilsets] unable to determine tile count: {}.set", resref);
        return nullptr;
    }
    tileset.tiles.resize(size_t(temp));

    for (size_t i = 0; i < tileset.tiles.size(); ++i) {
        auto key = fmt::format("tile{}", i);
        if (!set.get_to(key + "/model", tileset.tiles[i].model)) {
            LOG_F(ERROR, "[tilsets] failed to load tile {} in {}.set", i, resref);
            return nullptr;
        }
    }

    ++metrics_.tilesets_loaded;
    return &tileset;
}

Tileset* TilesetRegistry::get(StringView resref)
{
    auto it = tileset_map_.find(resref);
    if (it == std::end(tileset_map_)) {
        return nullptr;
    }

    return &it->second;
}

} // namespace nw::kernel
