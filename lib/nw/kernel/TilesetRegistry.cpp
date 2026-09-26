#include "TilesetRegistry.hpp"

#include "../formats/Ini.hpp"
#include "../resources/ResourceManager.hpp"
#include "../util/profile.hpp"
#include "../util/string.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace nw::kernel {
namespace {

constexpr int32_t max_set_entries = 65'536;

int32_t named_type_index(
    const Vector<TilesetNamedType>& values, StringView name) noexcept
{
    for (size_t index = 0; index < values.size(); ++index) {
        if (string::icmp(values[index].name, name)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

bool load_named_types(const Ini& set,
    StringView count_key,
    StringView section_prefix,
    Vector<TilesetNamedType>& output)
{
    int32_t count = 0;
    if (!set.get_to(String{count_key}, count)
        || count < 0 || count > max_set_entries) {
        return false;
    }

    output.clear();
    output.reserve(static_cast<size_t>(count));
    for (int32_t index = 0; index < count; ++index) {
        const auto section = fmt::format("{}{}", section_prefix, index);
        TilesetNamedType value;
        if (!set.get_to(section + "/name", value.name)
            || value.name.empty()) {
            output.clear();
            return false;
        }
        output.push_back(std::move(value));
    }
    return true;
}

bool load_tile_topology(const Ini& set,
    size_t tile_index,
    const Vector<TilesetNamedType>& terrains,
    const Vector<TilesetNamedType>& crossers,
    TilesetTileTopology& output)
{
    static constexpr std::array<StringView, 4> corner_names{
        "topleft", "topright", "bottomleft", "bottomright"};
    static constexpr std::array<StringView, 4> edge_names{
        "top", "right", "bottom", "left"};

    const auto section = fmt::format("tile{}", tile_index);
    TilesetTileTopology candidate;
    for (size_t corner = 0; corner < corner_names.size(); ++corner) {
        String terrain;
        if (!set.get_to(fmt::format("{}/{}", section, corner_names[corner]), terrain)
            || !set.get_to(fmt::format("{}/{}height", section, corner_names[corner]),
                candidate.height[corner])) {
            return false;
        }
        candidate.terrain[corner] = named_type_index(terrains, terrain);
        if (candidate.terrain[corner] < 0) {
            return false;
        }
    }
    for (size_t edge = 0; edge < edge_names.size(); ++edge) {
        String crosser;
        if (!set.get_to(
                fmt::format("{}/{}", section, edge_names[edge]), crosser)) {
            return false;
        }
        if (!crosser.empty()) {
            candidate.crosser[edge] = named_type_index(crossers, crosser);
            if (candidate.crosser[edge] < 0) {
                return false;
            }
        }
    }
    candidate.valid = true;
    output = candidate;
    return true;
}

void load_groups(
    const Ini& set, StringView resref, Tileset& tileset)
{
    int32_t count = 0;
    if (!set.get_to("groups/count", count)) {
        return;
    }
    if (count < 0 || count > max_set_entries) {
        LOG_F(WARNING,
            "[tilesets] ignored invalid group count {} in {}.set",
            count, resref);
        return;
    }

    size_t rejected = 0;
    tileset.groups.reserve(static_cast<size_t>(count));
    for (int32_t index = 0; index < count; ++index) {
        const auto section = fmt::format("group{}", index);
        int32_t rows = 0;
        int32_t columns = 0;
        TilesetGroup group;
        const bool header_valid
            = set.get_to(section + "/rows", rows)
            && set.get_to(section + "/columns", columns)
            && rows > 0 && columns > 0
            && rows <= max_set_entries && columns <= max_set_entries
            && static_cast<uint64_t>(rows) * static_cast<uint64_t>(columns)
                <= static_cast<uint64_t>(max_set_entries);
        if (!header_valid) {
            ++rejected;
            continue;
        }

        group.rows = static_cast<uint32_t>(rows);
        group.columns = static_cast<uint32_t>(columns);
        group.tile_count = group.rows * group.columns;
        Vector<int32_t> ids;
        ids.reserve(group.tile_count);
        bool valid = true;
        for (uint32_t tile = 0; tile < group.tile_count; ++tile) {
            int32_t tile_id = -2;
            if (!set.get_to(fmt::format("{}/tile{}", section, tile), tile_id)
                || tile_id < -1
                || (tile_id >= 0
                    && static_cast<size_t>(tile_id) >= tileset.tiles.size())) {
                valid = false;
                break;
            }
            ids.push_back(tile_id);
        }
        if (!valid
            || tileset.group_tile_ids.size()
                > std::numeric_limits<uint32_t>::max() - ids.size()) {
            ++rejected;
            continue;
        }

        group.tile_offset
            = static_cast<uint32_t>(tileset.group_tile_ids.size());
        tileset.group_tile_ids.insert(
            tileset.group_tile_ids.end(), ids.begin(), ids.end());
        tileset.groups.push_back(std::move(group));
        for (const int32_t tile_id : ids) {
            if (tile_id >= 0) {
                tileset.grouped_tiles[static_cast<size_t>(tile_id)] = 1;
            }
        }
    }
    if (rejected > 0) {
        LOG_F(WARNING,
            "[tilesets] ignored {} malformed groups in {}.set",
            rejected, resref);
    }
}

} // namespace

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
        LOG_F(ERROR, "[tilesets] unable to locate set file: {}.set", resref);
        return nullptr;
    }

    nw::Ini set{std::move(rd)};
    if (!set.valid()) {
        LOG_F(ERROR, "[tilesets] failed to parse set file: {}.set", resref);
        return nullptr;
    }

    int32_t temp = 0;
    Tileset tileset;

    set.get_to("general/displayname", tileset.strref);
    set.get_to("general/UnlocalizedName", tileset.name);
    set.get_to("general/transition", tileset.tile_height);
    (void)set.get_to(
        "general/hasheighttransition", tileset.has_height_transition);

    const bool terrain_catalog_valid = load_named_types(set,
        "terrain types/count", "terrain", tileset.terrains);
    const bool crosser_catalog_valid = load_named_types(set,
        "crosser types/count", "crosser", tileset.crossers);
    if (!terrain_catalog_valid || !crosser_catalog_valid) {
        LOG_F(WARNING,
            "[tilesets] topology catalogs are unavailable in {}.set; exact rendering remains available",
            resref);
    }
    String default_terrain;
    if (set.get_to("general/default", default_terrain)) {
        tileset.default_terrain
            = named_type_index(tileset.terrains, default_terrain);
    }
    if (!set.get_to("tiles/count", temp)
        || temp < 0 || temp > max_set_entries) {
        LOG_F(ERROR, "[tilesets] unable to determine tile count: {}.set", resref);
        return nullptr;
    }
    tileset.tiles.resize(size_t(temp));
    tileset.tile_topologies.resize(size_t(temp));
    tileset.grouped_tiles.resize(size_t(temp), 0);

    // Custom SET exporters can write unrelated memory bytes into this count.
    // A tile has a finite list of hook sections, so bound both allocation and work.
    constexpr int32_t max_door_slots_per_tile = 64;
    size_t rejected_door_count_count = 0;
    size_t rejected_door_slot_count = 0;
    for (size_t i = 0; i < tileset.tiles.size(); ++i) {
        auto key = fmt::format("tile{}", i);
        if (!set.get_to(key + "/model", tileset.tiles[i].model)) {
            LOG_F(ERROR, "[tilesets] failed to load tile {} in {}.set", i, resref);
            return nullptr;
        }
        (void)set.get_to(key + "/imagemap2d", tileset.tiles[i].image_map_2d);
        (void)set.get_to(key + "/pathnode", tileset.tiles[i].path_node);
        (void)set.get_to(key + "/orientation",
            tileset.tiles[i].path_node_orientation);
        if (terrain_catalog_valid && crosser_catalog_valid) {
            (void)load_tile_topology(set, i,
                tileset.terrains, tileset.crossers,
                tileset.tile_topologies[i]);
        }

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

    load_groups(set, resref, tileset);

    size_t invalid_topologies = 0;
    for (const auto& topology : tileset.tile_topologies) {
        invalid_topologies += topology.valid ? 0u : 1u;
    }
    if (invalid_topologies > 0) {
        LOG_F(WARNING,
            "[tilesets] {} of {} tile topology rows are unavailable in {}.set",
            invalid_topologies, tileset.tile_topologies.size(), resref);
    }

    auto [inserted, was_inserted]
        = tileset_map_.emplace(String{resref}, std::move(tileset));
    if (!was_inserted) {
        return &inserted->second;
    }
    ++metrics_.tilesets_loaded;
    return &inserted->second;
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
