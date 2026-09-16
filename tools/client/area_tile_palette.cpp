#include "area_tile_palette.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Palette.hpp>
#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/util/string.hpp>

#include <absl/strings/match.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>

namespace nw::toolset {
namespace {

std::string thumbnail_source(
    uint64_t generation, std::string_view tileset, int32_t tile_id)
{
    std::string result = "rollnw-area-tile://v1/";
    result += std::to_string(generation);
    result += '/';
    result += tileset;
    result += '/';
    result += std::to_string(tile_id);
    return result;
}

bool load_thumbnail(AreaTilePaletteRow& row, RmlGeneratedTexture& output)
{
    if (row.image_map_2d.empty()) {
        return false;
    }
    std::unique_ptr<Image> image{
        kernel::resman().texture(Resref{row.image_map_2d})};
    if (!image || !copy_image_rgba(*image, image->is_bio_dds(), output)) {
        return false;
    }
    output.visible_width = output.width;
    output.visible_height = output.height;
    output.source = row.thumbnail_source;
    return true;
}

std::string palette_label(const PaletteTreeNode& node)
{
    if (node.strref != std::numeric_limits<uint32_t>::max()) {
        auto label = kernel::strings().get(node.strref);
        if (!label.empty()) {
            return label;
        }
    }
    if (!node.name.empty()) {
        return node.name;
    }
    return node.resref.string();
}

std::unique_ptr<Palette> load_tileset_palette(StringView tileset)
{
    const Resref palette_resref{String{tileset} + "palstd"};
    auto data = kernel::resman().demand(
        {palette_resref, ResourceType::itp});
    if (data.bytes.size() == 0) {
        return nullptr;
    }

    const auto bytes = data.bytes.string_view();
    const auto first = std::find_if(bytes.begin(), bytes.end(),
        [](char value) {
            return std::isspace(static_cast<unsigned char>(value)) == 0;
        });
    try {
        if (first != bytes.end() && *first == '{') {
            auto result = std::make_unique<Palette>();
            result->from_json(nlohmann::json::parse(bytes));
            if (!result->valid()) {
                return nullptr;
            }
            return result;
        }
    } catch (const nlohmann::json::exception&) {
        return nullptr;
    }

    Gff gff{std::move(data)};
    if (!gff.valid()) {
        return nullptr;
    }
    auto result = std::make_unique<Palette>(gff);
    if (!result->valid()) {
        return nullptr;
    }
    return result;
}

int32_t find_named_type(
    const Vector<TilesetNamedType>& values, StringView name) noexcept
{
    for (size_t index = 0; index < values.size(); ++index) {
        if (string::icmp(values[index].name, name)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

int32_t find_group(const Tileset& tileset, StringView model) noexcept
{
    for (size_t index = 0; index < tileset.groups.size(); ++index) {
        const auto& group = tileset.groups[index];
        if (group.tile_count == 0
            || group.tile_offset >= tileset.group_tile_ids.size()) {
            continue;
        }
        const int32_t tile_id = tileset.group_tile_ids[group.tile_offset];
        if (tile_id >= 0 && static_cast<size_t>(tile_id) < tileset.tiles.size()
            && string::icmp(tileset.tiles[static_cast<size_t>(tile_id)].model,
                model)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

constexpr uint32_t invalid_palette_row = UINT32_MAX;

uint32_t append_folder_row(AreaTilePalette& output,
    std::string label,
    uint32_t parent)
{
    if (output.rows.size()
        >= static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::length_error{"Tile palette row limit exceeded"};
    }
    const uint32_t index = static_cast<uint32_t>(output.rows.size());
    output.rows.push_back({
        .kind = AreaTilePaletteRowKind::folder,
        .parent = parent,
        .subtree_end = index + 1,
        .label = std::move(label),
    });
    return index;
}

uint32_t append_palette_row(AreaTilePalette& output,
    const Tileset& tileset,
    uint32_t parent,
    AreaTileBrush brush,
    std::string label,
    std::string category,
    std::string resref,
    int32_t thumbnail_tile_id = -1)
{
    if (output.rows.size()
        >= static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::length_error{"Tile palette row limit exceeded"};
    }
    const uint32_t index = static_cast<uint32_t>(output.rows.size());
    AreaTilePaletteRow row{
        .kind = AreaTilePaletteRowKind::action,
        .parent = parent,
        .subtree_end = index + 1,
        .brush = brush,
        .label = std::move(label),
        .category = std::move(category),
        .resref = std::move(resref),
    };
    if (thumbnail_tile_id >= 0
        && static_cast<size_t>(thumbnail_tile_id) < tileset.tiles.size()) {
        row.image_map_2d
            = tileset.tiles[static_cast<size_t>(thumbnail_tile_id)].image_map_2d;
        row.thumbnail_source = thumbnail_source(output.resource_generation,
            output.tileset, thumbnail_tile_id);
    }
    output.rows.push_back(std::move(row));
    return index;
}

uint32_t append_palette_leaf(AreaTilePalette& output,
    const Tileset& tileset,
    const PaletteTreeNode& node,
    uint32_t parent,
    uint8_t category_id,
    const std::string& category)
{
    const std::string resref = node.resref.string();
    const std::string label = palette_label(node);
    if (category_id == 2) {
        if (string::icmp(resref, "eraser")) {
            if (tileset.default_terrain >= 0) {
                return append_palette_row(output, tileset, parent,
                    {
                        .kind = AreaTileBrushKind::eraser,
                        .value = tileset.default_terrain,
                    },
                    label.empty() ? "Eraser" : label,
                    category, resref);
            }
            return invalid_palette_row;
        }
        if (string::icmp(resref, "raiselower")) {
            if (tileset.has_height_transition) {
                return append_palette_row(output, tileset, parent,
                    {.kind = AreaTileBrushKind::raise},
                    label.empty() ? "Raise / Lower Terrain" : label,
                    category, resref);
            }
            return invalid_palette_row;
        }
        if (const int32_t terrain
            = find_named_type(tileset.terrains, resref);
            terrain >= 0) {
            return append_palette_row(output, tileset, parent,
                {
                    .kind = AreaTileBrushKind::terrain,
                    .value = terrain,
                },
                label, category, resref);
        }
        if (const int32_t crosser
            = find_named_type(tileset.crossers, resref);
            crosser >= 0) {
            return append_palette_row(output, tileset, parent,
                {
                    .kind = AreaTileBrushKind::crosser,
                    .value = crosser,
                },
                label, category, resref);
        }
        return invalid_palette_row;
    }

    const int32_t group_index = find_group(tileset, resref);
    if (group_index < 0) {
        return invalid_palette_row;
    }
    const auto& group = tileset.groups[static_cast<size_t>(group_index)];
    const int32_t thumbnail_tile_id
        = group.tile_count > 0
        ? tileset.group_tile_ids[group.tile_offset]
        : -1;
    return append_palette_row(output, tileset, parent,
        {
            .kind = AreaTileBrushKind::group,
            .value = group_index,
        },
        label, category, resref, thumbnail_tile_id);
}

uint32_t append_palette_node(AreaTilePalette& output,
    const Tileset& tileset,
    const PaletteTreeNode& node,
    uint32_t parent,
    uint8_t category_id,
    const std::string& category,
    bool hidden)
{
    const bool node_hidden = hidden
        || (node.type != PaletteNodeType::blueprint && node.display == 1);
    if (node_hidden) {
        return invalid_palette_row;
    }
    uint8_t child_category_id = category_id;
    std::string child_category = category;
    if (node.type == PaletteNodeType::category) {
        child_category_id = node.id;
        child_category = palette_label(node);
    }
    if (node.type == PaletteNodeType::blueprint) {
        return append_palette_leaf(output, tileset, node, parent,
            child_category_id, child_category);
    }

    const size_t row_mark = output.rows.size();
    const size_t child_mark = output.child_rows.size();
    const uint32_t folder = append_folder_row(
        output, palette_label(node), parent);
    std::vector<uint32_t> children;
    children.reserve(node.children.size());
    for (const auto* child : node.children) {
        if (child) {
            const uint32_t child_index = append_palette_node(output, tileset,
                *child, folder, child_category_id, child_category, node_hidden);
            if (child_index != invalid_palette_row) {
                children.push_back(child_index);
            }
        }
    }
    if (children.empty()) {
        output.rows.resize(row_mark);
        output.child_rows.resize(child_mark);
        return invalid_palette_row;
    }
    if (output.child_rows.size()
        > std::numeric_limits<uint32_t>::max() - children.size()) {
        throw std::length_error{"Tile palette child limit exceeded"};
    }
    const uint32_t child_offset
        = static_cast<uint32_t>(output.child_rows.size());
    output.child_rows.insert(
        output.child_rows.end(), children.begin(), children.end());
    auto& row = output.rows[folder];
    row.child_offset = child_offset;
    row.child_count = static_cast<uint32_t>(children.size());
    row.subtree_end = static_cast<uint32_t>(output.rows.size());
    return folder;
}

} // namespace

bool build_area_tile_palette(ObjectHandle area_handle, AreaTilePalette& output)
{
    output = {};
    const auto* area = kernel::objects().get<Area>(area_handle);
    if (!area || !area->tileset) {
        output.status = AreaTilePaletteStatus::invalid_input;
        output.diagnostic = "Area or tileset is unavailable";
        return false;
    }
    if (area->tileset->tiles.size()
            > static_cast<size_t>(std::numeric_limits<int32_t>::max())
        || area->tileset->tile_topologies.size()
            != area->tileset->tiles.size()) {
        output.status = AreaTilePaletteStatus::invalid_input;
        output.diagnostic = "Tileset topology is unavailable";
        return false;
    }

    output.area = area_handle;
    output.tileset = area->tileset_resref.string();
    output.resource_generation = kernel::resman().generation();
    try {
        auto palette = load_tileset_palette(output.tileset);
        if (!palette) {
            output.status = AreaTilePaletteStatus::invalid_input;
            output.diagnostic = "Tileset palette " + output.tileset
                + "palstd.itp is unavailable";
            return false;
        }
        output.root_folder = append_folder_row(output, "Tiles",
            invalid_palette_row);
        std::vector<uint32_t> root_children;
        root_children.reserve(palette->children.size());
        for (const auto* node : palette->children) {
            if (node) {
                const uint32_t row_index = append_palette_node(output,
                    *area->tileset, *node, output.root_folder,
                    std::numeric_limits<uint8_t>::max(), {}, false);
                if (row_index != invalid_palette_row) {
                    root_children.push_back(row_index);
                }
            }
        }
        if (root_children.empty()) {
            output.status = AreaTilePaletteStatus::invalid_input;
            output.diagnostic = "Tileset palette contains no usable actions";
            return false;
        }
        if (output.child_rows.size()
            > std::numeric_limits<uint32_t>::max() - root_children.size()) {
            throw std::length_error{"Tile palette child limit exceeded"};
        }
        auto& root = output.rows[output.root_folder];
        root.child_offset = static_cast<uint32_t>(output.child_rows.size());
        root.child_count = static_cast<uint32_t>(root_children.size());
        root.subtree_end = static_cast<uint32_t>(output.rows.size());
        output.child_rows.insert(output.child_rows.end(),
            root_children.begin(), root_children.end());
        output.current_folder = output.root_folder;
    } catch (const std::bad_alloc&) {
        output = {};
        output.status = AreaTilePaletteStatus::failed;
        output.diagnostic = "Tile palette allocation failed";
        return false;
    } catch (const std::length_error&) {
        output = {};
        output.status = AreaTilePaletteStatus::failed;
        output.diagnostic = "Tile palette exceeds container capacity";
        return false;
    }

    output.status = AreaTilePaletteStatus::ready;
    return filter_area_tile_palette(output, {});
}

bool filter_area_tile_palette(AreaTilePalette& palette, std::string_view query)
{
    palette.matches.clear();
    if (palette.status != AreaTilePaletteStatus::ready
        || palette.current_folder >= palette.rows.size()
        || palette.rows[palette.current_folder].kind
            != AreaTilePaletteRowKind::folder) {
        return false;
    }

    try {
        const absl::string_view needle{query.data(), query.size()};
        const auto& folder = palette.rows[palette.current_folder];
        if (needle.empty()) {
            const uint64_t child_end
                = static_cast<uint64_t>(folder.child_offset)
                + static_cast<uint64_t>(folder.child_count);
            if (child_end > palette.child_rows.size()) {
                palette.status = AreaTilePaletteStatus::failed;
                palette.diagnostic = "Tile palette child range is malformed";
                return false;
            }
            palette.matches.insert(palette.matches.end(),
                palette.child_rows.begin() + folder.child_offset,
                palette.child_rows.begin()
                    + static_cast<std::ptrdiff_t>(child_end));
            return true;
        }
        if (folder.subtree_end <= palette.current_folder
            || folder.subtree_end > palette.rows.size()) {
            palette.status = AreaTilePaletteStatus::failed;
            palette.diagnostic = "Tile palette subtree range is malformed";
            return false;
        }
        palette.matches.reserve(
            static_cast<size_t>(folder.subtree_end - palette.current_folder));
        for (uint32_t index = palette.current_folder + 1;
            index < folder.subtree_end; ++index) {
            const auto& row = palette.rows[index];
            if (row.kind == AreaTilePaletteRowKind::action
                && (absl::StrContainsIgnoreCase(row.label, needle)
                    || absl::StrContainsIgnoreCase(row.category, needle)
                    || absl::StrContainsIgnoreCase(row.resref, needle))) {
                palette.matches.push_back(index);
            }
        }
    } catch (const std::bad_alloc&) {
        palette.matches.clear();
        palette.status = AreaTilePaletteStatus::failed;
        palette.diagnostic = "Tile palette filter allocation failed";
        return false;
    } catch (const std::length_error&) {
        palette.matches.clear();
        palette.status = AreaTilePaletteStatus::failed;
        palette.diagnostic = "Tile palette filter exceeds container capacity";
        return false;
    }
    return true;
}

bool enter_area_tile_palette_folder(
    AreaTilePalette& palette, uint32_t row_index)
{
    if (palette.status != AreaTilePaletteStatus::ready
        || row_index >= palette.rows.size()
        || palette.rows[row_index].kind != AreaTilePaletteRowKind::folder
        || palette.rows[row_index].parent != palette.current_folder) {
        return false;
    }
    palette.current_folder = row_index;
    return filter_area_tile_palette(palette, {});
}

bool leave_area_tile_palette_folder(AreaTilePalette& palette)
{
    if (palette.status != AreaTilePaletteStatus::ready
        || palette.current_folder == palette.root_folder
        || palette.current_folder >= palette.rows.size()) {
        return false;
    }
    const uint32_t parent = palette.rows[palette.current_folder].parent;
    if (parent >= palette.rows.size()
        || palette.rows[parent].kind != AreaTilePaletteRowKind::folder) {
        return false;
    }
    palette.current_folder = parent;
    return filter_area_tile_palette(palette, {});
}

bool load_area_tile_palette_thumbnails(
    AreaTilePalette& palette, int match_begin, int match_end)
{
    if (palette.status != AreaTilePaletteStatus::ready
        || match_begin < 0 || match_end < match_begin
        || static_cast<size_t>(match_end) > palette.matches.size()
        || palette.resource_generation != kernel::resman().generation()) {
        return false;
    }

    std::vector<RmlGeneratedTexture> textures;
    try {
        textures.reserve(static_cast<size_t>(match_end - match_begin));
        for (int match_index = match_begin; match_index < match_end; ++match_index) {
            auto& row = palette.rows[palette.matches[static_cast<size_t>(match_index)]];
            if (row.thumbnail_source.empty()) {
                continue;
            }
            RmlGeneratedTexture texture;
            if (load_thumbnail(row, texture)) {
                textures.push_back(std::move(texture));
            } else {
                row.thumbnail_source.clear();
            }
        }
        std::sort(textures.begin(), textures.end(),
            [](const RmlGeneratedTexture& lhs, const RmlGeneratedTexture& rhs) {
                return lhs.source < rhs.source;
            });
    } catch (const std::bad_alloc&) {
        palette.status = AreaTilePaletteStatus::failed;
        palette.diagnostic = "Tile thumbnail allocation failed";
        return false;
    } catch (const std::length_error&) {
        palette.status = AreaTilePaletteStatus::failed;
        palette.diagnostic = "Tile thumbnail exceeds container capacity";
        return false;
    }
    palette.textures.swap(textures);
    return true;
}

} // namespace nw::toolset
