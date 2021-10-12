#include "Object.hpp"

namespace nw {

Object::Object(const GffStruct gff, SerializationProfile profile)
    : locals{gff, profile}
    , location{gff, profile}
{
    if (!gff.get_to("TemplateResRef", resref, false)
        && !gff.get_to("ResRef", resref, false)) { // Store blueprints do their own thing
        LOG_F(ERROR, "invalid object no resref");
        return;
    }

    gff.get_to("Tag", tag);
    gff.get_to("Faction", faction);

    if (profile == SerializationProfile::blueprint) {
        gff.get_to("Comment", comment);
        gff.get_to("PaletteID", palette_id);
    }

    valid_ = true;
}

} // namespace nw
