#include "Palette.hpp"

namespace nw {

Palette::Palette(const GffInputArchiveStruct gff)
{
    is_valid_ = load(gff);
}

bool Palette::load(const GffInputArchiveStruct gff)
{
    size_t list_size = gff["MAIN"].size();
    if (list_size == 0) {
        LOG_F(ERROR, "No main palette list!");
        return false;
    }

    // Skeleton Palettes
    uint16_t temp;
    if (gff.get_to("RESTYPE", temp, false)) {
        is_skeleton = true;
        restype = static_cast<ResourceType::type>(temp);
        gff.get_to("NEXT_USEABLE_ID", next_id_, false);

        if (restype == ResourceType::set && !gff.get_to("TILESETRESREF", tileset)) {
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

PaletteTreeNode Palette::read_child(Palette* parent, const GffInputArchiveStruct st)
{
    PaletteTreeNode node;

    st.get_to("STRREF", node.strref, false);
    // Only try DELETE_ME if there is no name.
    st.get_to("NAME", node.name, false) || st.get_to("DELETE_ME", node.name, false);

    if (st.has_field("RESREF")) {
        node.type = PaletteNodeType::blueprint;
    } else if (st.has_field("ID")) {
        node.type = PaletteNodeType::category;
    } else {
        node.type = PaletteNodeType::branch;
    }

    st.get_to("ID", node.id, false);
    if (node.id != 0xFF) { parent->max_id_ = std::max(node.id, parent->max_id_); }

    st.get_to("TYPE", node.display, false);

    // Assume this isn't a skeleton
    if (node.type == PaletteNodeType::blueprint) {
        st.get_to("RESREF", node.resref, false);
        st.get_to("CR", node.cr, false);
        st.get_to("FACTION", node.faction, false);
    } else {
        size_t list_size = st["LIST"].size();
        node.children.reserve(list_size);
        for (size_t i = 0; i < list_size; ++i) {
            node.children.push_back(read_child(parent, st["LIST"][i]));
        }
    }

    return node;
}

} // namespace nw
