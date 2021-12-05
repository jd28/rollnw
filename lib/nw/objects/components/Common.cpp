#include "Common.hpp"

namespace nw {

Common::Common(ObjectType obj_type, const GffInputArchiveStruct gff, SerializationProfile profile)
    : object_type{obj_type}
    , local_data{gff}
    , location{gff, profile}
{
    if (!gff.get_to("TemplateResRef", resref, false)
        && !gff.get_to("ResRef", resref, false)) { // Store blueprints do their own thing
        LOG_F(ERROR, "invalid object no resref");
        return;
    }

    if (obj_type != ObjectType::creature
        && !gff.get_to("LocalizedName", name, false)
        && !gff.get_to("LocName", name, false)) {
        LOG_F(WARNING, "object no localized name");
    }

    gff.get_to("Tag", tag);
    if (obj_type != ObjectType::creature) {
        gff.get_to("Faction", faction);
    }

    if (profile == SerializationProfile::blueprint) {
        gff.get_to("Comment", comment);
        gff.get_to("PaletteID", palette_id);
    }

    valid_ = true;
}

} // namespace nw
