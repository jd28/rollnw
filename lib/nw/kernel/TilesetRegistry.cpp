#include "TilesetRegistry.hpp"

#include "../formats/Ini.hpp"
#include "../resources/ResourceManager.hpp"
#include "../util/profile.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace nw::kernel {

const std::type_index TilesetRegistry::type_index{typeid(TilesetRegistry)};

TilesetRegistry::TilesetRegistry(MemoryResource* memory)
    : Service(memory)
{
}

void TilesetRegistry::initialize(ServiceInitTime time)
{
    NW_PROFILE_SCOPE_N("tilesets.initialize");

    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    LOG_F(INFO, "kernel: tileset registry initializing...");
    auto start = std::chrono::high_resolution_clock::now();

    {
        NW_PROFILE_SCOPE_N("tilesets.initialize.visit_sets");
        auto set_getter = [this](const Resource& res) {
            if (res.type != ResourceType::set) { return; }
            NW_PROFILE_SCOPE_N("tilesets.initialize.load_set");
            auto resref = res.resref.string();
            NW_PROFILE_TEXT(resref.data(), resref.size());
            load(res.resref.view());
        };

        resman().visit(set_getter);
    }

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

    // Custom SET exporters can write unrelated memory bytes into this count.
    // A tile has a finite list of hook sections, so bound both allocation and work.
    constexpr int32_t max_door_slots_per_tile = 64;
    size_t rejected_door_count_count = 0;
    size_t rejected_door_slot_count = 0;
    for (size_t i = 0; i < tileset.tiles.size(); ++i) {
        auto key = fmt::format("tile{}", i);
        if (!set.get_to(key + "/model", tileset.tiles[i].model)) {
            LOG_F(ERROR, "[tilsets] failed to load tile {} in {}.set", i, resref);
            return nullptr;
        }
        (void)set.get_to(key + "/pathnode", tileset.tiles[i].path_node);
        (void)set.get_to(key + "/orientation",
            tileset.tiles[i].path_node_orientation);

        int32_t door_count = 0;
        if (!set.get_to(key + "/doors", door_count) || door_count == 0) {
            continue;
        }
        if (door_count < 0 || door_count > max_door_slots_per_tile) {
            ++rejected_door_count_count;
            continue;
        }
        auto& slots = tileset.tiles[i].door_slots;
        slots.reserve(static_cast<size_t>(door_count));
        for (int32_t door_index = 0; door_index < door_count; ++door_index) {
            const auto door_key = fmt::format("tile{}door{}", i, door_index);
            TileDoorSlot slot;
            if (!set.get_to(door_key + "/type", slot.type)
                || !set.get_to(door_key + "/x", slot.position.x)
                || !set.get_to(door_key + "/y", slot.position.y)
                || !set.get_to(door_key + "/z", slot.position.z)
                || !set.get_to(door_key + "/orientation", slot.orientation)
                || slot.type < 0
                || !std::isfinite(slot.position.x)
                || !std::isfinite(slot.position.y)
                || !std::isfinite(slot.position.z)
                || !std::isfinite(slot.orientation)) {
                ++rejected_door_slot_count;
                continue;
            }
            slots.push_back(slot);
        }
    }
    if (rejected_door_count_count > 0 || rejected_door_slot_count > 0) {
        LOG_F(WARNING,
            "[tilesets] ignored {} door counts outside [0, {}] and {} malformed door slots in {}.set",
            rejected_door_count_count,
            max_door_slots_per_tile,
            rejected_door_slot_count,
            resref);
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

nlohmann::json TilesetRegistry::stats() const
{
    nlohmann::json j;
    j["tileset service"] = {
        {"total_registered", tileset_map_.size()}};
    return j;
}

} // namespace nw::kernel
