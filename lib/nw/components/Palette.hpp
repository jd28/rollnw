#pragma once

#include "../serialization/Archives.hpp"

#include <string>

namespace nw {

enum struct PaletteNodeType {
    branch,   // Can only contain other branches and categories.
    category, // Can only contains blueprints (i.e. leaf nodes)
    blueprint
};

struct PaletteTreeNode {
    PaletteTreeNode() = default;

    PaletteNodeType type;
    uint8_t id = std::numeric_limits<uint8_t>::max();

    // Very rarely used, controls the visibility of category nodes.
    // 0 - Display only if not empty.
    // 1 - Never display (in Toolset)
    // 2 - Display in a custom blueprint palette, when creating a blueprint,
    //     and assigning a palette category
    uint8_t display = 0;

    // Every node has a name or a strref present, if both are, strref wins,
    // but both are retained.
    std::string name;
    uint32_t strref = std::numeric_limits<uint32_t>::max();

    // Extra data dependent on palette resource type,
    // the game appears to handle this with inheritance, but no thanks.
    // Note that in the case of Tilesets, the node ID of the category,
    // determines what the resref is referring to.  There are a couple
    // special tileset nodes that will not have a resref at all: Eraser
    // and Raise/Lower
    Resref resref;

    // Only if restype == ResourceType::utc
    float cr = 0.0;
    std::string faction;

    std::vector<PaletteTreeNode> children;
};

struct Palette {
    explicit Palette(const Gff& gff);
    ~Palette() = default;

    static constexpr int json_archive_version = 1;

    uint8_t max_id() const noexcept { return max_id_; }
    void set_max_id(uint8_t id) noexcept { max_id_ = std::max(id, max_id_); }
    bool valid() const noexcept { return is_valid_; }

    nlohmann::json to_json(nw::ResourceType::type restype) const;

    PaletteTreeNode root;
    ResourceType::type resource_type; // Only in skeletons
    Resref tileset;                   // Only if restype is ResourceType::set
    bool is_skeleton = false;

private:
    bool load(const GffStruct gff);
    PaletteTreeNode read_child(Palette* parent, const GffStruct st);

    uint8_t max_id_ = 0;
    uint8_t next_id_ = 0;
    bool is_valid_ = false;
};

} // namespace nw
