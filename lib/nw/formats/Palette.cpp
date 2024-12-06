#include "Palette.hpp"

#include "../kernel/Memory.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

PaletteTreeNode::PaletteTreeNode(nw::MemoryResource*)
{
}

void PaletteTreeNode::clear()
{
    id = std::numeric_limits<uint8_t>::max();
    name.clear();
    strref = std::numeric_limits<uint32_t>::max();
    display = 0;
    resref = "";
    cr = 0.0;
    faction.clear();

    for (auto& it : children) {
        parent->node_pool_.free(it);
    }

    children.clear();
}

Palette::Palette()
    : root{nw::kernel::global_allocator()}
    , node_pool_(256, nw::kernel::global_allocator())
{
}

Palette::Palette(const Gff& archive)
    : Palette()
{
    is_valid_ = load(archive.toplevel());
}

Palette::~Palette()
{
    for (auto& it : root.children) {
        node_pool_.free(it);
    }
}

PaletteTreeNode* Palette::add_node(std::string_view name, uint32_t strref)
{
    auto result = node_pool_.allocate();
    result->parent = this;
    return result;
}

bool Palette::is_skeleton() const noexcept
{
    return resource_type != nw::ResourceType::invalid;
}

PaletteTreeNode* Palette::read_child(Palette* parent, const GffStruct st)
{
    PaletteTreeNode* node = parent->add_node("");

    st.get_to("STRREF", node->strref, false);
    // Only try DELETE_ME if there is no name.
    st.get_to("NAME", node->name, false) || st.get_to("DELETE_ME", node->name, false);

    if (st.has_field("RESREF")) {
        node->type = PaletteNodeType::blueprint;
    } else if (st.has_field("ID")) {
        node->type = PaletteNodeType::category;
    } else {
        node->type = PaletteNodeType::branch;
    }

    st.get_to("ID", node->id, false);
    st.get_to("TYPE", node->display, false);

    // Assume this isn't a skeleton
    if (node->type == PaletteNodeType::blueprint) {
        st.get_to("RESREF", node->resref, false);
        st.get_to("CR", node->cr, false);
        st.get_to("FACTION", node->faction, false);
    } else {
        size_t list_size = st["LIST"].size();
        node->children.reserve(list_size);
        for (size_t i = 0; i < list_size; ++i) {
            node->children.push_back(read_child(parent, st["LIST"][i]));
        }
    }

    return node;
}

bool Palette::load(const GffStruct gff)
{
    size_t list_size = gff["MAIN"].size();
    if (list_size == 0) {
        LOG_F(ERROR, "No main palette list!");
        return false;
    }

    // Skeleton Palettes
    uint16_t temp;
    if (gff.get_to("RESTYPE", temp, false)) {
        resource_type = static_cast<ResourceType::type>(temp);
        gff.get_to("NEXT_USEABLE_ID", next_id_, false);

        if (resource_type == ResourceType::set && !gff.get_to("TILESETRESREF", tileset)) {
            LOG_F(ERROR, "palette no tileset resref specified");
            return false;
        }
    }

    root.children.reserve(list_size);
    for (size_t i = 0; i < list_size; ++i) {
        root.children.push_back(read_child(this, gff["MAIN"][i]));
    }

    return true;
}

PaletteTreeNode* read_node(Palette& self, const nlohmann::json& archive)
{
    LOG_F(INFO, "Reading node");
    PaletteTreeNode* node = self.add_node("");

    node->name = archive.at("name").get<std::string>();
    node->strref = archive.at("strref").get<uint32_t>();

    if (archive.find("id") != std::end(archive)) {
        node->type = PaletteNodeType::category;
        node->id = archive.at("id").get<int>();
        node->display = archive.at("display").get<int>();
    } else if (archive.find("resref") != std::end(archive)) {
        node->type = PaletteNodeType::blueprint;
        node->resref = Resref(archive.at("resref").get<std::string>());

        if (self.resource_type == ResourceType::utc) {
            node->cr = archive.at("cr").get<float>();
            node->faction = archive.at("faction").get<int>();
        }
    } else {
        node->type = PaletteNodeType::branch;
    }

    if (archive.find("|children") != std::end(archive)) {
        for (const auto& it : archive.at("|children")) {
            node->children.push_back(read_node(self, it));
        }
    } else {
        LOG_F(INFO, "no children");
    }

    return node;
}

void Palette::from_json(const nlohmann::json& archive)
{
    try {
        nlohmann::json j;

        auto it = archive.find("resource_type");
        if (it != archive.end()) {
            resource_type = ResourceType::from_extension(it->get<std::string>());
            next_id_ = archive.at("next_available_id").get<int>();
            if (resource_type == ResourceType::set) {
                tileset = Resref(archive.at("tileset").get<std::string>());
            }
        }

        for (const auto& it : archive.at("root")) {
            root.children.push_back(read_node(*this, it));
        }
        is_valid_ = true;
    } catch (nlohmann::json::exception&) {
        is_valid_ = false;
    }
}

nlohmann::json process_node(nw::ResourceType::type restype, const PaletteTreeNode& node, bool is_skeleton)
{
    nlohmann::json res;

    if (node.type == PaletteNodeType::category) {
        res["id"] = node.id;
        res["display"] = node.display;
    }

    res["name"] = node.name;
    res["strref"] = node.strref;

    if (node.type == PaletteNodeType::blueprint) {
        res["resref"] = node.resref;

        if (restype == ResourceType::utc) {
            res["cr"] = node.cr;
            res["faction"] = node.faction;
        }
    }

    if (node.type != PaletteNodeType::blueprint && !node.children.empty()) {
        // [NOTE] '|' is not a type, it's much easier to read if chidren come last.
        auto& arr = res["|children"] = nlohmann::json::array();
        for (const auto& c : node.children) {
            arr.push_back(process_node(restype, *c, is_skeleton));
        }
    }
    return res;
}

nlohmann::json Palette::to_json(nw::ResourceType::type restype) const
{
    nlohmann::json j;

    // This is in distinction to GFF, for some weird reason the toolset deletes resource type
    // out of skeletons when building palettes.  While it's true that one will always know the
    // resource type of an ITP, it still seems silly to delete it.

    if (is_skeleton()) {
        j["resource_type"] = ResourceType::to_string(restype);
        j["next_available_id"] = next_id_;
        if (restype == ResourceType::set) {
            j["tileset"] = tileset;
        }
    }

    auto& arr = j["root"] = nlohmann::json::array();
    for (const auto& c : root.children) {
        arr.push_back(process_node(restype, *c, is_skeleton()));
    }

    return j;
}

} // namespace nw
