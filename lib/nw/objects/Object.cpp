#include "Object.hpp"

namespace nw {

Object::Object(const GffStruct gff, SerializationProfile profile)
{
    gff.get_to("TemplateResRef", resref);
    gff.get_to("Tag", tag);
    gff.get_to("Faction", faction);

    if (profile == SerializationProfile::blueprint) {
        gff.get_to("Comment", comment);
        gff.get_to("PaletteID", palette_id);
    }
}

} // namespace nw
