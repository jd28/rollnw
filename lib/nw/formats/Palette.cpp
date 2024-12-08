#include "Palette.hpp"

#include "../kernel/Memory.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

namespace nw {

PaletteTreeNode::PaletteTreeNode(nw::MemoryResource*)
{
}

PaletteTreeNode* PaletteTreeNode::add_blueprint(String name, Resref resref, uint32_t strref)
{
    if (type != PaletteNodeType::category) {
        throw std::runtime_error(fmt::format("attempting to add a blueprint node '{}' to non-category node", name));
    }

    auto node = parent->make_node();
    node->type = PaletteNodeType::blueprint;
    node->name = std::move(name);
    node->strref = strref;
    node->resref = resref;
    children.push_back(node);
    return node;
}

PaletteTreeNode* PaletteTreeNode::add_branch(String name, uint32_t strref)
{
    if (type != PaletteNodeType::branch) {
        throw std::runtime_error(fmt::format("attempting to add a branch '{}' to non-branch node", name));
    }

    auto node = parent->make_node();
    node->type = PaletteNodeType::branch;
    node->name = std::move(name);
    node->strref = strref;
    children.push_back(node);
    return node;
}

PaletteTreeNode* PaletteTreeNode::add_category(String name, uint32_t strref, int id)
{
    if (type != PaletteNodeType::branch) {
        throw std::runtime_error(fmt::format("attempting to add a category node '{}' to non-branch node", name));
    }

    auto node = parent->make_node();
    node->type = PaletteNodeType::category;
    node->name = std::move(name);
    node->strref = strref;
    node->id = id < 0 ? parent->next_id_++ : id;
    children.push_back(node);
    parent->node_map_.insert({node->id, node});
    return node;
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
    : node_pool_(256, nw::kernel::global_allocator())
{
}

Palette::Palette(const Gff& archive)
    : Palette()
{
    is_valid_ = deserialize(*this, archive.toplevel());
}

Palette::~Palette()
{
    for (auto& it : children) {
        node_pool_.free(it);
    }
}

PaletteTreeNode* Palette::add_branch(String name, uint32_t strref)
{
    auto node = make_node();
    node->type = PaletteNodeType::branch;
    node->name = std::move(name);
    node->strref = strref;
    children.push_back(node);
    return node;
}

PaletteTreeNode* Palette::add_category(String name, uint32_t strref, int id)
{
    auto node = make_node();
    node->type = PaletteNodeType::category;
    node->name = std::move(name);
    node->strref = strref;
    node->id = id < 0 ? next_id_++ : id;
    children.push_back(node);
    node_map_.insert({node->id, node});
    return node;
}

PaletteTreeNode* Palette::make_node()
{
    auto result = node_pool_.allocate();
    result->parent = this;
    return result;
}

bool Palette::is_skeleton() const noexcept
{
    return resource_type != nw::ResourceType::invalid;
}

PaletteTreeNode* read_node(Palette& self, const nlohmann::json& archive)
{
    PaletteTreeNode* node = self.make_node();

    node->name = archive.at("name").get<std::string>();
    node->strref = archive.at("strref").get<uint32_t>();

    if (archive.find("id") != std::end(archive)) {
        node->type = PaletteNodeType::category;
        node->id = archive.at("id").get<int>();

        auto it = archive.find("display");
        if (it != archive.end()) {
            node->display = archive["display"].get<int>();
        }

        self.node_map_.insert({node->id, node});
    } else if (archive.find("resref") != std::end(archive)) {
        node->type = PaletteNodeType::blueprint;
        node->resref = Resref(archive.at("resref").get<std::string>());

        if (self.resource_type == ResourceType::utc) {
            node->cr = archive.at("cr").get<float>();
            node->faction = archive.at("faction").get<int>();
        }
    } else {
        node->type = PaletteNodeType::branch;

        auto it = archive.find("display");
        if (it != archive.end()) {
            node->display = archive["display"].get<int>();
        }
    }

    if (archive.find("|children") != std::end(archive)) {
        for (const auto& it : archive.at("|children")) {
            node->children.push_back(read_node(self, it));
        }
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
            children.push_back(read_node(*this, it));
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
    }

    if (node.display) {
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

nlohmann::json Palette::to_json() const
{
    nlohmann::json j;

    j["$type"] = serial_id;
    j["$version"] = json_archive_version;

    if (is_skeleton()) {
        j["resource_type"] = ResourceType::to_string(resource_type);
        j["next_available_id"] = next_id_;
        if (resource_type == ResourceType::set) {
            j["tileset"] = tileset;
        }
    }

    auto& arr = j["root"] = nlohmann::json::array();
    for (const auto& c : children) {
        arr.push_back(process_node(resource_type, *c, is_skeleton()));
    }

    return j;
}

// == Palette - Serialization - Gff ===========================================
// ============================================================================

PaletteTreeNode* read_child(Palette& parent, const GffStruct st)
{
    PaletteTreeNode* node = parent.make_node();

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

    // Assume this isn't a skeleton
    if (node->type == PaletteNodeType::blueprint) {
        st.get_to("RESREF", node->resref, false);
        st.get_to("CR", node->cr, false);
        st.get_to("FACTION", node->faction, false);
    } else {
        st.get_to("TYPE", node->display, false);
        if (node->type == PaletteNodeType::category) {
            st.get_to("ID", node->id);
            parent.node_map_.insert({node->id, node});
        }
        size_t list_size = st["LIST"].size();
        node->children.reserve(list_size);
        for (size_t i = 0; i < list_size; ++i) {
            node->children.push_back(read_child(parent, st["LIST"][i]));
        }
    }

    return node;
}

bool deserialize(Palette& obj, const GffStruct& archive)
{
    size_t list_size = archive["MAIN"].size();
    if (list_size == 0) {
        LOG_F(ERROR, "No main palette list!");
        return false;
    }

    // Skeleton Palettes
    uint16_t temp;
    if (archive.get_to("RESTYPE", temp, false)) {
        obj.resource_type = static_cast<ResourceType::type>(temp);
        archive.get_to("NEXT_USEABLE_ID", obj.next_id_, false);

        if (obj.resource_type == ResourceType::set && !archive.get_to("TILESETRESREF", obj.tileset)) {
            LOG_F(ERROR, "palette no tileset resref specified");
            return false;
        }
    }

    obj.children.reserve(list_size);
    for (size_t i = 0; i < list_size; ++i) {
        obj.children.push_back(read_child(obj, archive["MAIN"][i]));
    }

    return true;
}

inline void add_node(const Palette& obj, const PaletteTreeNode* node, GffBuilderStruct& str)
{

    str.add_field("STRREF", node->strref);
    str.add_field("DELETE_ME", node->name); // This should provide more compatibility over "NAME"

    if (node->type == PaletteNodeType::blueprint) {
        str.add_field("RESREF", node->resref);

        if (node->cr != 0.0f) {
            str.add_field("CR", node->cr);
        }

        if (!node->faction.empty()) {
            str.add_field("FACTION", node->faction);
        }
    } else {
        if (node->type == PaletteNodeType::category) {
            str.add_field("ID", node->id);
        }

        if (node->display) {
            str.add_field("TYPE", node->display);
        }

        if (node->children.size()) {
            auto& list = str.add_list("LIST");
            for (auto it : node->children) {
                add_node(obj, it, list.push_back(1));
            }
        }
    }
}

GffBuilder serialize(const Palette& obj)
{
    GffBuilder result{Palette::serial_id};
    auto& top = result.top;

    if (obj.resource_type != ResourceType::invalid) {
        top.add_field("RESTYPE", static_cast<uint16_t>(obj.resource_type));
        top.add_field("NEXT_USEABLE_ID", obj.next_id_);

        if (obj.resource_type == ResourceType::set) {
            top.add_field("TILESETRESREF", obj.tileset);
        }
    }

    auto& main = top.add_list("MAIN");
    for (auto node : obj.children) {
        add_node(obj, node, main.push_back(1));
    }

    result.build();
    return result;
}

} // namespace nw
