#pragma once

#include "../resources/ResourceType.hpp"
#include "../resources/Resref.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/memory.hpp"

#include <absl/container/flat_hash_map.h>

namespace nw {

struct Palette;

enum struct PaletteNodeType {
    branch,   // Can only contain other branches and categories.
    category, // Can only contains blueprints (i.e. leaf nodes)
    blueprint
};

struct PaletteTreeNode {
    explicit PaletteTreeNode(nw::MemoryResource* allocator);

    /// Add blueprint node to root
    PaletteTreeNode* add_blueprint(String name, Resref resref, uint32_t strref);

    /// Add branch node to root
    PaletteTreeNode* add_branch(String name, uint32_t strref);

    /// Add category to root
    PaletteTreeNode* add_category(String name, uint32_t strref, int id = -1);

    ///  Clears node returning it to default constructed state
    void clear();

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
    String name;
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
    String faction;

    Palette* parent = nullptr;
    Vector<PaletteTreeNode*> children;
};

struct Palette {
    Palette();
    explicit Palette(const Gff& gff);
    ~Palette();

    static constexpr int json_archive_version = 1;
    static constexpr ResourceType::type restype = ResourceType::utc;
    static constexpr StringView serial_id{"ITP"};

    /// Add branch node to root
    PaletteTreeNode* add_branch(String name, uint32_t strref);

    /// Add category to root
    PaletteTreeNode* add_category(String name, uint32_t strref, int id = -1);

    /// Makes a node and adds it to root
    PaletteTreeNode* make_node();

    bool is_skeleton() const noexcept;
    bool valid() const noexcept { return is_valid_; }

    void from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    Vector<PaletteTreeNode*> children;
    ResourceType::type resource_type = nw::ResourceType::invalid; // Only in skeletons
    Resref tileset;                                               // Only if restype is ResourceType::set

    // Private
    uint8_t next_id_ = 0;
    bool is_valid_ = false;

    ObjectPool<PaletteTreeNode> node_pool_;
    absl::flat_hash_map<uint8_t, PaletteTreeNode*> node_map_;
};

// == Palette - Serialization - Gff ===========================================
// ============================================================================

bool deserialize(Palette& obj, const GffStruct& archive);
GffBuilder serialize(const Palette& obj);

} // namespace nw
